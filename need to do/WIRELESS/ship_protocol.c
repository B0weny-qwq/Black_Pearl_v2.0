/**
 * @file    ship_protocol.c
 * @brief   Ship-side legacy wireless business protocol.
 * @author  boweny
 * @date    2026-05-07
 * @version v1.2
 *
 * @details
 * This file implements the ship-side legacy wireless business protocol.
 * Frame format: AA | len | cmd | payload... | xor | BB.
 * len = 2 + payload_len; xor covers len, cmd, and all payload bytes.
 *
 * Responsibilities:
 * - send pair requests on the fixed pair channel, then listen on the
 *   calculated work channel;
 * - parse 0x11 throttle/key frames and forward manual input to ShipControl;
 * - reply to accepted frames with one legacy 0x12 GPS/status packet;
 * - forward 0x13/0x14/0x15 point and switch commands to AutoDrive;
 * - monitor link timeout, battery level, and AutoDrive diagnostics.
 */
#include "ship_protocol.h"
#include "wireless.h"
#include "..\..\Device\GPS\GPS.h"
#include "..\..\Device\AutoDrive\autodrive.h"
#include "..\..\Device\Control\ShipControl.h"
#include "..\..\Function\Log\Log.h"
#include "..\..\..\User\MainLoop.h"
#include "..\..\..\User\Task.h"
#include "..\..\..\Driver\inc\STC32G_ADC.h"

#if !SHIP_PROTOCOL_DIAG_ENABLE
#undef LOGI
#undef LOGW
#undef LOGD
#define LOGI(tag, ...)
#define LOGW(tag, ...)
#define LOGD(tag, ...)
#endif

#ifndef SHIP_PROTOCOL_VIEWER_LOG_ENABLE
#define SHIP_PROTOCOL_VIEWER_LOG_ENABLE SHIP_PROTOCOL_DIAG_ENABLE
#endif

#if SHIP_PROTOCOL_VIEWER_LOG_ENABLE
#define SHIP_VIEWER_LOG0(tag, msg) log_info((u8 *)(tag), (u8 *)(msg))
#define SHIP_VIEWER_LOGI(tag, fmt, ...) log_info((u8 *)(tag), (u8 *)(fmt), __VA_ARGS__)
#define SHIP_VIEWER_LOGW(tag, fmt, ...) log_warn((u8 *)(tag), (u8 *)(fmt), __VA_ARGS__)
#else
#define SHIP_VIEWER_LOG0(tag, msg)
#define SHIP_VIEWER_LOGI(tag, fmt, ...)
#define SHIP_VIEWER_LOGW(tag, fmt, ...)
#endif

#ifndef SHIP_PROTOCOL_ERROR_LOG_ENABLE
#define SHIP_PROTOCOL_ERROR_LOG_ENABLE SHIP_PROTOCOL_DIAG_ENABLE
#endif

#if !SHIP_PROTOCOL_ERROR_LOG_ENABLE
#undef LOGE
#define LOGE(tag, ...)
#endif

#if SHIP_PROTO_DEBUG_ENABLE
#define SHIP_PROTO_DBG(...) LOGI(SHIP_TAG, __VA_ARGS__)
#else
#define SHIP_PROTO_DBG(...)
#endif

#if SHIP_PROTOCOL_DIAG_ENABLE
#define SHIP_REASON_C(text)  (text)
#define SHIP_REASON_U8(text) ((const u8 *)(text))
#define SHIP_STAGE_U8(text)  ((const u8 *)(text))
#else
#define SHIP_REASON_C(text)  ((const char *)0)
#define SHIP_REASON_U8(text) ((const u8 *)0)
#define SHIP_STAGE_U8(text)  ((const u8 *)0)
#endif

#if defined(SHIP_PAIR_SEED_USE_CHIPID) && (SHIP_PAIR_SEED_USE_CHIPID != 0)
#include "..\..\..\User\STC32G.h"
#endif

#define SHIP_TAG "SHIP"
#define SHIP_DATA_TAG "DATA"
#define SHIP_PAIR_FIX_REV "pairbiz-r4"

#define SHIP_LEGACY_PROTO_MAX_LEN      30U
#define SHIP_AXIS_CENTER               100U
#define SHIP_CRUISE_KEY_START_INPUT    60
#define SHIP_CRUISE_KEY_STOP_INPUT     (-50)
#define SHIP_CRUISE_KEY_SPEED          760
#define SHIP_CRUISE_STEER_START_MAX    8
#define SHIP_CRUISE_GYRO_START_MAX_DPS 8
#define SHIP_POWER_LEVEL_0             0U
#define SHIP_POWER_LEVEL_1             1U
#define SHIP_POWER_LEVEL_2             2U
#define SHIP_POWER_LEVEL_3             3U
#define SHIP_POWER_LEVEL_4             4U
#define SHIP_THROTTLE_RECOVER_MS       3000UL
#ifndef SHIP_MANUAL_BOOT_BLOCK_MS
#define SHIP_MANUAL_BOOT_BLOCK_MS      3000UL
#endif
#ifndef SHIP_MANUAL_BOOT_WAIT_HEADING
#define SHIP_MANUAL_BOOT_WAIT_HEADING  1U
#endif
#ifndef SHIP_RC_INPUT_LOG_ENABLE
#define SHIP_RC_INPUT_LOG_ENABLE       1U
#endif
#ifndef SHIP_GOTO_POINT_UART_LOG_ENABLE
#define SHIP_GOTO_POINT_UART_LOG_ENABLE 1U
#endif
#ifndef SHIP_RC_INPUT_LOG_PERIOD_MS
#define SHIP_RC_INPUT_LOG_PERIOD_MS    100UL
#endif
#define SHIP_LOWPOWER_CHECK_TICKS      600U
#define SHIP_POWER_SAMPLE_DIVIDER      100U
#ifndef SHIP_AUTODRIVE_DIAG_ENABLE
#define SHIP_AUTODRIVE_DIAG_ENABLE     1
#endif
#ifndef SHIP_AUTODRIVE_DIAG_PERIOD_MS
#define SHIP_AUTODRIVE_DIAG_PERIOD_MS  1000UL
#endif
#ifndef SHIP_AUTODRIVE_DIAG_MIN_GAP_MS
#define SHIP_AUTODRIVE_DIAG_MIN_GAP_MS 200UL
#endif
#define SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN 36U

#ifdef BOARD_12V
#define SHIP_BATT_ADC_FULL_RAW         2000U
#define SHIP_BATT_ADC_LEVEL3_RAW       1900U
#define SHIP_BATT_ADC_LEVEL2_RAW       1730U
#define SHIP_BATT_ADC_LEVEL1_RAW       1620U
#else
#define SHIP_BATT_ADC_FULL_RAW         1710U
#define SHIP_BATT_ADC_LEVEL3_RAW       1630U
#define SHIP_BATT_ADC_LEVEL2_RAW       1530U
#define SHIP_BATT_ADC_LEVEL1_RAW       1420U
#endif

#define SHIP_KEY_E_RESERVED            0xA1U
#define SHIP_KEY_A_TOGGLE_LIGHT        0xA3U
#define SHIP_KEY_B_UNUSED              0xA5U
#define SHIP_KEY_C_UNUSED              0xA7U
#define SHIP_KEY_D_UNUSED              0xA9U
#define SHIP_KEY_NULL                  0xA0U

typedef enum
{
    /* Initial delay before the first pairing burst. */
    SHIP_STATE_BOOT_WAIT = 0,
    /* Pair requests are being transmitted on SHIP_PAIR_CHANNEL_DEFAULT. */
    SHIP_STATE_PAIR_SEND,
    /* Work-channel receive mode: parse commands and report ship status. */
    SHIP_STATE_WORK_RX
} ShipState_t;

/* Protocol runtime state.  This is intentionally kept as transport/business
 * state only; closed-loop control details belong to ShipControl/AutoDrive. */
typedef struct
{
    u8 lr;
    u8 ud;
    u8 key;
    u8 last_key;
    u8 valid;
    u8 paired;
    u8 work_rx_configured;
    u8 work_state_logged;
    u8 light_toggle_pending;
    ShipState_t state;
    u8 rf_channel[3];
    u8 rf_send_key[2];
    u16 pair_wait_rsp_time;
    u16 wait_ticks;
    u16 pair_left;
    u16 pair_retry_count;
    u16 work_rx_reopen_ticks;
    u16 work_rx_reopen_total;
    u32 pair_wait_start_ms;
    u32 last_proto_rx_ms;
    u32 last_throttle_rx_ms;
    u8 pair_rsp_timeout_logged;
    u8 rx_idle_warned;
    u8 remote_online;
    u8 throttle_online;
    u8 throttle_recover_done;
    u8 manual_boot_block_logged;
    u8 manual_boot_ready_logged;
    u32 rc_input_last_log_ms;
} ShipRuntime_t;

/* Last known power sample.  report is the compact 0..4 value sent back to
 * the handheld; raw/millivolt fields are retained for diagnostics. */
typedef struct
{
    u16 raw;
    u16 adc_mv;
    u32 bat_mv;
    u8 report;
    u8 valid;
} ShipPowerSample_t;

static ShipRuntime_t xdata g_ship_rt;
static u8 g_ship_power_sample_times = 0U;
static u16 g_lowpower_check_times = 0U;
static u8 g_ship_power_level = SHIP_POWER_LEVEL_0;
static ShipPowerSample_t g_ship_power_sample;
static u8 xdata g_ship_tx_frame[SHIP_PROTO_MAX_FRAME_LEN];
static u8 xdata g_ship_rx_frame[SHIP_PROTO_MAX_FRAME_LEN];
static u8 xdata g_ship_parse_frame[SHIP_LEGACY_PROTO_MAX_LEN];

static s8 ShipProtocol_ApplyWorkSyncIdle(u8 log_rxdbg);
static s8 ShipProtocol_ApplyWorkRx(u8 log_rxdbg);
#if SHIP_PROTOCOL_DIAG_ENABLE
static void ShipProtocol_ReopenWorkRxImpl(const char *reason, u8 log_rxdbg, u8 log_ok);
#define ShipProtocol_ReopenWorkRx(reason, log_rxdbg, log_ok) ShipProtocol_ReopenWorkRxImpl((reason), (log_rxdbg), (log_ok))
#elif SHIP_PROTOCOL_ERROR_LOG_ENABLE
static void ShipProtocol_ReopenWorkRxImpl(const char *reason, u8 log_rxdbg);
#define ShipProtocol_ReopenWorkRx(reason, log_rxdbg, log_ok) ShipProtocol_ReopenWorkRxImpl((reason), (log_rxdbg))
#else
static void ShipProtocol_ReopenWorkRxImpl(u8 log_rxdbg);
#define ShipProtocol_ReopenWorkRx(reason, log_rxdbg, log_ok) ShipProtocol_ReopenWorkRxImpl((log_rxdbg))
#endif
static void ShipProtocol_MarkPairedByFrame(u8 cmd);
static void ShipProtocol_ReadPowerSample(ShipPowerSample_t *sample);
static void ShipProtocol_ServicePowerSample(void);
static u8 ShipProtocol_AdcRawToPowerLevel(u16 adc_raw);
static int16 ShipProtocol_RawUdToInput(u8 front_back);
static int16 ShipProtocol_RawLrToInput(u8 left_right);
static u8 ShipProtocol_IsLowPower(void);
static void ShipProtocol_LowPowerCheck(void);
#if SHIP_PROTOCOL_DIAG_ENABLE
static u8 ShipProtocol_RefreshDefaultRfImpl(const char *stage, u8 log_mismatch);
#define ShipProtocol_RefreshDefaultRf(stage, log_mismatch) ShipProtocol_RefreshDefaultRfImpl((stage), (log_mismatch))
#else
static u8 ShipProtocol_RefreshDefaultRfImpl(void);
#define ShipProtocol_RefreshDefaultRf(stage, log_mismatch) ShipProtocol_RefreshDefaultRfImpl()
#endif
static void ShipProtocol_WritePointLegacy(u8 *dst, const AutoDrive_PointRaw_t *point);
static void ShipProtocol_SendAutoDriveDiagOnce(u8 log_this_tx);
static void ShipProtocol_ServiceAutoDriveDiag(u32 now_ms);
#if SHIP_PROTOCOL_DIAG_ENABLE
static void ShipProtocol_LogAutoDriveSnapshot(const char *stage);
#else
#define ShipProtocol_LogAutoDriveSnapshot(stage)
#endif
#if SHIP_PROTOCOL_DIAG_ENABLE
static const char *ShipProtocol_CmdName(u8 cmd);
#endif
#if SHIP_PROTOCOL_DIAG_ENABLE || SHIP_PROTOCOL_VIEWER_LOG_ENABLE
static void ShipProtocol_LogPowerSample(const ShipPowerSample_t *sample, u8 force_log);
#endif
#if SHIP_PROTOCOL_DIAG_ENABLE
static void ShipProtocol_LogRxDebug(const u8 *stage);
static void ShipProtocol_LogPayloadBrief(const u8 *stage, u8 cmd, const u8 *payload, u8 payload_len);
static void ShipProtocol_LogFrameBrief(const u8 *stage, u8 channel, const u8 *frame, u8 frame_len);
static void ShipProtocol_LogCoordBE(const u8 *buf, u8 len);
#else
#define ShipProtocol_LogRxDebug(stage)
#define ShipProtocol_LogPayloadBrief(stage, cmd, payload, payload_len)
#define ShipProtocol_LogFrameBrief(stage, channel, frame, frame_len)
#define ShipProtocol_LogCoordBE(buf, len)
#endif
#if !(SHIP_PROTOCOL_DIAG_ENABLE || SHIP_PROTOCOL_VIEWER_LOG_ENABLE)
#define ShipProtocol_LogPowerSample(sample, force_log)
#endif
static u8 ShipProtocol_ShouldLogRcInputSample(u32 now_ms);
static u16 ShipProtocol_ReadU16Legacy(const u8 *buf);
static const char *ShipProtocol_FishCmdResultName(u8 result);
static void ShipProtocol_LogGotoPointUart(const u8 *frame,
                                          u8 frame_len,
                                          const u8 *payload,
                                          u8 payload_len,
                                          u8 xor_calc,
                                          u8 xor_recv,
                                          u8 result);

void ShipProtocol_ResetYawHoldController(void)
{
    ShipControl_ResetYawHoldController();
}

static u32 ShipProtocol_ElapsedMs(u32 now_ms, u32 start_ms)
{
    if (start_ms == 0UL) {
        return 0UL;
    }
    if (now_ms < start_ms) {
        return 0UL;
    }
    return (u32)(now_ms - start_ms);
}

/* Compatibility wrapper used by older navigation code.  New code should call
 * ShipControl_RequestGpsNav() directly. */
u8 ShipProtocol_ApplyYawHoldTarget(u16 target_heading_cd, int16 base_speed)
{
    ShipControl_RequestGpsNav(target_heading_cd, base_speed);
    return 1U;
}

/* Convert the legacy stick center value 100 to signed control input. */
static int16 ShipProtocol_RawUdToInput(u8 front_back)
{
    return (int16)((int16)front_back - (int16)SHIP_AXIS_CENTER);
}

static int16 ShipProtocol_RawLrToInput(u8 left_right)
{
    return (int16)((int16)left_right - (int16)SHIP_AXIS_CENTER);
}

/* True when the cached battery level has dropped to the lowest band. */
static u8 ShipProtocol_IsLowPower(void)
{
    return (g_ship_power_level == SHIP_POWER_LEVEL_0) ? 1U : 0U;
}

/* Periodically sample battery level and request AutoDrive return when the
 * boat is idle, not already in AutoDrive, and power stays low long enough. */
static void ShipProtocol_LowPowerCheck(void)
{
    ShipProtocol_ServicePowerSample();
    ShipProtocol_LogPowerSample(&g_ship_power_sample, 0U);
    g_lowpower_check_times++;
    if (g_lowpower_check_times > SHIP_LOWPOWER_CHECK_TICKS) {
        g_lowpower_check_times = 0U;
        if (ShipProtocol_IsLowPower() &&
            (AutoDrive_GetMode() == AUTO_DRIVE_CLOSE) &&
            (ShipControl_GetManualAccelerator() < 10U)) {
            AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_LOW_POWER);
        }
    } else if (ShipProtocol_IsLowPower() == 0U) {
        g_lowpower_check_times = 0U;
    }
}

#if SHIP_PROTOCOL_DIAG_ENABLE
static const char *ShipProtocol_CmdName(u8 cmd)
{
    switch (cmd) {
    case SHIP_CMD_PAIR_RSP:
        return "pair-rsp";
    case SHIP_CMD_PAIR:
        return "pair-req";
    case SHIP_CMD_THROTTLE:
        return "manual-ctrl";
    case SHIP_CMD_GPS_REPORT:
        return "gps-report";
    case SHIP_CMD_AUTODRIVE_DIAG:
        return "autodrive-diag";
    case SHIP_CMD_RETURN_HOME:
        return "return-home";
    case SHIP_CMD_GOTO_POINT:
        return "goto-point";
    case SHIP_CMD_RETURN_SWITCH:
        return "return-switch";
    default:
        return "unknown";
    }
}
#endif

/* Rate-limit high-frequency RC input logs. */
static u8 ShipProtocol_ShouldLogRcInputSample(u32 now_ms)
{
    if ((SHIP_RC_INPUT_LOG_PERIOD_MS == 0U) ||
        ((now_ms - g_ship_rt.rc_input_last_log_ms) >= SHIP_RC_INPUT_LOG_PERIOD_MS)) {
        g_ship_rt.rc_input_last_log_ms = now_ms;
        return 1U;
    }

    return 0U;
}

static const char *ShipProtocol_FishCmdResultName(u8 result)
{
    switch (result) {
    case AUTODRIVE_FISH_CMD_BUSY:
        return "busy";
    case AUTODRIVE_FISH_CMD_STORED:
        return "store";
    case AUTODRIVE_FISH_CMD_DUP_WAIT:
        return "dup-wait";
    case AUTODRIVE_FISH_CMD_REJECT_UNKNOWN:
        return "reject-unknown";
    case AUTODRIVE_FISH_CMD_REJECT_DISTANCE:
        return "reject-distance";
    case AUTODRIVE_FISH_CMD_STARTED:
        return "start";
    case AUTODRIVE_FISH_CMD_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

static void ShipProtocol_LogGotoPointUart(const u8 *frame,
                                          u8 frame_len,
                                          const u8 *payload,
                                          u8 payload_len,
                                          u8 xor_calc,
                                          u8 xor_recv,
                                          u8 result)
{
#if SHIP_GOTO_POINT_UART_LOG_ENABLE
    AutoDrive_PointRaw_t point;
    u8 fish_index;

    if ((payload == 0) || (payload_len < AUTODRIVE_LEGACY_POINT_WIRE_LEN)) {
        log_warn((u8 *)SHIP_DATA_TAG,
                 (u8 *)"0x14 short len=%u xor=%02X/%02X res=%s",
                 (u16)payload_len,
                 (u16)xor_calc,
                 (u16)xor_recv,
                 ShipProtocol_FishCmdResultName(result));
        return;
    }

    point.lon_ew = payload[0];
    point.lon_whole = ShipProtocol_ReadU16Legacy(&payload[1]);
    point.lon_frac = ShipProtocol_ReadU16Legacy(&payload[3]);
    point.lat_ns = payload[5];
    point.lat_whole = ShipProtocol_ReadU16Legacy(&payload[6]);
    point.lat_frac = ShipProtocol_ReadU16Legacy(&payload[8]);
    fish_index = AutoDrive_GetLastFishCommandIndex();

    log_info((u8 *)SHIP_DATA_TAG,
             (u8 *)"0x14 rx fl=%u pl=%u xor=%02X/%02X res=%u(%s) idx=%u",
             (u16)frame_len,
             (u16)payload_len,
             (u16)xor_calc,
             (u16)xor_recv,
             (u16)result,
             ShipProtocol_FishCmdResultName(result),
             (u16)fish_index);
    log_info((u8 *)SHIP_DATA_TAG,
             (u8 *)"0x14 point ew=0x%02X lon=%u.%u ns=0x%02X lat=%u.%u",
             (u16)point.lon_ew,
             point.lon_whole,
             point.lon_frac,
             (u16)point.lat_ns,
             point.lat_whole,
             point.lat_frac);
    log_info((u8 *)SHIP_DATA_TAG,
             (u8 *)"0x14 payload=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             (u16)payload[0],
             (u16)payload[1],
             (u16)payload[2],
             (u16)payload[3],
             (u16)payload[4],
             (u16)payload[5],
             (u16)payload[6],
             (u16)payload[7],
             (u16)payload[8],
             (u16)payload[9]);
    if ((frame != 0) && (frame_len >= 15U)) {
        log_info((u8 *)SHIP_DATA_TAG,
                 (u8 *)"0x14 frame=AA %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X BB",
                 (u16)frame[1],
                 (u16)frame[2],
                 (u16)frame[3],
                 (u16)frame[4],
                 (u16)frame[5],
                 (u16)frame[6],
                 (u16)frame[7],
                 (u16)frame[8],
                 (u16)frame[9],
                 (u16)frame[10],
                 (u16)frame[11],
                 (u16)frame[12],
                 (u16)frame[13]);
    }
#else
    (void)frame;
    (void)frame_len;
    (void)payload;
    (void)payload_len;
    (void)xor_calc;
    (void)xor_recv;
    (void)result;
#endif
}

/* A key is logged as a placeholder because this board version does not bind
 * the lamp output pin at this protocol layer. */
static void ShipProtocol_LogLightPending(void)
{
#if SHIP_PROTOCOL_DIAG_ENABLE
    if (g_ship_rt.light_toggle_pending == 0U) {
        g_ship_rt.light_toggle_pending = 1U;
        LOGW(SHIP_TAG, "key action=A light-unbound");
    } else {
        LOGI(SHIP_TAG, "key action=A light-unbound repeat");
    }
#else
    g_ship_rt.light_toggle_pending = 1U;
#endif
}

/* Decode one key edge.  Repeated key bytes are ignored so holding a button
 * does not retrigger cruise/lamp actions every throttle frame. */
static void ShipProtocol_HandleKey(u8 front_back, u8 key)
{
    u8 cruise_active;
    int16 throttle_input;
    int16 steering_input;
    int16 yaw_rate_dps;
    u16 heading_cd;

    if (key == g_ship_rt.last_key) {
        return;
    }
    g_ship_rt.last_key = key;
    throttle_input = ShipProtocol_RawUdToInput(front_back);
    steering_input = ShipProtocol_RawLrToInput(g_ship_rt.lr);
    yaw_rate_dps = (int16)(MainLoop_GetGyroZDps100() / 100);
    cruise_active =
        (ShipControl_GetMode() == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) ? 1U : 0U;

    switch (key) {
    case SHIP_KEY_A_TOGGLE_LIGHT:
        ShipProtocol_LogLightPending();
        break;
    case SHIP_KEY_B_UNUSED:
        SHIP_VIEWER_LOG0(SHIP_TAG, "key action=B noop");
        break;
    case SHIP_KEY_C_UNUSED:
        SHIP_VIEWER_LOG0(SHIP_TAG, "key action=C noop");
        break;
    case SHIP_KEY_D_UNUSED:
        SHIP_VIEWER_LOG0(SHIP_TAG, "key action=D noop");
        break;
    case SHIP_KEY_E_RESERVED:
        if (cruise_active != 0U) {
            ShipControl_Stop(SHIP_CONTROL_STOP_REASON_CRUISE_KEY);
            log_info((u8 *)SHIP_DATA_TAG,
                     (u8 *)"cruise exit reason=key-toggle input=%d raw_ud=%u",
                     throttle_input,
                     (u16)front_back);
            SHIP_VIEWER_LOG0(SHIP_TAG, "key action=E cruise-toggle-stop");
        } else if ((throttle_input >= SHIP_CRUISE_KEY_START_INPUT) &&
                   (steering_input <= SHIP_CRUISE_STEER_START_MAX) &&
                   (steering_input >= (int16)(-SHIP_CRUISE_STEER_START_MAX)) &&
                   (yaw_rate_dps <= SHIP_CRUISE_GYRO_START_MAX_DPS) &&
                   (yaw_rate_dps >= (int16)(-SHIP_CRUISE_GYRO_START_MAX_DPS))) {
            if (MainLoop_IsHeadingReady() != 0U) {
                heading_cd = MainLoop_GetHeadingDeg100();
                ShipControl_RequestCruise(heading_cd,
                                          SHIP_CRUISE_KEY_SPEED);
                log_info((u8 *)SHIP_DATA_TAG,
                         (u8 *)"cruise enter input=%d raw_ud=%u speed=%d hd=%u start_th=%d",
                         throttle_input,
                         (u16)front_back,
                         (int16)SHIP_CRUISE_KEY_SPEED,
                         heading_cd,
                         (int16)SHIP_CRUISE_KEY_START_INPUT);
            } else {
                ShipControl_Stop(SHIP_CONTROL_STOP_REASON_HEADING_LOST);
                log_info((u8 *)SHIP_DATA_TAG,
                         (u8 *)"cruise reject reason=heading input=%d raw_ud=%u",
                         throttle_input,
                         (u16)front_back);
            }
            SHIP_VIEWER_LOGI(SHIP_TAG,
                             "key action=E cruise-high input=%d raw_ud=%u",
                             throttle_input,
                             (u16)front_back);
        } else if (throttle_input >= SHIP_CRUISE_KEY_START_INPUT) {
            log_info((u8 *)SHIP_DATA_TAG,
                     (u8 *)"cruise ignore reason=not-straight input=%d steer=%d gyro=%d raw_ud=%u raw_lr=%u",
                     throttle_input,
                     steering_input,
                     yaw_rate_dps,
                     (u16)front_back,
                     (u16)g_ship_rt.lr);
            SHIP_VIEWER_LOGI(SHIP_TAG,
                             "key action=E cruise-not-straight input=%d steer=%d",
                             throttle_input,
                             steering_input);
        } else if (throttle_input <= SHIP_CRUISE_KEY_STOP_INPUT) {
            ShipControl_Stop(SHIP_CONTROL_STOP_REASON_CRUISE_KEY);
            log_info((u8 *)SHIP_DATA_TAG,
                     (u8 *)"cruise stop input=%d raw_ud=%u stop_th=%d",
                     throttle_input,
                     (u16)front_back,
                     (int16)SHIP_CRUISE_KEY_STOP_INPUT);
            SHIP_VIEWER_LOGI(SHIP_TAG,
                             "key action=E cruise-stop input=%d raw_ud=%u",
                             throttle_input,
                             (u16)front_back);
        } else {
            log_info((u8 *)SHIP_DATA_TAG,
                     (u8 *)"cruise ignore input=%d raw_ud=%u need=%d",
                     throttle_input,
                     (u16)front_back,
                     (int16)SHIP_CRUISE_KEY_START_INPUT);
            SHIP_VIEWER_LOGI(SHIP_TAG,
                             "key action=E cruise-ignore input=%d raw_ud=%u need=%d",
                             throttle_input,
                             (u16)front_back,
                             (int16)SHIP_CRUISE_KEY_START_INPUT);
        }
        AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
        break;
    case SHIP_KEY_NULL:
    default:
        break;
    }
}

/* Convert ADC count to voltage at the MCU pin. */
static u16 ShipProtocol_AdcRawToMv(u16 adc_raw)
{
    return (u16)(((u32)adc_raw * (u32)SHIP_ADC_REF_MV) / 4095UL);
}

/* Convert divider output voltage back to estimated battery voltage. */
static u32 ShipProtocol_AdcMvToBatteryMv(u16 adc_mv)
{
    if (SHIP_BAT_DIV_DEN == 0UL) {
        return (u32)adc_mv;
    }
    return (((u32)adc_mv * (u32)SHIP_BAT_DIV_NUM) / (u32)SHIP_BAT_DIV_DEN);
}

/* Map raw ADC thresholds to the compact legacy power level 0..4. */
static u8 ShipProtocol_AdcRawToPowerLevel(u16 adc_raw)
{
    if (adc_raw >= SHIP_BATT_ADC_FULL_RAW) {
        return SHIP_POWER_LEVEL_4;
    }
    if (adc_raw >= SHIP_BATT_ADC_LEVEL3_RAW) {
        return SHIP_POWER_LEVEL_3;
    }
    if (adc_raw >= SHIP_BATT_ADC_LEVEL2_RAW) {
        return SHIP_POWER_LEVEL_2;
    }
    if (adc_raw >= SHIP_BATT_ADC_LEVEL1_RAW) {
        return SHIP_POWER_LEVEL_1;
    }
    return SHIP_POWER_LEVEL_0;
}

/* Take one ADC reading.  On invalid ADC results, preserve the last report
 * level so the handheld does not jump to a misleading value. */
static void ShipProtocol_ReadPowerSample(ShipPowerSample_t *sample)
{
    u16 adc_raw;

    if (sample == 0) {
        return;
    }

    sample->raw = g_ship_power_sample.raw;
    sample->adc_mv = g_ship_power_sample.adc_mv;
    sample->bat_mv = g_ship_power_sample.bat_mv;
    sample->report = g_ship_power_level;
    sample->valid = g_ship_power_sample.valid;
    adc_raw = Get_ADCResult(ADC_CH8);
    if (adc_raw > 4095U) {
        sample->raw = adc_raw;
        sample->adc_mv = 0U;
        sample->bat_mv = 0UL;
        sample->report = g_ship_power_level;
        sample->valid = 0U;
        return;
    }

    sample->raw = adc_raw;
    sample->adc_mv = ShipProtocol_AdcRawToMv(adc_raw);
    sample->bat_mv = ShipProtocol_AdcMvToBatteryMv(sample->adc_mv);
    sample->report = ShipProtocol_AdcRawToPowerLevel(adc_raw);
    sample->valid = 1U;
}

/* Downsample battery reads to reduce ADC/log traffic in the 10 ms scheduler. */
static void ShipProtocol_ServicePowerSample(void)
{
    if (g_ship_power_sample_times < SHIP_POWER_SAMPLE_DIVIDER) {
        g_ship_power_sample_times++;
        return;
    }
    g_ship_power_sample_times = 0U;

    ShipProtocol_ReadPowerSample(&g_ship_power_sample);
    if (g_ship_power_sample.valid != 0U) {
        g_ship_power_level = g_ship_power_sample.report;
    }
}

#if SHIP_PROTOCOL_DIAG_ENABLE || SHIP_PROTOCOL_VIEWER_LOG_ENABLE
static void ShipProtocol_LogPowerSample(const ShipPowerSample_t *sample, u8 force_log)
{
#if SHIP_ADC_LOG_ENABLE
    static u32 last_log_ms = 0UL;
    u32 now_ms;

    if (sample == 0) {
        return;
    }
    now_ms = Task_GetTickMs();
    if ((force_log == 0U) &&
        (SHIP_POWER_LOG_PERIOD_MS != 0U) &&
        ((now_ms - last_log_ms) < SHIP_POWER_LOG_PERIOD_MS)) {
        return;
    }
    last_log_ms = now_ms;

    if (sample->valid == 0U) {
        SHIP_VIEWER_LOGW(SHIP_TAG, "adc fail raw=%u p=%u",
                         (u16)sample->raw,
                         (u16)sample->report);
        return;
    }

    SHIP_VIEWER_LOGI(SHIP_TAG, "adc raw=%u mv=%u bat=%lu p=%u",
                     (u16)sample->raw,
                     (u16)sample->adc_mv,
                     (u32)sample->bat_mv,
                     (u16)sample->report);
#else
    (void)force_log;
    (void)sample;
#endif
}
#endif

/* Legacy checksum: XOR all bytes from len through the last payload byte. */
static u8 ShipProtocol_Xor(const u8 *buf, u8 len)
{
    u8 i;
    u8 val;

    val = 0U;
    for (i = 0U; i < len; i++) {
        val ^= buf[i];
    }
    return val;
}

/* Pair seed can be fixed constants or chip ID bytes, depending on build
 * configuration.  The same seed also derives work channel and sync keys. */
static void ShipProtocol_GetPairSeed(u8 *seed)
{
    if (seed == 0) {
        return;
    }

#if defined(SHIP_PAIR_SEED_USE_CHIPID) && (SHIP_PAIR_SEED_USE_CHIPID != 0)
    seed[0] = CHIPID20;
    seed[1] = CHIPID21;
    seed[2] = CHIPID22;
    seed[3] = CHIPID23;
#else
    seed[0] = SHIP_PAIR_SEED0;
    seed[1] = SHIP_PAIR_SEED1;
    seed[2] = SHIP_PAIR_SEED2;
    seed[3] = SHIP_PAIR_SEED3;
#endif
}

static u16 ShipProtocol_ReadU16Legacy(const u8 *buf)
{
    return (u16)(((u16)buf[0] << 8) | buf[1]);
}

/* Convert decimal degrees * 1e7 to the legacy ddmm.mmmm split format. */
static void ShipProtocol_ToLegacyNmeaCoord(u32 abs_deg1e7, u16 *coord1, u16 *coord2)
{
    u32 degrees;
    u32 minutes_scaled1e4;

    degrees = abs_deg1e7 / 10000000UL;
    minutes_scaled1e4 = (((abs_deg1e7 % 10000000UL) * 6UL) + 50UL) / 100UL;
    if (minutes_scaled1e4 >= 600000UL) {
        degrees++;
        minutes_scaled1e4 = 0UL;
    }

    *coord1 = (u16)((degrees * 100UL) + (minutes_scaled1e4 / 10000UL));
    *coord2 = (u16)(minutes_scaled1e4 % 10000UL);
}

/* Legacy protocol stores 16-bit fields in big-endian order. */
static void ShipProtocol_WriteU16Legacy(u8 *dst, u16 value)
{
    dst[0] = (u8)(value >> 8);
    dst[1] = (u8)(value & 0xFFU);
}

/* Legacy 0x12 GPS report is not raw struct bytes.
 * The handheld expects angle/lon1/lon2/lat1/lat2 in big-endian order. */
static void ShipProtocol_WriteU16GpsReportBE(u8 *dst, u16 value)
{
    dst[0] = (u8)(value >> 8);
    dst[1] = (u8)(value & 0xFFU);
}

/* Serialize AutoDrive point in the exact 10-byte layout expected by the
 * handheld: lon dir, lon whole/frac, lat dir, lat whole/frac. */
static void ShipProtocol_WritePointLegacy(u8 *dst, const AutoDrive_PointRaw_t *point)
{
    if ((dst == 0) || (point == 0)) {
        return;
    }

    dst[0] = point->lon_ew;
    ShipProtocol_WriteU16Legacy(&dst[1], point->lon_whole);
    ShipProtocol_WriteU16Legacy(&dst[3], point->lon_frac);
    dst[5] = point->lat_ns;
    ShipProtocol_WriteU16Legacy(&dst[6], point->lat_whole);
    ShipProtocol_WriteU16Legacy(&dst[8], point->lat_frac);
}

#if SHIP_PROTOCOL_DIAG_ENABLE
static void ShipProtocol_LogCoordBE(const u8 *buf, u8 len)
{
#if SHIP_PROTOCOL_DIAG_ENABLE
    if (len < 10U) {
        LOGW(SHIP_TAG, "coord short len=%u", (u16)len);
        return;
    }

    LOGI(SHIP_TAG,
         "coord lonEW=0x%02X lon=%u.%u latNS=0x%02X lat=%u.%u",
         (u16)buf[0],
         ShipProtocol_ReadU16Legacy(&buf[1]),
         ShipProtocol_ReadU16Legacy(&buf[3]),
         (u16)buf[5],
         ShipProtocol_ReadU16Legacy(&buf[6]),
         ShipProtocol_ReadU16Legacy(&buf[8]));
#else
    (void)buf;
    (void)len;
#endif
}
#endif

/* Build one AA-BB business frame and send it on the selected RF channel. */
static s8 ShipProtocol_SendFrame(u8 channel, u8 cmd, const u8 *payload, u8 payload_len, u8 log_frame)
{
    u8 *frame;
    u8 idx;
    u8 body_len;
    u8 i;

    if (payload_len > (SHIP_PROTO_MAX_FRAME_LEN - 5U)) {
        return WIRELESS_ERR_PARAM;
    }

    frame = g_ship_tx_frame;
    body_len = (u8)(2U + payload_len);
    idx = 0U;
    frame[idx++] = SHIP_PROTO_HEAD;
    frame[idx++] = body_len;
    frame[idx++] = cmd;

    for (i = 0U; i < payload_len; i++) {
        frame[idx++] = payload[i];
    }

    frame[idx++] = ShipProtocol_Xor(&frame[1], body_len);
    frame[idx++] = SHIP_PROTO_TAIL;
    if (log_frame != 0U) {
        ShipProtocol_LogFrameBrief(SHIP_STAGE_U8("tx send"), channel, frame, idx);
    }

    return Wireless_SendOnChannel(channel, frame, idx);
}

/* Match the old handheld derivation of work channel and sync register bytes.
 * Changing this formula breaks pairing with existing remotes. */
static void ShipProtocol_CalcDefaultRf(u8 *channel, u8 *key0, u8 *key1)
{
    u8 seed[4];

    ShipProtocol_GetPairSeed(seed);

    *key0 =
        (u8)(((u8)((u8)(seed[0] << 4) >> 4)) + ((u8)(seed[3] >> 2) + (u8)(seed[3] % 0x03U)));
    *key1 =
        (u8)(((u8)((u8)(seed[1] << 4) >> 4)) + ((u8)(seed[2] >> 3) + (u8)(seed[0] % 0x06U)));

    *channel = (u8)(((u8)(((seed[3] + 0x06U) % 0x40U) +
                          ((seed[2] >> 3) * 0x08U) +
                          (((seed[1] | seed[0]) % 0x08U) / 2U))) % 0x40U);
}

#if SHIP_PROTOCOL_DIAG_ENABLE
/* Recalculate and cache derived RF parameters; diagnostics can warn if a
 * previous cached value drifted from the current seed-derived value. */
static u8 ShipProtocol_RefreshDefaultRfImpl(const char *stage, u8 log_mismatch)
#else
/* Recalculate and cache derived RF parameters. */
static u8 ShipProtocol_RefreshDefaultRfImpl(void)
#endif
{
    u8 channel;
    u8 key0;
    u8 key1;

    ShipProtocol_CalcDefaultRf(&channel, &key0, &key1);
#if SHIP_PROTOCOL_DIAG_ENABLE
    if ((log_mismatch != 0U) &&
        ((g_ship_rt.rf_channel[0] != channel) ||
         (g_ship_rt.rf_send_key[0] != key0) ||
         (g_ship_rt.rf_send_key[1] != key1))) {
        LOGW(SHIP_TAG,
             "rf cache mismatch stage=%s cache_ch=%u calc_ch=%u cache_key=%u/%u calc_key=%u/%u",
             stage,
             (u16)g_ship_rt.rf_channel[0],
             (u16)channel,
             (u16)g_ship_rt.rf_send_key[0],
             (u16)g_ship_rt.rf_send_key[1],
             (u16)key0,
             (u16)key1);
    }
#endif

    g_ship_rt.rf_channel[0] = channel;
    g_ship_rt.rf_channel[1] = channel;
    g_ship_rt.rf_channel[2] = (u8)(channel + 0x40U);
    g_ship_rt.rf_send_key[0] = key0;
    g_ship_rt.rf_send_key[1] = key1;

    return channel;
}

/* Seed the runtime RF cache before the radio is configured. */
static void ShipProtocol_ApplyDefaultRf(void)
{
    (void)ShipProtocol_RefreshDefaultRf(SHIP_REASON_C("default"), 0U);
}

/* Program work-channel sync words while keeping the radio idle.  This mirrors
 * the legacy pair response window sequence before RX is enabled. */
static s8 ShipProtocol_ApplyWorkSyncIdle(u8 log_rxdbg)
{
    u16 reg36;
    u16 reg39;
    s8 rc;

    (void)ShipProtocol_RefreshDefaultRf(SHIP_REASON_C("work-sync"), 1U);
    reg36 = (u16)(((u16)g_ship_rt.rf_send_key[0] << 8) | g_ship_rt.rf_send_key[0]);
    reg39 = (u16)(((u16)g_ship_rt.rf_send_key[1] << 8) | g_ship_rt.rf_send_key[1]);

    rc = Wireless_SetSyncRegsIdle(reg36, reg39);
    if (rc != SUCCESS) {
        return rc;
    }

    g_ship_rt.work_rx_configured = 0U;
    g_ship_rt.work_rx_reopen_ticks = 0U;
    if (log_rxdbg != 0U) {
        ShipProtocol_LogRxDebug(SHIP_STAGE_U8("pair-sync-idle"));
    }

    return SUCCESS;
}

/* Put the radio back on the derived work channel in receive mode. */
static s8 ShipProtocol_ApplyWorkRx(u8 log_rxdbg)
{
    s8 rc;
    u8 channel;

    channel = ShipProtocol_RefreshDefaultRf(SHIP_REASON_C("work-rx"), 1U);
    rc = Wireless_SetChannel(channel);
    if (rc == SUCCESS) {
        g_ship_rt.work_rx_configured = 1U;
        g_ship_rt.work_rx_reopen_ticks = 0U;
        if (log_rxdbg != 0U) {
            ShipProtocol_LogRxDebug(SHIP_STAGE_U8("work-rx"));
        }
    }
    return rc;
}

#if SHIP_PROTOCOL_DIAG_ENABLE
static void ShipProtocol_ReopenWorkRxImpl(const char *reason, u8 log_rxdbg, u8 log_ok)
#elif SHIP_PROTOCOL_ERROR_LOG_ENABLE
static void ShipProtocol_ReopenWorkRxImpl(const char *reason, u8 log_rxdbg)
#else
static void ShipProtocol_ReopenWorkRxImpl(u8 log_rxdbg)
#endif
{
    s8 rc;

    rc = ShipProtocol_ApplyWorkRx(log_rxdbg);
    if (rc != SUCCESS) {
#if SHIP_PROTOCOL_ERROR_LOG_ENABLE
        LOGE(SHIP_TAG, "work-rx reopen fail reason=%s rc=%d", reason, rc);
#endif
        return;
    }

#if SHIP_PROTOCOL_DIAG_ENABLE
    if (log_ok != 0U) {
        LOGI(SHIP_TAG, "work-rx reopen reason=%s ch=%u",
             reason,
             (u16)g_ship_rt.rf_channel[0]);
    }
#endif
}

/* Any valid business frame proves that the remote is on the derived work
 * channel.  Use that as a pairing success signal for legacy compatibility. */
static void ShipProtocol_MarkPairedByFrame(u8 cmd)
{
    u32 now_ms;

    now_ms = Task_GetTickMs();
    g_ship_rt.last_proto_rx_ms = now_ms;
    g_ship_rt.rx_idle_warned = 0U;
    g_ship_rt.throttle_recover_done = 0U;
    if (g_ship_rt.remote_online == 0U) {
        g_ship_rt.remote_online = 1U;
        SHIP_VIEWER_LOGI(SHIP_TAG, "rl c=%02X", (u16)cmd);
    }

    if (g_ship_rt.paired != 0U) {
        return;
    }

    g_ship_rt.paired = 1U;
    g_ship_rt.pair_left = 0U;
    g_ship_rt.pair_wait_rsp_time = 0U;
    g_ship_rt.pair_wait_start_ms = 0UL;
    g_ship_rt.pair_rsp_timeout_logged = 0U;
    g_ship_rt.state = SHIP_STATE_WORK_RX;
    g_ship_rt.work_state_logged = 0U;
    SHIP_VIEWER_LOGI(SHIP_TAG,
                     "pair c=%02X ch=%u k=%u/%u",
                     (u16)cmd,
                     (u16)g_ship_rt.rf_channel[0],
                     (u16)g_ship_rt.rf_send_key[0],
                     (u16)g_ship_rt.rf_send_key[1]);
}

#if SHIP_PROTOCOL_DIAG_ENABLE
static void ShipProtocol_LogRxDebug(const u8 *stage)
{
#if SHIP_PROTOCOL_DIAG_ENABLE
    Wireless_RxDebug_t dbg;
    s8 rc;

    rc = Wireless_GetRxDebug(&dbg);
    if (rc != SUCCESS) {
        LOGW(SHIP_TAG, "rxdbg %s read fail rc=%d", stage, rc);
        return;
    }

    LOGI(SHIP_TAG,
         "rxdbg %s ch=%u reg7=0x%04X reg8=0x%04X reg36=0x%04X reg37=0x%04X reg38=0x%04X reg39=0x%04X reg48=0x%04X reg52=0x%04X rssi=%u rxbit=%u mode=%u rxen=%u txen=%u",
         stage,
         (u16)dbg.channel,
         dbg.reg7,
         dbg.reg8,
         dbg.reg36,
         dbg.reg37,
         dbg.reg38,
         dbg.reg39,
         dbg.reg48,
         dbg.reg52,
         (u16)dbg.rssi,
         (u16)dbg.rx_mode_bit,
         (u16)dbg.mode,
         (u16)dbg.rx_en,
         (u16)dbg.tx_en);
#else
    (void)stage;
#endif
}

static void ShipProtocol_LogPayloadBrief(const u8 *stage, u8 cmd, const u8 *payload, u8 payload_len)
{
#if SHIP_PROTOCOL_DIAG_ENABLE
    LOGI(SHIP_TAG,
         "%s cmd=0x%02X(%s) len=%u data=%02X %02X %02X %02X %02X %02X",
         stage,
         (u16)cmd,
         ShipProtocol_CmdName(cmd),
         (u16)payload_len,
         (u16)((payload_len > 0U) ? payload[0] : 0U),
         (u16)((payload_len > 1U) ? payload[1] : 0U),
         (u16)((payload_len > 2U) ? payload[2] : 0U),
         (u16)((payload_len > 3U) ? payload[3] : 0U),
         (u16)((payload_len > 4U) ? payload[4] : 0U),
         (u16)((payload_len > 5U) ? payload[5] : 0U));
#else
    (void)stage;
    (void)cmd;
    (void)payload;
    (void)payload_len;
#endif
}

static void ShipProtocol_LogFrameBrief(const u8 *stage, u8 channel, const u8 *frame, u8 frame_len)
{
#if SHIP_PROTOCOL_DIAG_ENABLE
    if ((frame == 0) || (frame_len < 3U)) {
        return;
    }

    LOGI(SHIP_TAG,
         "%s ch=%u frame_len=%u cmd=0x%02X(%s) raw=%02X %02X %02X %02X %02X %02X %02X %02X",
         stage,
         (u16)channel,
         (u16)frame_len,
         (u16)frame[2],
         ShipProtocol_CmdName(frame[2]),
         (u16)((frame_len > 0U) ? frame[0] : 0U),
         (u16)((frame_len > 1U) ? frame[1] : 0U),
         (u16)((frame_len > 2U) ? frame[2] : 0U),
         (u16)((frame_len > 3U) ? frame[3] : 0U),
         (u16)((frame_len > 4U) ? frame[4] : 0U),
         (u16)((frame_len > 5U) ? frame[5] : 0U),
         (u16)((frame_len > 6U) ? frame[6] : 0U),
         (u16)((frame_len > 7U) ? frame[7] : 0U));
#else
    (void)stage;
    (void)channel;
    (void)frame;
    (void)frame_len;
#endif
}
#endif

/* Send one pair request on the fixed pair channel.  left_after_send is used
 * only for retry/sequence logging and scheduler state updates. */
static s8 ShipProtocol_TryPairSend(u16 left_after_send)
{
    u8 pair_data[4];
    u8 pair_xor;
    s8 rc;

    ShipProtocol_GetPairSeed(pair_data);
    pair_xor = (u8)(0x06U ^ SHIP_CMD_PAIR ^ pair_data[0] ^
                    pair_data[1] ^ pair_data[2] ^ pair_data[3]);

    rc = ShipProtocol_SendFrame(SHIP_PAIR_CHANNEL_DEFAULT, SHIP_CMD_PAIR, pair_data, 4U, 1U);
    if (rc == SUCCESS) {
        LOGI(SHIP_TAG,
             "pair req sent seq=%u/%u retry=%u ch=0x%02X",
             (u16)(SHIP_PAIR_SEND_TIMES - left_after_send),
             (u16)SHIP_PAIR_SEND_TIMES,
             (u16)g_ship_rt.pair_retry_count,
             (u16)SHIP_PAIR_CHANNEL_DEFAULT);
        if (left_after_send == (SHIP_PAIR_SEND_TIMES - 1U)) {
            LOGI(SHIP_TAG,
                 "pair req start retry=%u pair_ch=0x%02X seed=%02X%02X%02X%02X work_rx=%u key=%u/%u",
                 (u16)g_ship_rt.pair_retry_count,
                 (u16)SHIP_PAIR_CHANNEL_DEFAULT,
                 (u16)pair_data[0], (u16)pair_data[1],
                 (u16)pair_data[2], (u16)pair_data[3],
                 (u16)g_ship_rt.rf_channel[0],
                 (u16)g_ship_rt.rf_send_key[0],
                 (u16)g_ship_rt.rf_send_key[1]);
            LOGI(SHIP_TAG,
                 "pair req frame=AA 06 10 %02X %02X %02X %02X %02X BB",
                 (u16)pair_data[0],
                 (u16)pair_data[1],
                 (u16)pair_data[2],
                 (u16)pair_data[3],
                 (u16)pair_xor);
        } else if (left_after_send == 0U) {
            LOGI(SHIP_TAG, "pair req burst done, wait rsp");
        }
    } else {
        LOGE(SHIP_TAG, "pair req tx fail rc=%d", rc);
    }

    return rc;
}

/* After a pair burst, switch to the work sync/channel and keep a short window
 * open for the handheld pair response. */
static s8 ShipProtocol_ArmPairRspWindow(u8 log_rxdbg)
{
    s8 rc;

    rc = ShipProtocol_ApplyWorkSyncIdle(log_rxdbg);
    if (rc != SUCCESS) {
        return rc;
    }

    g_ship_rt.pair_wait_rsp_time = SHIP_PAIR_WAIT_RSP_TICKS;
    g_ship_rt.pair_wait_start_ms = Task_GetTickMs();
    g_ship_rt.last_proto_rx_ms = g_ship_rt.pair_wait_start_ms;
    g_ship_rt.pair_rsp_timeout_logged = 0U;

    rc = ShipProtocol_ApplyWorkRx(log_rxdbg);
    if (rc != SUCCESS) {
        return rc;
    }

    return SUCCESS;
}

/* Send one legacy 0x12 GPS/status report.  The payload remains 15 bytes so
 * old handheld firmware can parse it without a protocol upgrade. */
static void ShipProtocol_SendGpsOnce(u8 log_this_tx)
{
    u8 payload[15];
    ShipPowerSample_t power;
    const GPS_State_t *gps;
    u8 idx;
    u16 angle;
    u32 abs_lon;
    u32 abs_lat;
    u16 lon_coord1;
    u16 lon_coord2;
    u16 lat_coord1;
    u16 lat_coord2;
    u8 sat_report;
    s8 rc;
    char lon_dir;
    char lat_dir;
    char payload_lon_dir;
    char payload_lat_dir;

    gps = GPS_GetState();
    idx = 0U;

    sat_report = (gps->satellites_used_gsa > 0U) ? gps->satellites_used_gsa : gps->satellites_used;
    if (sat_report > 24U) {
        sat_report = 24U;
    }
    payload[idx++] = sat_report;

    angle = (u16)((gps->course_deg_x100 / 100U) % 360U);
    ShipProtocol_WriteU16GpsReportBE(&payload[idx], angle);
    idx += 2U;

    if (gps->legacy_coord_valid != 0U) {
        lon_dir = (char)gps->legacy_lon_dir;
        lat_dir = (char)gps->legacy_lat_dir;
        lon_coord1 = gps->legacy_lon1;
        lon_coord2 = gps->legacy_lon2;
        lat_coord1 = gps->legacy_lat1;
        lat_coord2 = gps->legacy_lat2;
        abs_lon = (gps->lon_deg1e7 < 0) ? (u32)(-gps->lon_deg1e7) : (u32)gps->lon_deg1e7;
        abs_lat = (gps->lat_deg1e7 < 0) ? (u32)(-gps->lat_deg1e7) : (u32)gps->lat_deg1e7;
    } else {
        if (gps->lon_deg1e7 < 0) {
            lon_dir = 'W';
            abs_lon = (u32)(-gps->lon_deg1e7);
        } else {
            lon_dir = 'E';
            abs_lon = (u32)gps->lon_deg1e7;
        }
        ShipProtocol_ToLegacyNmeaCoord(abs_lon, &lon_coord1, &lon_coord2);

        if (gps->lat_deg1e7 < 0) {
            lat_dir = 'S';
            abs_lat = (u32)(-gps->lat_deg1e7);
        } else {
            lat_dir = 'N';
            abs_lat = (u32)gps->lat_deg1e7;
        }
        ShipProtocol_ToLegacyNmeaCoord(abs_lat, &lat_coord1, &lat_coord2);
    }

    /* Match the legacy handheld parser exactly.
     * 0x12 keeps fixed E/W marker bytes even when the real hemisphere differs. */
    payload_lon_dir = 'E';
    payload_lat_dir = 'W';

    payload[idx++] = (u8)payload_lon_dir;
    ShipProtocol_WriteU16GpsReportBE(&payload[idx], lon_coord1);
    idx += 2U;
    ShipProtocol_WriteU16GpsReportBE(&payload[idx], lon_coord2);
    idx += 2U;

    payload[idx++] = (u8)payload_lat_dir;
    ShipProtocol_WriteU16GpsReportBE(&payload[idx], lat_coord1);
    idx += 2U;
    ShipProtocol_WriteU16GpsReportBE(&payload[idx], lat_coord2);
    idx += 2U;

    power = g_ship_power_sample;
    power.report = g_ship_power_level;
    payload[idx++] = g_ship_power_level;
    payload[idx++] = AutoDrive_InActive();

    if (idx != 15U) {
        LOGE(SHIP_TAG, "gps payload len bad=%u", (u16)idx);
        return;
    }

    ShipProtocol_LogPowerSample(&power, 0U);
    if (log_this_tx != 0U) {
        SHIP_VIEWER_LOGI(SHIP_TAG,
                         "gps12 ch=%u len=%u sat=%u ang=%u p=%02X auto=%02X fix=%u lon=%c%lu lat=%c%lu seq=%lu",
                         (u16)g_ship_rt.rf_channel[0],
                         (u16)idx,
                         (u16)payload[0],
                         (u16)(((u16)payload[1] << 8) | payload[2]),
                         (u16)payload[13],
                         (u16)payload[14],
                         (u16)gps->fix_valid,
                         lon_dir,
                         (u32)abs_lon,
                         lat_dir,
                         (u32)abs_lat,
                         (u32)gps->update_sequence);
#if SHIP_PROTOCOL_DIAG_ENABLE
        LOGI(SHIP_TAG,
             "gps state fix=%u legacy=%u sat=%u lon=%c%lu lat=%c%lu angle=%u power=0x%02X seq=%lu",
             (u16)gps->fix_valid,
             (u16)gps->legacy_coord_valid,
             (u16)sat_report,
             lon_dir,
             (u32)abs_lon,
             lat_dir,
             (u32)abs_lat,
             (u16)angle,
             (u16)payload[13],
             (u32)gps->update_sequence);
        SHIP_VIEWER_LOGI(SHIP_TAG,
                         "gps sat source gsa=%u gga=%u report=%u",
                         (u16)gps->satellites_used_gsa,
                         (u16)gps->satellites_used,
                         (u16)sat_report);
        SHIP_VIEWER_LOGI(SHIP_TAG,
                         "gps payload oldfmt ew=%c lon1=%u lon2=%u ns=%c lat1=%u lat2=%u",
                         payload_lon_dir,
                         (u16)lon_coord1,
                         (u16)lon_coord2,
                         payload_lat_dir,
                         (u16)lat_coord1,
                         (u16)lat_coord2);
        LOGI(SHIP_TAG,
             "gps payload bytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             (u16)payload[0],
             (u16)payload[1],
             (u16)payload[2],
             (u16)payload[3],
             (u16)payload[4],
             (u16)payload[5],
             (u16)payload[6],
             (u16)payload[7],
             (u16)payload[8],
             (u16)payload[9],
             (u16)payload[10],
             (u16)payload[11],
             (u16)payload[12],
             (u16)payload[13],
             (u16)payload[14]);
#endif
        ShipProtocol_LogPayloadBrief(SHIP_STAGE_U8("tx frame"), SHIP_CMD_GPS_REPORT, payload, idx);
    }

    rc = ShipProtocol_SendFrame(g_ship_rt.rf_channel[0], SHIP_CMD_GPS_REPORT, payload, idx, log_this_tx);
    if (rc != SUCCESS) {
        LOGE(SHIP_TAG, "gps tx fail rc=%d ch=0x%02X", rc, (u16)g_ship_rt.rf_channel[0]);
    }
    if (g_ship_rt.paired != 0U) {
        ShipProtocol_ReopenWorkRx(SHIP_REASON_C("gps report tx"), 0U, log_this_tx);
    } else {
        g_ship_rt.work_rx_configured = 0U;
    }
}

/* Send a compact AutoDrive diagnostic snapshot to the handheld/viewer. */
static void ShipProtocol_SendAutoDriveDiagOnce(u8 log_this_tx)
{
#if SHIP_AUTODRIVE_DIAG_ENABLE
    AutoDrive_DebugSnapshot_t snapshot;
    u8 payload[SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN];
    u8 idx;
    s8 rc;

    AutoDrive_GetDebugSnapshot(&snapshot);
    idx = 0U;
    payload[idx++] = 0x01U;
    payload[idx++] = snapshot.state;
    payload[idx++] = snapshot.mode;
    payload[idx++] = snapshot.auto_ret_onoff;
    payload[idx++] = snapshot.fail_flag;
    payload[idx++] = snapshot.last_reason;
    payload[idx++] = snapshot.gps_ready;
    payload[idx++] = snapshot.sat_count;
    payload[idx++] = snapshot.can_activate_target;
    payload[idx++] = 0U;
    ShipProtocol_WriteU16Legacy(&payload[idx], snapshot.distance_to_target_m);
    idx += 2U;
    ShipProtocol_WriteU16Legacy(&payload[idx], snapshot.current_heading_deg);
    idx += 2U;
    ShipProtocol_WriteU16Legacy(&payload[idx], snapshot.target_heading_deg);
    idx += 2U;
    ShipProtocol_WritePointLegacy(&payload[idx], &snapshot.current_point);
    idx += AUTODRIVE_LEGACY_POINT_WIRE_LEN;
    ShipProtocol_WritePointLegacy(&payload[idx], &snapshot.target_point);
    idx += AUTODRIVE_LEGACY_POINT_WIRE_LEN;

    if (idx != SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN) {
        LOGE(SHIP_TAG, "diag payload len bad=%u", (u16)idx);
        return;
    }

    if (log_this_tx != 0U) {
        LOGI(SHIP_TAG,
             "tx cmd=0x16 state=%u mode=%u sw=0x%02X reason=%u gps=%u sat=%u dist=%u",
             (u16)snapshot.state,
             (u16)snapshot.mode,
             (u16)snapshot.auto_ret_onoff,
             (u16)snapshot.last_reason,
             (u16)snapshot.gps_ready,
             (u16)snapshot.sat_count,
             (u16)snapshot.distance_to_target_m);
        ShipProtocol_LogPayloadBrief(SHIP_STAGE_U8("tx frame"),
                                     SHIP_CMD_AUTODRIVE_DIAG,
                                     payload,
                                     idx);
    }

    rc = ShipProtocol_SendFrame(g_ship_rt.rf_channel[0],
                                SHIP_CMD_AUTODRIVE_DIAG,
                                payload,
                                idx,
                                log_this_tx);
    if (rc != SUCCESS) {
        LOGE(SHIP_TAG, "diag tx fail rc=%d ch=0x%02X", rc, (u16)g_ship_rt.rf_channel[0]);
    }
    if (g_ship_rt.paired != 0U) {
        ShipProtocol_ReopenWorkRx(SHIP_REASON_C("autodrive diag tx"), 0U, 0U);
    } else {
        g_ship_rt.work_rx_configured = 0U;
    }
#else
    (void)log_this_tx;
#endif
}

/* Emit AutoDrive diagnostics on state changes, and periodically while a
 * tracked condition is active. */
static void ShipProtocol_ServiceAutoDriveDiag(u32 now_ms)
{
#if SHIP_AUTODRIVE_DIAG_ENABLE
    static u32 last_tx_ms = 0UL;
    static u8 last_state = 0xFFU;
    static u8 last_mode = 0xFFU;
    static u8 last_reason = 0xFFU;
    static u8 last_busy = 0xFFU;
    static u8 last_switch = 0xFFU;
    static u8 last_fail = 0xFFU;
    AutoDrive_DebugSnapshot_t snapshot;
    u8 busy;
    u8 tracked;
    u8 changed;

    if (g_ship_rt.paired == 0U) {
        return;
    }

    AutoDrive_GetDebugSnapshot(&snapshot);
    busy = (snapshot.state != AUTO_DRIVE_IDLE) ? 1U : 0U;
    tracked = (busy != 0U) ||
              (snapshot.auto_ret_onoff != 0x30U) ||
              (snapshot.fail_flag != 0U);
    changed = (snapshot.state != last_state) ||
              (snapshot.mode != last_mode) ||
              (snapshot.last_reason != last_reason) ||
              (busy != last_busy) ||
              (snapshot.auto_ret_onoff != last_switch) ||
              (snapshot.fail_flag != last_fail);

    if ((tracked == 0U) && (changed == 0U) && (last_tx_ms == 0UL)) {
        return;
    }

    if ((last_tx_ms == 0UL) ||
        ((changed != 0U) &&
         (ShipProtocol_ElapsedMs(now_ms, last_tx_ms) >= SHIP_AUTODRIVE_DIAG_MIN_GAP_MS)) ||
        (ShipProtocol_ElapsedMs(now_ms, last_tx_ms) >= SHIP_AUTODRIVE_DIAG_PERIOD_MS)) {
        ShipProtocol_SendAutoDriveDiagOnce((changed != 0U) ? 1U : 0U);
        last_tx_ms = now_ms;
        last_state = snapshot.state;
        last_mode = snapshot.mode;
        last_reason = snapshot.last_reason;
        last_busy = busy;
        last_switch = snapshot.auto_ret_onoff;
        last_fail = snapshot.fail_flag;
    }
#else
    (void)now_ms;
#endif
}

#if SHIP_PROTOCOL_DIAG_ENABLE
static void ShipProtocol_LogAutoDriveSnapshot(const char *stage)
{
    AutoDrive_DebugSnapshot_t snapshot;

    AutoDrive_GetDebugSnapshot(&snapshot);
    LOGI(SHIP_TAG,
         "ad %s st=%u md=%u sw=0x%02X fail=%u rsn=%u gps=%u sat=%u can=%u dist=%u",
         stage,
         (u16)snapshot.state,
         (u16)snapshot.mode,
         (u16)snapshot.auto_ret_onoff,
         (u16)snapshot.fail_flag,
         (u16)snapshot.last_reason,
         (u16)snapshot.gps_ready,
         (u16)snapshot.sat_count,
         (u16)snapshot.can_activate_target,
         (u16)snapshot.distance_to_target_m);
    LOGI(SHIP_TAG,
         "ad cur=%c%u.%u/%c%u.%u",
         (char)snapshot.current_point.lon_ew,
         snapshot.current_point.lon_whole,
         snapshot.current_point.lon_frac,
         (char)snapshot.current_point.lat_ns,
         snapshot.current_point.lat_whole,
         snapshot.current_point.lat_frac);
    LOGI(SHIP_TAG,
         "ad tgt=%c%u.%u/%c%u.%u",
         (char)snapshot.target_point.lon_ew,
         snapshot.target_point.lon_whole,
         snapshot.target_point.lon_frac,
         (char)snapshot.target_point.lat_ns,
         snapshot.target_point.lat_whole,
         snapshot.target_point.lat_frac);
}
#endif

/* Accept pair response only while the response window is open. */
static void ShipProtocol_HandlePairRsp(const u8 *payload, u8 payload_len)
{
    if (g_ship_rt.pair_wait_rsp_time == 0U) {
        return;
    }

    ShipProtocol_LogPayloadBrief(SHIP_STAGE_U8("pair rsp rx"), SHIP_CMD_PAIR_RSP, payload, payload_len);
    g_ship_rt.pair_wait_rsp_time = 0U;
    g_ship_rt.pair_wait_start_ms = 0UL;
    g_ship_rt.last_proto_rx_ms = Task_GetTickMs();
    g_ship_rt.paired = 1U;
    g_ship_rt.pair_left = 0U;
    g_ship_rt.wait_ticks = 0U;
    g_ship_rt.state = SHIP_STATE_WORK_RX;
    g_ship_rt.work_rx_configured = 1U;
    g_ship_rt.work_state_logged = 0U;
    LOGI(SHIP_TAG, "pair ok, enter work channel rx_ch=%u tx_ch=%u",
         (u16)g_ship_rt.rf_channel[0],
         (u16)g_ship_rt.rf_channel[0]);

    if ((payload != 0) && (payload_len == 4U)) {
        LOGI(SHIP_TAG,
             "pair success paired=1 work_rx=%u work_tx=%u key=%u/%u rsp=%02X%02X%02X%02X",
             (u16)g_ship_rt.rf_channel[0],
             (u16)g_ship_rt.rf_channel[0],
             (u16)g_ship_rt.rf_send_key[0],
             (u16)g_ship_rt.rf_send_key[1],
             (u16)payload[0], (u16)payload[1],
             (u16)payload[2], (u16)payload[3]);
    } else {
        LOGI(SHIP_TAG,
             "pair success paired=1 work_rx=%u work_tx=%u key=%u/%u rsp_len=%u",
             (u16)g_ship_rt.rf_channel[0],
             (u16)g_ship_rt.rf_channel[0],
             (u16)g_ship_rt.rf_send_key[0],
             (u16)g_ship_rt.rf_send_key[1],
             (u16)payload_len);
    }
}

/* Handle 0x11 manual control frames.  AutoDrive busy state consumes link
 * keepalive and key edges but blocks direct manual motor updates. */
static u8 ShipProtocol_HandleThrottle(const u8 *payload, u8 payload_len)
{
    u32 now_ms;
    u8 log_this_sample;

    if (payload_len < 3U) {
        LOGW(SHIP_TAG, "throttle short len=%u", (u16)payload_len);
        return 1U;
    }

    g_ship_rt.lr = payload[0];
    g_ship_rt.ud = payload[1];
    g_ship_rt.key = payload[2];
    g_ship_rt.valid = 1U;
    now_ms = Task_GetTickMs();
    g_ship_rt.last_throttle_rx_ms = now_ms;
    g_ship_rt.throttle_recover_done = 0U;
    log_this_sample = ShipProtocol_ShouldLogRcInputSample(now_ms);
    if (g_ship_rt.throttle_online == 0U) {
        g_ship_rt.throttle_online = 1U;
        SHIP_VIEWER_LOG0(SHIP_TAG, "rc11 on");
    }

#if SHIP_RC_INPUT_LOG_ENABLE
    if (log_this_sample != 0U) {
        SHIP_VIEWER_LOGI(SHIP_TAG,
                         "rc11 u=%u l=%u tv=%d sv=%d k=%02X",
                         (u16)g_ship_rt.ud,
                         (u16)g_ship_rt.lr,
                         (int16)g_ship_rt.ud - (int16)SHIP_AXIS_CENTER,
                         (int16)g_ship_rt.lr - (int16)SHIP_AXIS_CENTER,
                         (u16)g_ship_rt.key);
    }
#endif
    if ((now_ms < SHIP_MANUAL_BOOT_BLOCK_MS) ||
#if SHIP_MANUAL_BOOT_WAIT_HEADING
        (MainLoop_IsHeadingReady() == 0U)
#else
        0U
#endif
        ) {
        if (g_ship_rt.manual_boot_block_logged == 0U) {
            g_ship_rt.manual_boot_block_logged = 1U;
            SHIP_VIEWER_LOGI(SHIP_TAG,
                             "manual boot block wait=%lums hd=%u",
                             (now_ms < SHIP_MANUAL_BOOT_BLOCK_MS) ?
                             (u32)(SHIP_MANUAL_BOOT_BLOCK_MS - now_ms) :
                             0UL,
                             (u16)MainLoop_IsHeadingReady());
        }
        AutoDrive_LinkAliveKick();
        return log_this_sample;
    }
    if (g_ship_rt.manual_boot_ready_logged == 0U) {
        g_ship_rt.manual_boot_ready_logged = 1U;
        SHIP_VIEWER_LOGI(SHIP_TAG,
                         "manual boot ready t=%lums hd=%u",
                         now_ms,
                         (u16)MainLoop_IsHeadingReady());
    }

    if (AutoDrive_IsBusy() != 0U) {
        ShipProtocol_HandleKey(g_ship_rt.ud, g_ship_rt.key);
        AutoDrive_LinkAliveKick();
        return log_this_sample;
    }
    if ((ShipControl_GetMode() == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) &&
        (ShipProtocol_RawUdToInput(g_ship_rt.ud) <= SHIP_CRUISE_KEY_STOP_INPUT)) {
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_CRUISE_KEY);
        log_info((u8 *)SHIP_DATA_TAG,
                 (u8 *)"cruise exit reason=throttle input=%d raw_ud=%u stop_th=%d",
                 ShipProtocol_RawUdToInput(g_ship_rt.ud),
                 (u16)g_ship_rt.ud,
                 (int16)SHIP_CRUISE_KEY_STOP_INPUT);
        SHIP_VIEWER_LOGI(SHIP_TAG,
                         "key action=E cruise-reverse-stop input=%d raw_ud=%u",
                         ShipProtocol_RawUdToInput(g_ship_rt.ud),
                         (u16)g_ship_rt.ud);
        AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
        AutoDrive_LinkAliveKick();
        return log_this_sample;
    }
    ShipProtocol_HandleKey(g_ship_rt.ud, g_ship_rt.key);
    ShipControl_UpdateManualInput(g_ship_rt.lr, g_ship_rt.ud, g_ship_rt.key, now_ms);
    AutoDrive_LinkAliveKick();
    return log_this_sample;
}

/* Route a verified protocol frame to its business handler.  A GPS/status
 * reply is sent after every accepted command to match legacy behavior. */
static void ShipProtocol_Dispatch(u8 cmd,
                                  const u8 *payload,
                                  u8 payload_len,
                                  const u8 *frame,
                                  u8 frame_len,
                                  u8 xor_calc,
                                  u8 xor_recv)
{
    u8 log_gps_after_rsp;
    u8 fish_result;

    log_gps_after_rsp = 1U;
    fish_result = AUTODRIVE_FISH_CMD_INVALID;
    if (cmd != SHIP_CMD_THROTTLE) {
        LOGI(SHIP_TAG, "dispatch cmd=0x%02X(%s) payload_len=%u",
             (u16)cmd,
             ShipProtocol_CmdName(cmd),
             (u16)payload_len);
    }
    switch (cmd) {
    case SHIP_CMD_PAIR_RSP:
        ShipProtocol_HandlePairRsp(payload, payload_len);
        break;
    case SHIP_CMD_PAIR:
        break;
    case SHIP_CMD_THROTTLE:
        log_gps_after_rsp = ShipProtocol_HandleThrottle(payload, payload_len);
        break;
    case SHIP_CMD_GPS_REPORT:
        break;
    case SHIP_CMD_RETURN_HOME:
        if (payload_len < AUTODRIVE_LEGACY_POINT_WIRE_LEN) {
            break;
        }
        LOGI(SHIP_TAG, "cmd=0x13 return-home rx len=%u", (u16)payload_len);
        ShipProtocol_LogCoordBE(payload, payload_len);
        AutoDrive_SetReturnPositionRaw(payload);
        ShipProtocol_LogAutoDriveSnapshot("after-0x13");
        break;
    case SHIP_CMD_GOTO_POINT:
        if (payload_len < AUTODRIVE_LEGACY_POINT_WIRE_LEN) {
            break;
        }
        LOGI(SHIP_TAG, "cmd=0x14 goto-point rx len=%u", (u16)payload_len);
        ShipProtocol_LogCoordBE(payload, payload_len);
        fish_result = AutoDrive_SetFishPositionRaw(payload);
        ShipProtocol_LogGotoPointUart(frame,
                                      frame_len,
                                      payload,
                                      payload_len,
                                      xor_calc,
                                      xor_recv,
                                      fish_result);
        ShipProtocol_LogAutoDriveSnapshot("after-0x14");
        break;
    case SHIP_CMD_RETURN_SWITCH:
        if (payload_len < 1U) {
            LOGW(SHIP_TAG, "return-switch short len=%u", (u16)payload_len);
            break;
        }
        LOGI(SHIP_TAG, "cmd=0x15 return-switch rx len=%u state=%u",
             (u16)payload_len,
             (u16)payload[0]);
        if (payload_len >= (u8)(1U + AUTODRIVE_LEGACY_POINT_WIRE_LEN)) {
            ShipProtocol_LogCoordBE(&payload[1], (u8)(payload_len - 1U));
        }
        AutoDrive_SetSwitchRaw(payload, payload_len);
        ShipProtocol_LogAutoDriveSnapshot("after-0x15");
        break;
    default:
        LOGW(SHIP_TAG, "unknown cmd=0x%02X len=%u", (u16)cmd, (u16)payload_len);
        break;
    }

    ShipProtocol_SendGpsOnce(log_gps_after_rsp);
}

/* Validate a complete AA-BB frame, update link state, and dispatch payload. */
s8 ShipProtocol_ParseFrame(const u8 *frame, u8 frame_len)
{
    u8 body_len;
    u8 cmd;
    u8 data_len;
    u8 xor_calc;
    u8 xor_recv;

    if ((frame == 0) || (frame_len < 5U)) {
        return WIRELESS_ERR_PARAM;
    }
    if ((frame[0] != SHIP_PROTO_HEAD) || (frame[frame_len - 1U] != SHIP_PROTO_TAIL)) {
        LOGW(SHIP_TAG, "bad frame edge h=0x%02X t=0x%02X len=%u",
             (u16)frame[0], (u16)frame[frame_len - 1U], (u16)frame_len);
        return WIRELESS_ERR_VERIFY;
    }

    body_len = frame[1];
    if ((body_len < 2U) || ((u8)(body_len + 3U) != frame_len)) {
        LOGW(SHIP_TAG, "bad len field=%u frame=%u", (u16)body_len, (u16)frame_len);
        return WIRELESS_ERR_VERIFY;
    }

    xor_recv = frame[frame_len - 2U];
    xor_calc = ShipProtocol_Xor(&frame[1], body_len);
    if (xor_recv != xor_calc) {
        LOGW(SHIP_TAG,
             "aa-bb xor bad cmd=0x%02X len=%u calc=0x%02X recv=0x%02X",
             (u16)frame[2],
             (u16)body_len,
             (u16)xor_calc,
             (u16)xor_recv);
        return WIRELESS_ERR_VERIFY;
    }

    cmd = frame[2];
    data_len = (u8)(body_len - 2U);
    SHIP_PROTO_DBG("frame ok cmd=0x%02X len=%u data_len=%u xor=0x%02X",
                   (u16)cmd,
                   (u16)frame_len,
                   (u16)data_len,
                   (u16)xor_recv);
    ShipProtocol_MarkPairedByFrame(cmd);
    if (cmd != SHIP_CMD_THROTTLE) {
        ShipProtocol_LogPayloadBrief(SHIP_STAGE_U8("rx frame ok"), cmd, &frame[3], data_len);
    }
    ShipProtocol_Dispatch(cmd,
                          &frame[3],
                          data_len,
                          frame,
                          frame_len,
                          xor_calc,
                          xor_recv);
    return SUCCESS;
}

/* Rebuild protocol frames from RF payload bytes.  Wireless_Receive() may
 * deliver raw chunks rather than exactly one business frame, so this keeps
 * the legacy byte-by-byte frame finder. */
static void ShipProtocol_ReceiveHandle(const u8 *rx_buf, u8 len)
{
    u8 i;
    u8 *frame;
    u8 frame_index;
    u8 frame_left;
    u8 frame_finish;
    u8 check_ok;
    u8 check_sum;

    if (rx_buf == 0) {
        return;
    }

    frame = g_ship_parse_frame;
    SHIP_PROTO_DBG("rx raw len=%u b0=%02X b1=%02X b2=%02X b3=%02X b4=%02X b5=%02X b6=%02X b7=%02X",
                   (u16)len,
                   (u16)((len > 0U) ? rx_buf[0] : 0U),
                   (u16)((len > 1U) ? rx_buf[1] : 0U),
                   (u16)((len > 2U) ? rx_buf[2] : 0U),
                   (u16)((len > 3U) ? rx_buf[3] : 0U),
                   (u16)((len > 4U) ? rx_buf[4] : 0U),
                   (u16)((len > 5U) ? rx_buf[5] : 0U),
                   (u16)((len > 6U) ? rx_buf[6] : 0U),
                   (u16)((len > 7U) ? rx_buf[7] : 0U));

    /* Keep the legacy truncation behavior for oversized RF payloads. */
    if (len > SHIP_LEGACY_PROTO_MAX_LEN) {
        len = 10U;
    }

    frame_index = 0U;
    frame_left = 0U;
    frame_finish = 0U;
    for (i = 0U; i < len; i++) {
        check_ok = 1U;
        /* frame_index 0 waits for head, 1 reads len, later bytes count down
         * until tail/checksum bytes are available. */
        switch (frame_index) {
        case 0U:
            if (rx_buf[i] != SHIP_PROTO_HEAD) {
                frame_index = 0U;
                frame_left = 0U;
                check_ok = 0U;
            }
            break;
        case 1U:
            frame_left = (u8)(rx_buf[i] + 1U);
            if (frame_left > (SHIP_LEGACY_PROTO_MAX_LEN - 2U)) {
                frame_index = 0U;
                frame_left = 0U;
                check_ok = 0U;
            }
            break;
        default:
            if (frame_left > 0U) {
                frame_left--;
                if (frame_left == 0U) {
                    frame_finish = 1U;
                }
            }
            break;
        }

        if (check_ok == 0U) {
            continue;
        }

        if (frame_index < SHIP_LEGACY_PROTO_MAX_LEN) {
            frame[frame_index++] = rx_buf[i];
        } else {
            frame_index = 0U;
            frame_left = 0U;
            frame_finish = 0U;
            continue;
        }

        if (frame_finish != 0U) {
            frame_finish = 0U;
            SHIP_PROTO_DBG("frame build len=%u head=0x%02X tail=0x%02X lenfield=%u",
                           (u16)frame_index,
                           (u16)frame[0],
                           (u16)frame[frame_index - 1U],
                           (u16)frame[1]);
            check_sum = ShipProtocol_Xor(&frame[1], (u8)(frame_index - 3U));
            if ((check_sum == frame[frame_index - 2U]) &&
                (frame[frame_index - 1U] == SHIP_PROTO_TAIL)) {
                (void)ShipProtocol_ParseFrame(frame, frame_index);
            } else {
                SHIP_PROTO_DBG("frame drop len=%u calc=0x%02X recv=0x%02X tail=0x%02X",
                               (u16)frame_index,
                               (u16)check_sum,
                               (u16)frame[frame_index - 2U],
                               (u16)frame[frame_index - 1U]);
            }
            frame_index = 0U;
            frame_left = 0U;
        }
    }
}

/* Compatibility poll-only entry.  Normal firmware should use
 * ShipProtocol_RunScheduler() so pairing, timeout, and reports also run. */
void ShipProtocol_Poll(void)
{
    u8 *frame;
    u8 frame_len;
    s8 rc;

    frame = g_ship_rx_frame;
    do {
        frame_len = 0U;
        rc = Wireless_Receive(frame, SHIP_PROTO_MAX_FRAME_LEN, &frame_len);
        if (rc == SUCCESS) {
            SHIP_PROTO_DBG("rx pop len=%u", (u16)frame_len);
            ShipProtocol_ReceiveHandle(frame, frame_len);
        }
    } while (rc == SUCCESS);
}

/* Drain the wireless receive queue before each scheduler step. */
static void ShipProtocol_PollRxFrames(void)
{
    u8 *frame;
    u8 frame_len;
    s8 rc;

    frame = g_ship_rx_frame;
    do {
        frame_len = 0U;
        rc = Wireless_Receive(frame, SHIP_PROTO_MAX_FRAME_LEN, &frame_len);
        if (rc == SUCCESS) {
            ShipProtocol_ReceiveHandle(frame, frame_len);
        }
    } while (rc == SUCCESS);
}

/* Initialize local protocol state and the two downstream control modules. */
static void ShipProtocol_InitRuntime(void)
{
    u8 seed[4];

    ShipProtocol_ApplyDefaultRf();
    ShipProtocol_GetPairSeed(seed);

    g_ship_rt.lr = SHIP_AXIS_CENTER;
    g_ship_rt.ud = SHIP_AXIS_CENTER;
    g_ship_rt.key = SHIP_KEY_NULL;
    g_ship_rt.last_key = SHIP_KEY_NULL;
    g_ship_rt.valid = 0U;
    g_ship_rt.paired = 0U;
    g_ship_rt.work_rx_configured = 0U;
    g_ship_rt.work_state_logged = 0U;
    g_ship_rt.light_toggle_pending = 0U;
    g_ship_rt.state = SHIP_STATE_BOOT_WAIT;
    g_ship_rt.pair_wait_rsp_time = 0U;
    g_ship_rt.wait_ticks = SHIP_WAIT_TICKS_DEFAULT;
    g_ship_rt.pair_left = SHIP_PAIR_SEND_TIMES;
    g_ship_rt.pair_retry_count = 0U;
    g_ship_rt.work_rx_reopen_ticks = 0U;
    g_ship_rt.work_rx_reopen_total = 0U;
    g_ship_rt.pair_wait_start_ms = 0UL;
    g_ship_rt.last_proto_rx_ms = 0UL;
    g_ship_rt.last_throttle_rx_ms = 0UL;
    g_ship_rt.pair_rsp_timeout_logged = 0U;
    g_ship_rt.rx_idle_warned = 0U;
    g_ship_rt.remote_online = 0U;
    g_ship_rt.throttle_online = 0U;
    g_ship_rt.throttle_recover_done = 0U;
    g_ship_rt.manual_boot_block_logged = 0U;
    g_ship_rt.manual_boot_ready_logged = 0U;
    g_ship_rt.rc_input_last_log_ms = 0UL;
    g_ship_power_sample_times = 0U;
    g_lowpower_check_times = 0U;
    ShipControl_Init();
    AutoDrive_Init();

    LOGI(SHIP_TAG,
         "scheduler init rev=%s wait=%u pair_send=%u pair_ch=0x%02X seed=%02X%02X%02X%02X",
         SHIP_PAIR_FIX_REV,
         (u16)g_ship_rt.wait_ticks,
         (u16)g_ship_rt.pair_left,
         (u16)SHIP_PAIR_CHANNEL_DEFAULT,
         (u16)seed[0], (u16)seed[1], (u16)seed[2], (u16)seed[3]);
}

/* Scheduler state: send one pair request when the wait counter expires, then
 * arm the response window after the configured burst count. */
static void ShipProtocol_StepPairSend(void)
{
    s8 rc;
    u16 left_after_send;

    if (g_ship_rt.paired != 0U) {
        g_ship_rt.pair_left = 0U;
        g_ship_rt.state = SHIP_STATE_WORK_RX;
        return;
    }

    if (g_ship_rt.pair_left > 0U) {
        g_ship_rt.wait_ticks = SHIP_WAIT_TICKS_DEFAULT;
        left_after_send = (u16)(g_ship_rt.pair_left - 1U);
        rc = ShipProtocol_TryPairSend(left_after_send);
        if (rc != SUCCESS) {
            return;
        }
        g_ship_rt.pair_left = left_after_send;

        if (g_ship_rt.pair_left == 0U) {
            rc = ShipProtocol_ArmPairRspWindow(1U);
            if (rc != SUCCESS) {
                LOGE(SHIP_TAG, "pair rsp window arm fail rc=%d", rc);
                g_ship_rt.pair_retry_count++;
                g_ship_rt.pair_left = SHIP_PAIR_SEND_TIMES;
                g_ship_rt.wait_ticks = SHIP_WAIT_TICKS_DEFAULT;
                g_ship_rt.pair_wait_rsp_time = 0U;
                g_ship_rt.pair_wait_start_ms = 0UL;
                return;
            }
            g_ship_rt.state = SHIP_STATE_WORK_RX;
            LOGI(SHIP_TAG, "pair req burst done, enter rsp wait on work-rx");
        } else {
            LOGI(SHIP_TAG, "pair req sent seq_left=%u wait=%u",
                 (u16)g_ship_rt.pair_left,
                 (u16)g_ship_rt.wait_ticks);
        }
    }
}

/* Scheduler state: keep the radio in work-channel RX and periodically reopen
 * receive mode to recover from radio state drift. */
static void ShipProtocol_StepWorkRx(void)
{
    s8 rc;

    if (g_ship_rt.work_rx_configured == 0U) {
        rc = ShipProtocol_ApplyWorkRx(1U);
        if (rc != SUCCESS) {
            LOGE(SHIP_TAG, "enter work rx fail rc=%d", rc);
            return;
        }
        if (g_ship_rt.last_proto_rx_ms == 0UL) {
            g_ship_rt.last_proto_rx_ms = Task_GetTickMs();
        }
    }

    if (g_ship_rt.work_state_logged == 0U) {
        g_ship_rt.work_state_logged = 1U;
        LOGI(SHIP_TAG, "enter work-state rx_ch=%u tx_ch=%u",
             (u16)g_ship_rt.rf_channel[0],
             (u16)g_ship_rt.rf_channel[0]);
    } else if (g_ship_rt.work_rx_configured == 0U) {
        rc = ShipProtocol_ApplyWorkRx(1U);
        if (rc != SUCCESS) {
            LOGE(SHIP_TAG, "restore work rx fail rc=%d", rc);
        }
    } else {
        g_ship_rt.work_rx_reopen_ticks++;
        if (g_ship_rt.work_rx_reopen_ticks > SHIP_WORK_RX_REOPEN_TICKS) {
            rc = ShipProtocol_ApplyWorkRx(0U);
            if (rc != SUCCESS) {
                LOGE(SHIP_TAG, "periodic work rx reopen fail rc=%d", rc);
            } else {
                g_ship_rt.work_rx_reopen_total++;
                if ((g_ship_rt.work_rx_reopen_total <= 3U) ||
                    ((g_ship_rt.work_rx_reopen_total % 50U) == 0U)) {
                    LOGI(SHIP_TAG, "work-rx reopen cnt=%u ch=%u",
                         (u16)g_ship_rt.work_rx_reopen_total,
                         (u16)g_ship_rt.rf_channel[0]);
                }
            }
        }
    }
}

/* Main 10 ms protocol scheduler.  It drains RX, maintains pairing/RX state,
 * checks link/battery timeouts, and ticks AutoDrive/ShipControl. */
void ShipProtocol_RunScheduler(void)
{
    static u8 initialized = 0U;
    static u32 last_tick_ms = 0U;
    u32 now_ms;

    if (!initialized) {
        ShipProtocol_InitRuntime();
        initialized = 1U;
        last_tick_ms = Task_GetTickMs();
    }

    now_ms = Task_GetTickMs();

    if ((now_ms - last_tick_ms) < 10U) {
        ShipProtocol_PollRxFrames();
        ShipControl_Tick(now_ms);
        return;
    }
    last_tick_ms += 10U;

    ShipProtocol_PollRxFrames();
    AutoDrive_LinkAliveTick();
    ShipProtocol_LowPowerCheck();

    if (g_ship_rt.pair_wait_rsp_time > 0U) {
        g_ship_rt.pair_wait_rsp_time--;
    } else if ((g_ship_rt.pair_wait_start_ms != 0UL) &&
               (g_ship_rt.pair_rsp_timeout_logged == 0U) &&
               (ShipProtocol_ElapsedMs(now_ms, g_ship_rt.pair_wait_start_ms) >= SHIP_PAIR_RSP_EXPIRE_LOG_MS) &&
               (g_ship_rt.paired == 0U)) {
        g_ship_rt.pair_rsp_timeout_logged = 1U;
        LOGW(SHIP_TAG, "pair rsp window expired, no rsp");
        ShipProtocol_LogRxDebug(SHIP_STAGE_U8("pair-rsp-expired"));
    }

    if (g_ship_rt.wait_ticks > 0U) {
        g_ship_rt.wait_ticks--;
    } else {
        if (g_ship_rt.state == SHIP_STATE_BOOT_WAIT) {
            g_ship_rt.state = SHIP_STATE_PAIR_SEND;
        }

        switch (g_ship_rt.state) {
        case SHIP_STATE_PAIR_SEND:
            ShipProtocol_StepPairSend();
            break;
        case SHIP_STATE_WORK_RX:
            ShipProtocol_StepWorkRx();
            break;
        default:
            g_ship_rt.state = SHIP_STATE_PAIR_SEND;
            break;
        }
    }

    now_ms = Task_GetTickMs();
    {
        u32 rx_silence_ms;

        rx_silence_ms = ShipProtocol_ElapsedMs(now_ms, g_ship_rt.last_proto_rx_ms);

        if (g_ship_rt.state == SHIP_STATE_WORK_RX) {
            if ((g_ship_rt.last_proto_rx_ms != 0UL) &&
                (g_ship_rt.rx_idle_warned == 0U) &&
                (rx_silence_ms >= SHIP_RX_IDLE_WARN_MS)) {
                g_ship_rt.rx_idle_warned = 1U;
                LOGW(SHIP_TAG, "work-rx idle %lums, no aa-bb frame", rx_silence_ms);
                ShipProtocol_LogRxDebug(SHIP_STAGE_U8("rx-idle"));
            }

            if ((g_ship_rt.remote_online != 0U) &&
                (g_ship_rt.last_proto_rx_ms != 0UL) &&
                (rx_silence_ms >= SHIP_THROTTLE_TIMEOUT_MS)) {
                g_ship_rt.remote_online = 0U;
                if (g_ship_rt.throttle_online != 0U) {
                    g_ship_rt.throttle_online = 0U;
                    ShipControl_Stop(SHIP_CONTROL_STOP_REASON_REMOTE_TIMEOUT);
                }
                LOGW(SHIP_TAG, "remote link timeout by aa-bb-frame, dt=%lums",
                     rx_silence_ms);
                ShipProtocol_LogRxDebug(SHIP_STAGE_U8("remote-timeout"));
            }

            if ((g_ship_rt.throttle_online != 0U) &&
                (g_ship_rt.last_throttle_rx_ms != 0UL) &&
                ((now_ms - g_ship_rt.last_throttle_rx_ms) >= SHIP_THROTTLE_TIMEOUT_MS)) {
                g_ship_rt.throttle_online = 0U;
                LOGW(SHIP_TAG, "manual control timeout by cmd=0x11, dt=%lums",
                     (u32)(now_ms - g_ship_rt.last_throttle_rx_ms));
                ShipControl_Stop(SHIP_CONTROL_STOP_REASON_MANUAL_TIMEOUT);
                ShipProtocol_LogRxDebug(SHIP_STAGE_U8("throttle-timeout"));
            }

            if ((g_ship_rt.paired != 0U) &&
                (g_ship_rt.throttle_recover_done == 0U) &&
                (g_ship_rt.last_proto_rx_ms != 0UL) &&
                (rx_silence_ms >= SHIP_THROTTLE_RECOVER_MS)) {
                g_ship_rt.throttle_recover_done = 1U;
                ShipProtocol_ApplyDefaultRf();
                g_ship_rt.work_rx_reopen_ticks = 0U;
                LOGW(SHIP_TAG,
                     "legacy remote recovery after %lums link silence, re-open work-rx",
                     rx_silence_ms);
                ShipProtocol_ReopenWorkRx(SHIP_REASON_C("legacy remote recovery"), 1U, 1U);
            }

            ShipProtocol_LogPowerSample(&g_ship_power_sample, 0U);
        }

    }

    AutoDrive_Poll();
    ShipProtocol_ServiceAutoDriveDiag(now_ms);
    ShipControl_Tick(now_ms);
}

u8 ShipProtocol_IsPaired(void)
{
    return g_ship_rt.paired;
}
