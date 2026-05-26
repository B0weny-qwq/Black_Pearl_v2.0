#include "ship_protocol.h"
#include "app.h"
#include "autodrive.h"
#include "board_gps.h"
#include "board_power.h"
#include "board_wireless.h"
#include "logger.h"
#include "platform_scheduler.h"
#include "ship_control.h"

#define SHIP_TAG                         "SHIP"
#define SHIP_PAIR_CHANNEL_DEFAULT        0x7FU
#define SHIP_PAIR_SEED0                  0x65U
#define SHIP_PAIR_SEED1                  0x65U
#define SHIP_PAIR_SEED2                  0xA0U
#define SHIP_PAIR_SEED3                  0x65U
#define SHIP_PAIR_SEND_TIMES             10U
#define SHIP_WAIT_TICKS_DEFAULT          30U
#define SHIP_PAIR_WAIT_RSP_TICKS         500U
#define SHIP_PAIR_RSP_EXPIRE_LOG_MS      5000UL
#define SHIP_WORK_RX_REOPEN_TICKS        200U
#define SHIP_RX_IDLE_WARN_MS             3000UL
#define SHIP_THROTTLE_TIMEOUT_MS         1500UL
#define SHIP_THROTTLE_RECOVER_MS         3000UL
#define SHIP_MANUAL_BOOT_BLOCK_MS        3000UL
#define SHIP_MANUAL_BOOT_WAIT_HEADING    1U
#define SHIP_AXIS_CENTER                 100U
#define SHIP_KEY_NULL                    SHIP_PROTOCOL_KEY_NONE
#define SHIP_GPS_REPORT_PAYLOAD_LEN      15U
#define SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN  36U
#define SHIP_POWER_LEVEL_0               0U
#define SHIP_POWER_LEVEL_1               1U
#define SHIP_POWER_LEVEL_2               2U
#define SHIP_POWER_LEVEL_3               3U
#define SHIP_POWER_LEVEL_4               4U
#define SHIP_LOWPOWER_CHECK_TICKS        600U
#define SHIP_LOWPOWER_ACCEL_MAX          10U
#define SHIP_POWER_SAMPLE_DIVIDER        100U
#define SHIP_POWER_LOG_PERIOD_MS         1000UL
#define SHIP_ADC_LOG_ENABLE              1
#define SHIP_CRUISE_KEY_START_INPUT      60
#define SHIP_CRUISE_KEY_STOP_INPUT       (-50)
#define SHIP_CRUISE_KEY_SPEED            760
#define SHIP_CRUISE_STEER_START_MAX      8
#define SHIP_CRUISE_GYRO_START_MAX_DPS   8
#define SHIP_AUTODRIVE_DIAG_PERIOD_MS    1000UL
#define SHIP_AUTODRIVE_DIAG_MIN_GAP_MS   200UL

typedef struct
{
    u16 raw;
    u16 adc_mv;
    u32 bat_mv;
    u8  report;
    u8  valid;
} ship_protocol_power_sample_t;

typedef struct
{
    u8 seed[4];
    u8 work_rx_channel;
    u8 work_tx_channel;
    u8 key0;
    u8 key1;
    u16 reg36;
    u16 reg39;
} ship_protocol_pair_params_t;

typedef enum
{
    SHIP_PARSE_WAIT_HEAD = 0,
    SHIP_PARSE_READ_LEN,
    SHIP_PARSE_READ_BODY,
    SHIP_PARSE_DISPATCH
} ship_protocol_parse_state_t;

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
    ship_protocol_state_t state;
    u8 rf_channel[3];
    u8 rf_send_key[2];
    u16 pair_wait_rsp_ticks;
    u16 wait_ticks;
    u16 pair_left;
    u16 pair_retry_count;
    u16 work_rx_reopen_ticks;
    u16 work_rx_reopen_total;
    u32 tick_ms;
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
    u8 power_level;
    u8 power_adc_ready;
    u8 lowpower_return_latched;
    u16 power_sample_divider_count;
    u16 lowpower_check_ticks;
    u32 power_sample_period_ms;
    ship_protocol_power_sample_t power_sample;
    ship_protocol_event_snapshot_t event;
    ship_protocol_event_snapshot_t event_queue[SHIP_PROTOCOL_EVENT_QUEUE_DEPTH];
    u8 event_queue_head;
    u8 event_queue_tail;
    u8 event_queue_count;
    u16 event_queue_dropped;
} ship_protocol_runtime_t;

static ship_protocol_runtime_t ship_protocol_rt;
static u8 ship_protocol_initialized;
static u8 ship_protocol_parse_buffer[SHIP_PROTO_MAX_FRAME_LEN];
static u8 ship_protocol_tx_frame[SHIP_PROTO_MAX_FRAME_LEN];
static u8 ship_protocol_rx_payload[BOARD_WIRELESS_MAX_PAYLOAD_LEN];
static u8 ship_protocol_gps_payload[SHIP_GPS_REPORT_PAYLOAD_LEN];
static u8 ship_protocol_diag_payload[SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN];
static u8 ship_protocol_parse_index;
static u8 ship_protocol_parse_expected_len;
static ship_protocol_parse_state_t ship_protocol_parse_state;
static ship_protocol_pair_params_t ship_protocol_pair_params;

static void ship_protocol_power_init(void);
static void ship_protocol_read_power_sample(ship_protocol_power_sample_t *sample);
static void ship_protocol_service_power_sample(void);
static void ship_protocol_low_power_check(void);
static u8 ship_protocol_get_autodrive_status(void);
static void ship_protocol_log_power_sample(const ship_protocol_power_sample_t *sample, u8 force_log);
static void ship_protocol_publish_event(ship_protocol_event_type_t type,
                                        ship_protocol_event_state_t state,
                                        u8 cmd,
                                        u8 payload_len);
static void ship_protocol_event_queue_reset(void);
static void ship_protocol_event_queue_push(const ship_protocol_event_snapshot_t *event);
static void ship_protocol_clear_point_event(void);

static u8 ship_protocol_xor(const u8 *buf, u8 len)
{
    u8 i;
    u8 value;

    value = 0U;
    for (i = 0U; i < len; i++) {
        value ^= buf[i];
    }
    return value;
}

static void ship_protocol_get_pair_seed(u8 *seed)
{
    if (seed == 0) {
        return;
    }

    seed[0] = SHIP_PAIR_SEED0;
    seed[1] = SHIP_PAIR_SEED1;
    seed[2] = SHIP_PAIR_SEED2;
    seed[3] = SHIP_PAIR_SEED3;
}

static void ship_protocol_calc_pair_params(ship_protocol_pair_params_t *params)
{
    u8 key0;
    u8 key1;
    u8 channel;

    if (params == 0) {
        return;
    }

    ship_protocol_get_pair_seed(params->seed);

    key0 = (u8)((params->seed[0] & 0x0FU) +
                ((params->seed[3] >> 2) + (params->seed[3] % 0x03U)));
    key1 = (u8)((params->seed[1] & 0x0FU) +
                ((params->seed[2] >> 3) + (params->seed[0] % 0x06U)));
    channel = (u8)((((params->seed[3] + 0x06U) % 0x40U) +
                    ((params->seed[2] >> 3) * 0x08U) +
                    (((params->seed[1] | params->seed[0]) % 0x08U) / 2U)) % 0x40U);

    params->work_rx_channel = channel;
    params->work_tx_channel = (u8)(channel + 0x40U);
    params->key0 = key0;
    params->key1 = key1;
    params->reg36 = (u16)(((u16)key0 << 8) | key0);
    params->reg39 = (u16)(((u16)key1 << 8) | key1);
}

static void ship_protocol_apply_default_rf(void)
{
    ship_protocol_calc_pair_params(&ship_protocol_pair_params);

    ship_protocol_rt.rf_channel[0] = ship_protocol_pair_params.work_rx_channel;
    ship_protocol_rt.rf_channel[1] = ship_protocol_pair_params.work_rx_channel;
    ship_protocol_rt.rf_channel[2] = ship_protocol_pair_params.work_tx_channel;
    ship_protocol_rt.rf_send_key[0] = ship_protocol_pair_params.key0;
    ship_protocol_rt.rf_send_key[1] = ship_protocol_pair_params.key1;
}

static void ship_protocol_put_u16_be(u8 *buf, u16 value)
{
    buf[0] = (u8)(value >> 8);
    buf[1] = (u8)(value & 0x00FFU);
}

static u16 ship_protocol_read_u16_be(const u8 *buf)
{
    return (u16)(((u16)buf[0] << 8) | buf[1]);
}

static u32 ship_protocol_abs_int32(int32 value)
{
    if (value < 0L) {
        return (u32)(0L - value);
    }

    return (u32)value;
}

static void ship_protocol_clear_spi_ps_event(void)
{
    u8 i;

    ship_protocol_rt.event.spi_ps.status = 0;
    ship_protocol_rt.event.spi_ps.len = 0U;
    ship_protocol_rt.event.spi_ps.stored_len = 0U;
    for (i = 0U; i < SHIP_PROTOCOL_SPI_PS_EVENT_DATA_MAX; i++) {
        ship_protocol_rt.event.spi_ps.bytes[i] = 0U;
    }
}

static void ship_protocol_clear_event_payload(void)
{
    ship_protocol_rt.event.switch_state = 0U;
    ship_protocol_rt.event.point_valid = 0U;
    ship_protocol_rt.event.key_action = SHIP_PROTOCOL_KEY_ACTION_NONE;
    ship_protocol_rt.event.power.raw = 0U;
    ship_protocol_rt.event.power.adc_mv = 0U;
    ship_protocol_rt.event.power.bat_mv = 0UL;
    ship_protocol_rt.event.power.level = SHIP_POWER_LEVEL_0;
    ship_protocol_rt.event.power.valid = 0U;
    ship_protocol_clear_spi_ps_event();
}

static void ship_protocol_fill_power_event(const ship_protocol_power_sample_t *sample)
{
    if (sample == 0) {
        ship_protocol_rt.event.power.raw = 0U;
        ship_protocol_rt.event.power.adc_mv = 0U;
        ship_protocol_rt.event.power.bat_mv = 0UL;
        ship_protocol_rt.event.power.level = ship_protocol_rt.power_level;
        ship_protocol_rt.event.power.valid = 0U;
        return;
    }

    ship_protocol_rt.event.power.raw = sample->raw;
    ship_protocol_rt.event.power.adc_mv = sample->adc_mv;
    ship_protocol_rt.event.power.bat_mv = sample->bat_mv;
    ship_protocol_rt.event.power.level = sample->report;
    ship_protocol_rt.event.power.valid = sample->valid;
}

static void ship_protocol_event_queue_reset(void)
{
    u8 i;

    ship_protocol_rt.event_queue_head = 0U;
    ship_protocol_rt.event_queue_tail = 0U;
    ship_protocol_rt.event_queue_count = 0U;
    ship_protocol_rt.event_queue_dropped = 0U;
    for (i = 0U; i < SHIP_PROTOCOL_EVENT_QUEUE_DEPTH; i++) {
        ship_protocol_rt.event_queue[i].type = SHIP_PROTOCOL_EVENT_NONE;
        ship_protocol_rt.event_queue[i].state = SHIP_PROTOCOL_EVENT_STATE_IDLE;
        ship_protocol_rt.event_queue[i].pending = 0U;
    }
}

static void ship_protocol_event_queue_push(const ship_protocol_event_snapshot_t *event)
{
    if (event == 0) {
        return;
    }

    if (ship_protocol_rt.event_queue_count >= SHIP_PROTOCOL_EVENT_QUEUE_DEPTH) {
        ship_protocol_rt.event_queue_head++;
        if (ship_protocol_rt.event_queue_head >= SHIP_PROTOCOL_EVENT_QUEUE_DEPTH) {
            ship_protocol_rt.event_queue_head = 0U;
        }
        ship_protocol_rt.event_queue_count--;
        ship_protocol_rt.event_queue_dropped++;
    }

    ship_protocol_rt.event_queue[ship_protocol_rt.event_queue_tail] = *event;
    ship_protocol_rt.event_queue_tail++;
    if (ship_protocol_rt.event_queue_tail >= SHIP_PROTOCOL_EVENT_QUEUE_DEPTH) {
        ship_protocol_rt.event_queue_tail = 0U;
    }
    ship_protocol_rt.event_queue_count++;
}

static void ship_protocol_to_legacy_nmea_coord(u32 abs_deg1e7, u16 *coord1, u16 *coord2)
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

static void ship_protocol_power_init(void)
{
    ship_protocol_rt.power_adc_ready =
        (board_power_init() == BOARD_POWER_OK) ? 1U : 0U;
}

static void ship_protocol_read_power_sample(ship_protocol_power_sample_t *sample)
{
    board_power_sample_t board_sample;

    if (sample == 0) {
        return;
    }

    *sample = ship_protocol_rt.power_sample;
    sample->report = ship_protocol_rt.power_level;
    if (board_power_read(&board_sample) != BOARD_POWER_OK) {
        sample->valid = 0U;
        return;
    }

    sample->raw = board_sample.raw;
    sample->adc_mv = board_sample.adc_mv;
    sample->bat_mv = board_sample.bat_mv;
    sample->report = board_sample.level;
    sample->valid = board_sample.valid;
}

static void ship_protocol_service_power_sample(void)
{
    u8 previous_level;

    if (ship_protocol_rt.power_sample_divider_count < SHIP_POWER_SAMPLE_DIVIDER) {
        ship_protocol_rt.power_sample_divider_count++;
        return;
    }

    ship_protocol_rt.power_sample_divider_count = 0U;
    previous_level = ship_protocol_rt.power_level;
    ship_protocol_read_power_sample(&ship_protocol_rt.power_sample);
    if (ship_protocol_rt.power_sample.valid != 0U) {
        ship_protocol_rt.power_level = ship_protocol_rt.power_sample.report;
        ship_protocol_clear_event_payload();
        ship_protocol_fill_power_event(&ship_protocol_rt.power_sample);
        if (ship_protocol_rt.power_level != previous_level) {
            ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED,
                                        SHIP_PROTOCOL_EVENT_STATE_POWER,
                                        0U,
                                        0U);
        } else {
            ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_POWER_SAMPLE,
                                        SHIP_PROTOCOL_EVENT_STATE_POWER,
                                        0U,
                                        0U);
        }
    }
}

static void ship_protocol_log_power_sample(const ship_protocol_power_sample_t *sample, u8 force_log)
{
#if SHIP_ADC_LOG_ENABLE
    static u32 last_log_ms = 0UL;

    if (sample == 0) {
        return;
    }
    if ((force_log == 0U) &&
        (SHIP_POWER_LOG_PERIOD_MS != 0UL) &&
        ((ship_protocol_rt.tick_ms - last_log_ms) < SHIP_POWER_LOG_PERIOD_MS)) {
        return;
    }
    last_log_ms = ship_protocol_rt.tick_ms;

    if (sample->valid == 0U) {
        LOGW(SHIP_TAG, "adc fail raw=%u p=%u", sample->raw, (u16)sample->report);
        return;
    }

    LOGI(SHIP_TAG, "adc raw=%u mv=%u bat=%lu p=%u",
         sample->raw,
         sample->adc_mv,
         sample->bat_mv,
         (u16)sample->report);
#else
    (void)sample;
    (void)force_log;
#endif
}

static void ship_protocol_low_power_check(void)
{
    ship_protocol_service_power_sample();
    ship_protocol_log_power_sample(&ship_protocol_rt.power_sample, 0U);

    if (ship_protocol_rt.power_level == SHIP_POWER_LEVEL_0) {
        if (ship_protocol_rt.lowpower_check_ticks < 0xFFFFU) {
            ship_protocol_rt.lowpower_check_ticks++;
        }
        if ((ship_protocol_rt.lowpower_return_latched == 0U) &&
            (ship_protocol_rt.lowpower_check_ticks > SHIP_LOWPOWER_CHECK_TICKS) &&
            (AutoDrive_GetMode() == AUTO_DRIVE_CLOSE) &&
            (ShipControl_GetManualAccelerator() < SHIP_LOWPOWER_ACCEL_MAX)) {
            ship_protocol_rt.lowpower_return_latched = 1U;
            ship_protocol_clear_event_payload();
            ship_protocol_fill_power_event(&ship_protocol_rt.power_sample);
            ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED,
                                        SHIP_PROTOCOL_EVENT_STATE_POWER,
                                        0U,
                                        0U);
            LOGW(SHIP_TAG, "low power latched ticks=%u p=%u",
                 ship_protocol_rt.lowpower_check_ticks,
                 (u16)ship_protocol_rt.power_level);
            AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_LOW_POWER);
        }
    } else {
        ship_protocol_rt.lowpower_check_ticks = 0U;
        ship_protocol_rt.lowpower_return_latched = 0U;
    }
}

static u8 ship_protocol_get_autodrive_status(void)
{
    return AutoDrive_InActive();
}

static void ship_protocol_build_gps_payload(u8 *payload)
{
    const board_gps_state_t *gps;
    ship_protocol_power_sample_t power;
    u16 angle;
    u16 lon1;
    u16 lon2;
    u16 lat1;
    u16 lat2;
    u8 sat;

    gps = board_gps_get_state();
    sat = 0U;
    lon1 = 0U;
    lon2 = 0U;
    lat1 = 0U;
    lat2 = 0U;

    if (gps != 0) {
        sat = gps->satellites_used_gsa;
        if (sat == 0U) {
            sat = gps->satellites_used;
        }
        if (sat > 24U) {
            sat = 24U;
        }

        angle = (u16)((gps->course_deg_x100 / 100U) % 360U);
        if (gps->legacy_coord_valid != 0U) {
            lon1 = gps->legacy_lon1;
            lon2 = gps->legacy_lon2;
            lat1 = gps->legacy_lat1;
            lat2 = gps->legacy_lat2;
        } else {
            ship_protocol_to_legacy_nmea_coord(ship_protocol_abs_int32(gps->lon_deg1e7),
                                               &lon1,
                                               &lon2);
            ship_protocol_to_legacy_nmea_coord(ship_protocol_abs_int32(gps->lat_deg1e7),
                                               &lat1,
                                               &lat2);
        }

        payload[1] = (u8)(angle >> 8);
        payload[2] = (u8)(angle & 0x00FFU);
    } else {
        payload[1] = 0U;
        payload[2] = 0U;
    }

    power = ship_protocol_rt.power_sample;
    power.report = ship_protocol_rt.power_level;

    payload[0] = sat;
    /* Match the legacy handheld parser exactly.
     * 0x12 keeps fixed E/W marker bytes even when the real hemisphere differs.
     */
    payload[3] = (u8)'E';
    ship_protocol_put_u16_be(&payload[4], lon1);
    ship_protocol_put_u16_be(&payload[6], lon2);
    payload[8] = (u8)'W';
    ship_protocol_put_u16_be(&payload[9], lat1);
    ship_protocol_put_u16_be(&payload[11], lat2);
    payload[13] = ship_protocol_rt.power_level;
    payload[14] = ship_protocol_get_autodrive_status();

    ship_protocol_log_power_sample(&power, 0U);
}

static int8 ship_protocol_send_frame(u8 channel, u8 cmd, const u8 *payload, u8 payload_len)
{
    u8 body_len;
    u8 idx;
    u8 i;
    int8 ret;

    if ((payload_len > 0U) && (payload == 0)) {
        return BOARD_WIRELESS_ERR_PARAM;
    }
    if ((u16)payload_len + 5U > SHIP_PROTO_MAX_FRAME_LEN) {
        return BOARD_WIRELESS_ERR_PARAM;
    }

    body_len = (u8)(2U + payload_len);
    idx = 0U;
    ship_protocol_tx_frame[idx++] = SHIP_PROTO_HEAD;
    ship_protocol_tx_frame[idx++] = body_len;
    ship_protocol_tx_frame[idx++] = cmd;
    for (i = 0U; i < payload_len; i++) {
        ship_protocol_tx_frame[idx++] = payload[i];
    }
    ship_protocol_tx_frame[idx] = ship_protocol_xor(&ship_protocol_tx_frame[1], (u8)(1U + 1U + payload_len));
    idx++;
    ship_protocol_tx_frame[idx++] = SHIP_PROTO_TAIL;

    ret = board_wireless_send_on_channel(channel, ship_protocol_tx_frame, idx);
    if (ret == BOARD_WIRELESS_OK) {
        LOGI(SHIP_TAG, "tx cmd=0x%02X ch=%u payload_len=%u",
             (u16)cmd,
             (u16)channel,
             (u16)payload_len);
    } else {
        LOGE(SHIP_TAG, "tx cmd=0x%02X fail rc=%d", (u16)cmd, ret);
    }

    return ret;
}

static void ship_protocol_send_gps_once(void)
{
    ship_protocol_build_gps_payload(ship_protocol_gps_payload);
    (void)ship_protocol_send_frame(ship_protocol_rt.rf_channel[2],
                                   SHIP_CMD_GPS_REPORT,
                                   ship_protocol_gps_payload,
                                   SHIP_GPS_REPORT_PAYLOAD_LEN);
    (void)board_wireless_set_channel(ship_protocol_rt.rf_channel[0]);
}

static void ship_protocol_write_point_legacy(u8 *dst, const AutoDrive_PointRaw_t *point)
{
    if ((dst == 0) || (point == 0)) {
        return;
    }

    dst[0] = point->lon_ew;
    ship_protocol_put_u16_be(&dst[1], point->lon_whole);
    ship_protocol_put_u16_be(&dst[3], point->lon_frac);
    dst[5] = point->lat_ns;
    ship_protocol_put_u16_be(&dst[6], point->lat_whole);
    ship_protocol_put_u16_be(&dst[8], point->lat_frac);
}

static void ship_protocol_send_autodrive_diag_once(u8 log_this_tx)
{
    AutoDrive_DebugSnapshot_t snapshot;
    u8 idx;

    AutoDrive_GetDebugSnapshot(&snapshot);
    idx = 0U;
    ship_protocol_diag_payload[idx++] = 0x01U;
    ship_protocol_diag_payload[idx++] = snapshot.state;
    ship_protocol_diag_payload[idx++] = snapshot.mode;
    ship_protocol_diag_payload[idx++] = snapshot.auto_ret_onoff;
    ship_protocol_diag_payload[idx++] = snapshot.fail_flag;
    ship_protocol_diag_payload[idx++] = snapshot.last_reason;
    ship_protocol_diag_payload[idx++] = snapshot.gps_ready;
    ship_protocol_diag_payload[idx++] = snapshot.sat_count;
    ship_protocol_diag_payload[idx++] = snapshot.can_activate_target;
    ship_protocol_diag_payload[idx++] = 0U;
    ship_protocol_put_u16_be(&ship_protocol_diag_payload[idx], snapshot.distance_to_target_m);
    idx = (u8)(idx + 2U);
    ship_protocol_put_u16_be(&ship_protocol_diag_payload[idx], snapshot.current_heading_deg);
    idx = (u8)(idx + 2U);
    ship_protocol_put_u16_be(&ship_protocol_diag_payload[idx], snapshot.target_heading_deg);
    idx = (u8)(idx + 2U);
    ship_protocol_write_point_legacy(&ship_protocol_diag_payload[idx], &snapshot.current_point);
    idx = (u8)(idx + AUTODRIVE_LEGACY_POINT_WIRE_LEN);
    ship_protocol_write_point_legacy(&ship_protocol_diag_payload[idx], &snapshot.target_point);
    idx = (u8)(idx + AUTODRIVE_LEGACY_POINT_WIRE_LEN);

    if (idx != SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN) {
        LOGE(SHIP_TAG, "diag len bad=%u", (u16)idx);
        return;
    }

    if (log_this_tx != 0U) {
        LOGI(SHIP_TAG, "tx cmd=0x16 state=%u mode=%u sw=0x%02X reason=%u gps=%u sat=%u dist=%u",
             (u16)snapshot.state,
             (u16)snapshot.mode,
             (u16)snapshot.auto_ret_onoff,
             (u16)snapshot.last_reason,
             (u16)snapshot.gps_ready,
             (u16)snapshot.sat_count,
             snapshot.distance_to_target_m);
    }

    (void)ship_protocol_send_frame(ship_protocol_rt.rf_channel[2],
                                   SHIP_CMD_AUTODRIVE_DIAG,
                                   ship_protocol_diag_payload,
                                   SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN);
    (void)board_wireless_set_channel(ship_protocol_rt.rf_channel[0]);
}

static void ship_protocol_service_autodrive_diag(u32 now_ms)
{
    static u8 initialized = 0U;
    static u8 last_state = 0U;
    static u8 last_mode = 0U;
    static u8 last_busy = 0U;
    static u8 last_reason = 0U;
    static u32 last_tx_ms = 0UL;
    AutoDrive_DebugSnapshot_t snapshot;
    u8 busy;
    u8 changed;

    AutoDrive_GetDebugSnapshot(&snapshot);
    busy = (snapshot.state != AUTO_DRIVE_IDLE) ? 1U : 0U;
    changed = 0U;

    if (initialized == 0U) {
        initialized = 1U;
        changed = 1U;
    } else if ((snapshot.state != last_state) ||
               (snapshot.mode != last_mode) ||
               (busy != last_busy) ||
               (snapshot.last_reason != last_reason)) {
        changed = 1U;
    }

    if (((changed != 0U) &&
         ((now_ms - last_tx_ms) >= SHIP_AUTODRIVE_DIAG_MIN_GAP_MS)) ||
        ((now_ms - last_tx_ms) >= SHIP_AUTODRIVE_DIAG_PERIOD_MS)) {
        ship_protocol_send_autodrive_diag_once(changed);
        last_tx_ms = now_ms;
    }

    last_state = snapshot.state;
    last_mode = snapshot.mode;
    last_busy = busy;
    last_reason = snapshot.last_reason;
}

static int8 ship_protocol_apply_work_sync_idle(void)
{
    ship_protocol_apply_default_rf();

    return board_wireless_set_sync_regs_idle(ship_protocol_pair_params.reg36,
                                             ship_protocol_pair_params.reg39);
}

static int8 ship_protocol_apply_work_rx(void)
{
    int8 ret;

    ship_protocol_apply_default_rf();
    ret = board_wireless_set_channel(ship_protocol_rt.rf_channel[0]);
    if (ret == BOARD_WIRELESS_OK) {
        ship_protocol_rt.work_rx_configured = 1U;
        ship_protocol_rt.work_rx_reopen_ticks = 0U;
        ship_protocol_rt.rx_idle_warned = 0U;
    } else {
        ship_protocol_rt.work_rx_configured = 0U;
    }

    return ret;
}

static int8 ship_protocol_try_pair_send(u16 left_after_send)
{
    u8 pair_xor;
    int8 ret;

    ship_protocol_apply_default_rf();
    pair_xor = (u8)(0x06U ^
                    SHIP_CMD_PAIR ^
                    ship_protocol_pair_params.seed[0] ^
                    ship_protocol_pair_params.seed[1] ^
                    ship_protocol_pair_params.seed[2] ^
                    ship_protocol_pair_params.seed[3]);

    ret = ship_protocol_send_frame(SHIP_PAIR_CHANNEL_DEFAULT,
                                   SHIP_CMD_PAIR,
                                   ship_protocol_pair_params.seed,
                                   4U);
    if (ret == BOARD_WIRELESS_OK) {
        LOGI(SHIP_TAG, "pair req sent seq=%u/%u retry=%u ch=0x%02X",
             (u16)(SHIP_PAIR_SEND_TIMES - left_after_send),
             (u16)SHIP_PAIR_SEND_TIMES,
             (u16)ship_protocol_rt.pair_retry_count,
             (u16)SHIP_PAIR_CHANNEL_DEFAULT);
        if (left_after_send == (SHIP_PAIR_SEND_TIMES - 1U)) {
            LOGI(SHIP_TAG,
                 "pair req start retry=%u pair_ch=0x%02X seed=%02X%02X%02X%02X work_rx=%u work_tx=%u key=%u/%u reg36=0x%04X reg39=0x%04X",
                 (u16)ship_protocol_rt.pair_retry_count,
                 (u16)SHIP_PAIR_CHANNEL_DEFAULT,
                 (u16)ship_protocol_pair_params.seed[0],
                 (u16)ship_protocol_pair_params.seed[1],
                 (u16)ship_protocol_pair_params.seed[2],
                 (u16)ship_protocol_pair_params.seed[3],
                 (u16)ship_protocol_rt.rf_channel[0],
                 (u16)ship_protocol_rt.rf_channel[2],
                 (u16)ship_protocol_rt.rf_send_key[0],
                 (u16)ship_protocol_rt.rf_send_key[1],
                 ship_protocol_pair_params.reg36,
                 ship_protocol_pair_params.reg39);
            LOGI(SHIP_TAG,
                 "pair req frame=AA 06 10 %02X %02X %02X %02X %02X BB",
                 (u16)ship_protocol_pair_params.seed[0],
                 (u16)ship_protocol_pair_params.seed[1],
                 (u16)ship_protocol_pair_params.seed[2],
                 (u16)ship_protocol_pair_params.seed[3],
                 (u16)pair_xor);
        } else if (left_after_send == 0U) {
            LOGI(SHIP_TAG, "pair req burst done, wait rsp");
        }
    } else {
        LOGE(SHIP_TAG, "pair req tx fail rc=%d", ret);
    }

    return ret;
}

static int8 ship_protocol_arm_pair_rsp_window(void)
{
    int8 ret;

    ret = ship_protocol_apply_work_sync_idle();
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }

    ret = ship_protocol_apply_work_rx();
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }

    ship_protocol_rt.pair_wait_rsp_ticks = SHIP_PAIR_WAIT_RSP_TICKS;
    ship_protocol_rt.pair_wait_start_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.last_proto_rx_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.pair_rsp_timeout_logged = 0U;
    LOGI(SHIP_TAG,
         "pair rsp window open ticks=%u work_rx=%u key=%u/%u reg36=0x%04X reg39=0x%04X",
         (u16)ship_protocol_rt.pair_wait_rsp_ticks,
         (u16)ship_protocol_rt.rf_channel[0],
         (u16)ship_protocol_rt.rf_send_key[0],
         (u16)ship_protocol_rt.rf_send_key[1],
         ship_protocol_pair_params.reg36,
         ship_protocol_pair_params.reg39);
    return BOARD_WIRELESS_OK;
}

static void ship_protocol_mark_proto_activity(u8 cmd)
{
    ship_protocol_rt.last_proto_rx_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.remote_online = 1U;
    ship_protocol_rt.rx_idle_warned = 0U;
    if (ship_protocol_rt.paired == 0U) {
        ship_protocol_rt.paired = 1U;
        ship_protocol_rt.pair_left = 0U;
        ship_protocol_rt.pair_wait_rsp_ticks = 0U;
        ship_protocol_rt.pair_wait_start_ms = 0UL;
        ship_protocol_rt.pair_rsp_timeout_logged = 0U;
        ship_protocol_rt.state = SHIP_PROTOCOL_STATE_WORK_RX;
        ship_protocol_rt.work_state_logged = 0U;
        LOGI(SHIP_TAG, "pair ok by aa-bb-frame cmd=0x%02X work_rx=%u key=%u/%u",
             (u16)cmd,
             (u16)ship_protocol_rt.rf_channel[0],
             (u16)ship_protocol_rt.rf_send_key[0],
             (u16)ship_protocol_rt.rf_send_key[1]);
    }
}

static void ship_protocol_publish_event(ship_protocol_event_type_t type,
                                        ship_protocol_event_state_t state,
                                        u8 cmd,
                                        u8 payload_len)
{
    ship_protocol_rt.event.type = type;
    ship_protocol_rt.event.state = state;
    ship_protocol_rt.event.cmd = cmd;
    ship_protocol_rt.event.payload_len = payload_len;
    ship_protocol_rt.event.pending = 1U;
    ship_protocol_rt.event.sequence++;
    ship_protocol_rt.event.tick_ms = ship_protocol_rt.tick_ms;
    ship_protocol_event_queue_push(&ship_protocol_rt.event);
}

static void ship_protocol_publish_error_event(u8 cmd, u8 payload_len)
{
    ship_protocol_clear_event_payload();
    ship_protocol_clear_point_event();
    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_FRAME_ERROR,
                                SHIP_PROTOCOL_EVENT_STATE_ERROR,
                                cmd,
                                payload_len);
}

static void ship_protocol_parse_point_payload(const u8 *payload, ship_protocol_point_t *point)
{
    if ((payload == 0) || (point == 0)) {
        return;
    }

    point->lon_ew = payload[0];
    point->lon_whole = ship_protocol_read_u16_be(&payload[1]);
    point->lon_frac = ship_protocol_read_u16_be(&payload[3]);
    point->lat_ns = payload[5];
    point->lat_whole = ship_protocol_read_u16_be(&payload[6]);
    point->lat_frac = ship_protocol_read_u16_be(&payload[8]);
}

static void ship_protocol_clear_point_event(void)
{
    ship_protocol_rt.event.point.lon_ew = 0U;
    ship_protocol_rt.event.point.lon_whole = 0U;
    ship_protocol_rt.event.point.lon_frac = 0U;
    ship_protocol_rt.event.point.lat_ns = 0U;
    ship_protocol_rt.event.point.lat_whole = 0U;
    ship_protocol_rt.event.point.lat_frac = 0U;
    ship_protocol_rt.event.point_valid = 0U;
}

static void ship_protocol_log_point(const char *label, const ship_protocol_point_t *point)
{
    if (point == 0) {
        return;
    }

    LOGI(SHIP_TAG, "%s ew=0x%02X lon=%u.%u ns=0x%02X lat=%u.%u",
         label,
         (u16)point->lon_ew,
         point->lon_whole,
         point->lon_frac,
         (u16)point->lat_ns,
         point->lat_whole,
         point->lat_frac);
}

static int16 ship_protocol_raw_ud_to_input(u8 front_back)
{
    return (int16)((int16)front_back - (int16)SHIP_AXIS_CENTER);
}

static int16 ship_protocol_raw_lr_to_input(u8 left_right)
{
    return (int16)((int16)left_right - (int16)SHIP_AXIS_CENTER);
}

static ship_protocol_key_action_t ship_protocol_key_to_action(u8 key)
{
    switch (key) {
    case SHIP_PROTOCOL_KEY_B_UNUSED:
        return SHIP_PROTOCOL_KEY_ACTION_B_NOOP;
    case SHIP_PROTOCOL_KEY_C_UNUSED:
        return SHIP_PROTOCOL_KEY_ACTION_C_NOOP;
    case SHIP_PROTOCOL_KEY_D_UNUSED:
        return SHIP_PROTOCOL_KEY_ACTION_D_NOOP;
    default:
        return SHIP_PROTOCOL_KEY_ACTION_NONE;
    }
}

static const char *ship_protocol_fish_result_name(u8 result)
{
    switch (result) {
    case AUTODRIVE_FISH_CMD_BUSY:
        return "busy";
    case AUTODRIVE_FISH_CMD_STORED:
        return "stored";
    case AUTODRIVE_FISH_CMD_DUP_WAIT:
        return "dup-wait";
    case AUTODRIVE_FISH_CMD_REJECT_UNKNOWN:
        return "reject-unknown";
    case AUTODRIVE_FISH_CMD_REJECT_DISTANCE:
        return "reject-distance";
    case AUTODRIVE_FISH_CMD_STARTED:
        return "started";
    case AUTODRIVE_FISH_CMD_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

static void ship_protocol_log_goto_point(const u8 *frame,
                                         u8 frame_len,
                                         const u8 *payload,
                                         u8 payload_len,
                                         u8 xor_calc,
                                         u8 xor_recv,
                                         u8 result)
{
    ship_protocol_point_t point;
    u8 idx;

    idx = AutoDrive_GetLastFishCommandIndex();
    LOGI(SHIP_TAG,
         "0x14 rx fl=%u pl=%u xor=%02X/%02X res=%u(%s) idx=%u",
         (u16)frame_len,
         (u16)payload_len,
         (u16)xor_calc,
         (u16)xor_recv,
         (u16)result,
         ship_protocol_fish_result_name(result),
         (u16)idx);

    if ((payload != 0) && (payload_len >= SHIP_PROTOCOL_POINT_PAYLOAD_LEN)) {
        ship_protocol_parse_point_payload(payload, &point);
        ship_protocol_log_point("0x14 point", &point);
        LOGI(SHIP_TAG,
             "0x14 payload=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
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
    }

    if ((frame != 0) && (frame_len >= 5U)) {
        LOGI(SHIP_TAG,
             "0x14 frame head=%02X len=%u cmd=%02X tail=%02X",
             (u16)frame[0],
             (u16)frame[1],
             (u16)frame[2],
             (u16)frame[frame_len - 1U]);
    }
}

static void ship_protocol_handle_key_edge(u8 key)
{
    const char *name;
    ship_protocol_key_action_t key_action;
    int16 throttle_input;
    int16 steering_input;
    int16 yaw_rate_dps;
    u16 heading_cd;

    switch (key) {
    case SHIP_PROTOCOL_KEY_A_LIGHT:
        name = "A-light";
        break;
    case SHIP_PROTOCOL_KEY_B_UNUSED:
        name = "B-noop";
        break;
    case SHIP_PROTOCOL_KEY_C_UNUSED:
        name = "C-noop";
        break;
    case SHIP_PROTOCOL_KEY_D_UNUSED:
        name = "D-noop";
        break;
    case SHIP_PROTOCOL_KEY_E_RESERVED:
        name = "E-reserved";
        break;
    case SHIP_PROTOCOL_KEY_NONE:
        name = "none";
        break;
    default:
        name = "unknown";
        break;
    }

    key_action = ship_protocol_key_to_action(key);
    ship_protocol_rt.event.throttle.lr = ship_protocol_rt.lr;
    ship_protocol_rt.event.throttle.ud = ship_protocol_rt.ud;
    ship_protocol_rt.event.throttle.key = ship_protocol_rt.key;
    ship_protocol_rt.event.throttle.key_event = key;
    ship_protocol_rt.event.throttle.throttle_input =
        ship_protocol_raw_ud_to_input(ship_protocol_rt.ud);
    ship_protocol_rt.event.throttle.steering_input =
        ship_protocol_raw_lr_to_input(ship_protocol_rt.lr);
    ship_protocol_clear_event_payload();
    ship_protocol_clear_point_event();
    ship_protocol_rt.event.key_action = key_action;
    if (key_action != SHIP_PROTOCOL_KEY_ACTION_NONE) {
        ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_KEY_ACTION,
                                    SHIP_PROTOCOL_EVENT_STATE_KEY_ACTION,
                                    SHIP_CMD_THROTTLE,
                                    3U);
    } else {
        ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_KEY_EDGE,
                                    SHIP_PROTOCOL_EVENT_STATE_KEY_EDGE,
                                    SHIP_CMD_THROTTLE,
                                    3U);
    }
    LOGI(SHIP_TAG, "key edge key=0x%02X action=%s", (u16)key, name);

    if (key != SHIP_PROTOCOL_KEY_E_RESERVED) {
        return;
    }

    throttle_input = ship_protocol_raw_ud_to_input(ship_protocol_rt.ud);
    steering_input = ship_protocol_raw_lr_to_input(ship_protocol_rt.lr);
    yaw_rate_dps = 0;
    if (app_get_attitude_state() != 0) {
        yaw_rate_dps = (int16)(app_get_attitude_state()->gyro_z_dps100 / 100);
    }

    if (ShipControl_GetMode() == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) {
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_CRUISE_KEY);
        LOGI("DATA", "cruise exit reason=key-toggle input=%d raw_ud=%u",
             throttle_input,
             (u16)ship_protocol_rt.ud);
        return;
    }

    if ((throttle_input >= SHIP_CRUISE_KEY_START_INPUT) &&
        (steering_input <= SHIP_CRUISE_STEER_START_MAX) &&
        (steering_input >= (int16)(-SHIP_CRUISE_STEER_START_MAX)) &&
        (yaw_rate_dps <= SHIP_CRUISE_GYRO_START_MAX_DPS) &&
        (yaw_rate_dps >= (int16)(-SHIP_CRUISE_GYRO_START_MAX_DPS))) {
        if (app_get_heading_ready() != 0U) {
            heading_cd = app_get_heading_deg100();
            ShipControl_RequestCruise(heading_cd, SHIP_CRUISE_KEY_SPEED);
            LOGI("DATA", "cruise enter input=%d raw_ud=%u speed=%d hd=%u",
                 throttle_input,
                 (u16)ship_protocol_rt.ud,
                 (int16)SHIP_CRUISE_KEY_SPEED,
                 heading_cd);
        } else {
            ShipControl_Stop(SHIP_CONTROL_STOP_REASON_HEADING_LOST);
            LOGW(SHIP_TAG, "cruise reject heading-not-ready");
        }
    } else {
        LOGW(SHIP_TAG, "cruise reject input=%d steer=%d yaw=%d",
             throttle_input,
             steering_input,
             yaw_rate_dps);
    }
}

static void ship_protocol_handle_pair_rsp(const u8 *payload, u8 payload_len)
{
    if (ship_protocol_rt.pair_wait_rsp_ticks == 0U) {
        LOGW(SHIP_TAG, "pair rsp ignored outside window len=%u", (u16)payload_len);
        return;
    }

    ship_protocol_rt.pair_wait_rsp_ticks = 0U;
    ship_protocol_rt.pair_wait_start_ms = 0UL;
    ship_protocol_rt.last_proto_rx_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.paired = 1U;
    ship_protocol_rt.pair_left = 0U;
    ship_protocol_rt.wait_ticks = 0U;
    ship_protocol_rt.state = SHIP_PROTOCOL_STATE_WORK_RX;
    ship_protocol_rt.work_rx_configured = 1U;
    ship_protocol_rt.work_state_logged = 0U;
    ship_protocol_rt.pair_rsp_timeout_logged = 0U;

    LOGI(SHIP_TAG, "pair ok, enter work channel rx_ch=%u tx_ch=%u",
         (u16)ship_protocol_rt.rf_channel[0],
         (u16)ship_protocol_rt.rf_channel[2]);
    if ((payload != 0) && (payload_len == 4U)) {
        LOGI(SHIP_TAG,
             "pair success paired=1 work_rx=%u work_tx=%u key=%u/%u rsp=%02X%02X%02X%02X",
             (u16)ship_protocol_rt.rf_channel[0],
             (u16)ship_protocol_rt.rf_channel[2],
             (u16)ship_protocol_rt.rf_send_key[0],
             (u16)ship_protocol_rt.rf_send_key[1],
             (u16)payload[0],
             (u16)payload[1],
             (u16)payload[2],
             (u16)payload[3]);
    } else {
        LOGI(SHIP_TAG,
             "pair success paired=1 work_rx=%u work_tx=%u key=%u/%u rsp_len=%u",
             (u16)ship_protocol_rt.rf_channel[0],
             (u16)ship_protocol_rt.rf_channel[2],
             (u16)ship_protocol_rt.rf_send_key[0],
             (u16)ship_protocol_rt.rf_send_key[1],
             (u16)payload_len);
    }
}

static void ship_protocol_handle_throttle(const u8 *payload, u8 payload_len)
{
    u32 now_ms;

    if (payload_len < 3U) {
        LOGW(SHIP_TAG, "throttle short len=%u", (u16)payload_len);
        ship_protocol_publish_error_event(SHIP_CMD_THROTTLE, payload_len);
        return;
    }

    ship_protocol_rt.lr = payload[0];
    ship_protocol_rt.ud = payload[1];
    ship_protocol_rt.key = payload[2];
    ship_protocol_rt.valid = 1U;
    ship_protocol_rt.last_throttle_rx_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.throttle_recover_done = 0U;
    ship_protocol_rt.throttle_online = 1U;
    now_ms = platform_scheduler_get_tick_ms();

    ship_protocol_rt.event.throttle.lr = ship_protocol_rt.lr;
    ship_protocol_rt.event.throttle.ud = ship_protocol_rt.ud;
    ship_protocol_rt.event.throttle.key = ship_protocol_rt.key;
    ship_protocol_rt.event.throttle.throttle_input =
        (int16)((int16)ship_protocol_rt.ud - (int16)SHIP_AXIS_CENTER);
    ship_protocol_rt.event.throttle.steering_input =
        (int16)((int16)ship_protocol_rt.lr - (int16)SHIP_AXIS_CENTER);
    ship_protocol_rt.event.throttle.key_changed =
        (ship_protocol_rt.key != ship_protocol_rt.last_key) ? 1U : 0U;

    ship_protocol_clear_event_payload();
    ship_protocol_clear_point_event();
    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_THROTTLE,
                                SHIP_PROTOCOL_EVENT_STATE_THROTTLE_ACTIVE,
                                SHIP_CMD_THROTTLE,
                                payload_len);

    LOGI(SHIP_TAG, "rc cmd=0x11 lr=%u ud=%u tv=%d sv=%d key=0x%02X",
         (u16)ship_protocol_rt.lr,
         (u16)ship_protocol_rt.ud,
         ship_protocol_rt.event.throttle.throttle_input,
         ship_protocol_rt.event.throttle.steering_input,
         (u16)ship_protocol_rt.key);

    if ((now_ms < SHIP_MANUAL_BOOT_BLOCK_MS) ||
        ((SHIP_MANUAL_BOOT_WAIT_HEADING != 0U) && (app_get_heading_ready() == 0U))) {
        if (ship_protocol_rt.manual_boot_block_logged == 0U) {
            ship_protocol_rt.manual_boot_block_logged = 1U;
            LOGW(SHIP_TAG, "manual boot block wait=%lums hd=%u",
                 (now_ms < SHIP_MANUAL_BOOT_BLOCK_MS) ?
                 (u32)(SHIP_MANUAL_BOOT_BLOCK_MS - now_ms) : 0UL,
                 (u16)app_get_heading_ready());
        }
        AutoDrive_LinkAliveKick();
        return;
    }
    if (ship_protocol_rt.manual_boot_ready_logged == 0U) {
        ship_protocol_rt.manual_boot_ready_logged = 1U;
        LOGI(SHIP_TAG, "manual boot ready t=%lums hd=%u",
             now_ms,
             (u16)app_get_heading_ready());
    }

    if (ship_protocol_rt.key != ship_protocol_rt.last_key) {
        ship_protocol_handle_key_edge(ship_protocol_rt.key);
        ship_protocol_rt.last_key = ship_protocol_rt.key;
    }

    if (AutoDrive_IsBusy() != 0U) {
        AutoDrive_LinkAliveKick();
        return;
    }

    if ((ShipControl_GetMode() == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) &&
        (ship_protocol_raw_ud_to_input(ship_protocol_rt.ud) <= SHIP_CRUISE_KEY_STOP_INPUT)) {
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_CRUISE_KEY);
        LOGI("DATA", "cruise exit reason=throttle input=%d raw_ud=%u stop_th=%d",
             ship_protocol_raw_ud_to_input(ship_protocol_rt.ud),
             (u16)ship_protocol_rt.ud,
             (int16)SHIP_CRUISE_KEY_STOP_INPUT);
        AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
        AutoDrive_LinkAliveKick();
        return;
    }

    ShipControl_UpdateManualInput(ship_protocol_rt.lr,
                                  ship_protocol_rt.ud,
                                  ship_protocol_rt.key,
                                  now_ms);
    AutoDrive_LinkAliveKick();
}

static void ship_protocol_handle_return_home(const u8 *payload, u8 payload_len)
{
    if ((payload == 0) || (payload_len < SHIP_PROTOCOL_POINT_PAYLOAD_LEN)) {
        LOGW(SHIP_TAG, "0x13 return-home short len=%u", (u16)payload_len);
        ship_protocol_publish_error_event(SHIP_CMD_RETURN_HOME, payload_len);
        return;
    }

    ship_protocol_clear_event_payload();
    ship_protocol_parse_point_payload(payload, &ship_protocol_rt.event.point);
    ship_protocol_rt.event.switch_state = 0U;
    ship_protocol_rt.event.point_valid = 1U;
    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_RETURN_HOME,
                                SHIP_PROTOCOL_EVENT_STATE_RETURN_HOME_PENDING,
                                SHIP_CMD_RETURN_HOME,
                                payload_len);
    ship_protocol_log_point("0x13 return-home event", &ship_protocol_rt.event.point);
    AutoDrive_SetReturnPositionRaw(payload);
}

static u8 ship_protocol_handle_fish_point(const u8 *payload,
                                          u8 payload_len,
                                          const u8 *frame,
                                          u8 frame_len,
                                          u8 xor_calc,
                                          u8 xor_recv)
{
    u8 result;

    if ((payload == 0) || (payload_len < SHIP_PROTOCOL_POINT_PAYLOAD_LEN)) {
        LOGW(SHIP_TAG, "0x14 fish-point short len=%u", (u16)payload_len);
        ship_protocol_publish_error_event(SHIP_CMD_GOTO_POINT, payload_len);
        ship_protocol_log_goto_point(frame,
                                     frame_len,
                                     payload,
                                     payload_len,
                                     xor_calc,
                                     xor_recv,
                                     AUTODRIVE_FISH_CMD_INVALID);
        return AUTODRIVE_FISH_CMD_INVALID;
    }

    ship_protocol_clear_event_payload();
    ship_protocol_parse_point_payload(payload, &ship_protocol_rt.event.point);
    ship_protocol_rt.event.switch_state = 0U;
    ship_protocol_rt.event.point_valid = 1U;
    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_FISH_POINT,
                                SHIP_PROTOCOL_EVENT_STATE_FISH_POINT_PENDING,
                                SHIP_CMD_GOTO_POINT,
                                payload_len);
    ship_protocol_log_point("0x14 fish-point event", &ship_protocol_rt.event.point);
    result = AutoDrive_SetFishPositionRaw(payload);
    ship_protocol_log_goto_point(frame,
                                 frame_len,
                                 payload,
                                 payload_len,
                                 xor_calc,
                                 xor_recv,
                                 result);
    return result;
}

static void ship_protocol_handle_return_switch(const u8 *payload, u8 payload_len)
{
    if ((payload == 0) || (payload_len < 1U)) {
        LOGW(SHIP_TAG, "0x15 return-switch short len=%u", (u16)payload_len);
        ship_protocol_publish_error_event(SHIP_CMD_RETURN_SWITCH, payload_len);
        return;
    }

    ship_protocol_clear_event_payload();
    ship_protocol_rt.event.switch_state = payload[0];
    ship_protocol_clear_point_event();
    if (payload_len >= (u8)(1U + SHIP_PROTOCOL_POINT_PAYLOAD_LEN)) {
        ship_protocol_parse_point_payload(&payload[1], &ship_protocol_rt.event.point);
        ship_protocol_rt.event.point_valid = 1U;
    }

    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_RETURN_SWITCH,
                                SHIP_PROTOCOL_EVENT_STATE_RETURN_SWITCH_PENDING,
                                SHIP_CMD_RETURN_SWITCH,
                                payload_len);
    LOGI(SHIP_TAG, "0x15 return-switch event state=0x%02X point=%u len=%u",
         (u16)ship_protocol_rt.event.switch_state,
         (u16)ship_protocol_rt.event.point_valid,
         (u16)payload_len);
    if (ship_protocol_rt.event.point_valid != 0U) {
        ship_protocol_log_point("0x15 return-switch point", &ship_protocol_rt.event.point);
    }
    AutoDrive_SetSwitchRaw(payload, payload_len);
}

static int8 ship_protocol_parse_frame(const u8 *frame, u8 frame_len)
{
    u8 body_len;
    u8 payload_len;
    u8 cmd;
    u8 checksum;
    const u8 *payload;

    if ((frame == 0) || (frame_len < 5U)) {
        return BOARD_WIRELESS_ERR_PARAM;
    }
    if ((frame[0] != SHIP_PROTO_HEAD) || (frame[frame_len - 1U] != SHIP_PROTO_TAIL)) {
        LOGW(SHIP_TAG, "bad frame edge len=%u", (u16)frame_len);
        return BOARD_WIRELESS_ERR_VERIFY;
    }

    body_len = frame[1];
    if ((u8)(body_len + 3U) != frame_len) {
        LOGW(SHIP_TAG, "bad len field=%u frame=%u", (u16)body_len, (u16)frame_len);
        return BOARD_WIRELESS_ERR_VERIFY;
    }
    if (body_len < 2U) {
        return BOARD_WIRELESS_ERR_VERIFY;
    }

    payload_len = (u8)(body_len - 2U);
    checksum = ship_protocol_xor(&frame[1], (u8)(1U + 1U + payload_len));
    if (checksum != frame[frame_len - 2U]) {
        LOGW(SHIP_TAG, "bad xor cmd=0x%02X calc=0x%02X got=0x%02X",
             (u16)frame[2],
             (u16)checksum,
             (u16)frame[frame_len - 2U]);
        return BOARD_WIRELESS_ERR_VERIFY;
    }

    cmd = frame[2];
    payload = &frame[3];
    if (cmd == SHIP_CMD_PAIR_RSP) {
        ship_protocol_handle_pair_rsp(payload, payload_len);
        ship_protocol_send_gps_once();
        return BOARD_WIRELESS_OK;
    }

    ship_protocol_mark_proto_activity(cmd);

    switch (cmd) {
    case SHIP_CMD_THROTTLE:
        ship_protocol_handle_throttle(payload, payload_len);
        break;
    case SHIP_CMD_RETURN_HOME:
        ship_protocol_handle_return_home(payload, payload_len);
        break;
    case SHIP_CMD_GOTO_POINT:
        (void)ship_protocol_handle_fish_point(payload,
                                              payload_len,
                                              frame,
                                              frame_len,
                                              checksum,
                                              frame[frame_len - 2U]);
        break;
    case SHIP_CMD_RETURN_SWITCH:
        ship_protocol_handle_return_switch(payload, payload_len);
        break;
    case SHIP_CMD_GPS_REPORT:
        LOGI(SHIP_TAG, "cmd=0x12 rx ignored len=%u", (u16)payload_len);
        break;
    default:
        LOGW(SHIP_TAG, "unknown cmd=0x%02X len=%u", (u16)cmd, (u16)payload_len);
        break;
    }

    ship_protocol_send_gps_once();
    return BOARD_WIRELESS_OK;
}

static void ship_protocol_consume_byte(u8 value)
{
    u8 frame_len;

    if (ship_protocol_parse_state == SHIP_PARSE_WAIT_HEAD) {
        if (value != SHIP_PROTO_HEAD) {
            return;
        }
        ship_protocol_parse_index = 0U;
        ship_protocol_parse_buffer[ship_protocol_parse_index++] = value;
        ship_protocol_parse_expected_len = 0U;
        ship_protocol_parse_state = SHIP_PARSE_READ_LEN;
        return;
    }

    ship_protocol_parse_buffer[ship_protocol_parse_index++] = value;
    if (ship_protocol_parse_state == SHIP_PARSE_READ_LEN) {
        ship_protocol_parse_expected_len = (u8)(value + 3U);
        if ((ship_protocol_parse_expected_len < 5U) ||
            (ship_protocol_parse_expected_len > SHIP_PROTO_MAX_FRAME_LEN)) {
            ship_protocol_parse_index = 0U;
            ship_protocol_parse_expected_len = 0U;
            ship_protocol_parse_state = SHIP_PARSE_WAIT_HEAD;
        } else {
            ship_protocol_parse_state = SHIP_PARSE_READ_BODY;
        }
        return;
    }

    if ((ship_protocol_parse_state == SHIP_PARSE_READ_BODY) &&
        (ship_protocol_parse_expected_len != 0U) &&
        (ship_protocol_parse_index >= ship_protocol_parse_expected_len)) {
        frame_len = ship_protocol_parse_index;
        ship_protocol_parse_state = SHIP_PARSE_DISPATCH;
        (void)ship_protocol_parse_frame(ship_protocol_parse_buffer, frame_len);
        ship_protocol_parse_index = 0U;
        ship_protocol_parse_expected_len = 0U;
        ship_protocol_parse_state = SHIP_PARSE_WAIT_HEAD;
    }

    if (ship_protocol_parse_index >= SHIP_PROTO_MAX_FRAME_LEN) {
        ship_protocol_parse_index = 0U;
        ship_protocol_parse_expected_len = 0U;
        ship_protocol_parse_state = SHIP_PARSE_WAIT_HEAD;
    }
}

static void ship_protocol_poll_rx_frames(void)
{
    u8 payload_len;
    u8 i;
    int8 ret;

    do {
        ret = board_wireless_receive(ship_protocol_rx_payload,
                                     BOARD_WIRELESS_MAX_PAYLOAD_LEN,
                                     &payload_len);
        if (ret == BOARD_WIRELESS_OK) {
            for (i = 0U; i < payload_len; i++) {
                ship_protocol_consume_byte(ship_protocol_rx_payload[i]);
            }
        }
    } while (ret == BOARD_WIRELESS_OK);
}

static void ship_protocol_step_pair_send(void)
{
    int8 ret;
    u16 left_after_send;

    if (ship_protocol_rt.paired != 0U) {
        ship_protocol_rt.pair_left = 0U;
        ship_protocol_rt.state = SHIP_PROTOCOL_STATE_WORK_RX;
        return;
    }

    if (ship_protocol_rt.pair_left > 0U) {
        ship_protocol_rt.wait_ticks = SHIP_WAIT_TICKS_DEFAULT;
        left_after_send = (u16)(ship_protocol_rt.pair_left - 1U);
        ret = ship_protocol_try_pair_send(left_after_send);
        if (ret != BOARD_WIRELESS_OK) {
            return;
        }
        ship_protocol_rt.pair_left = left_after_send;
        if (ship_protocol_rt.pair_left == 0U) {
            ret = ship_protocol_arm_pair_rsp_window();
            if (ret != BOARD_WIRELESS_OK) {
                LOGE(SHIP_TAG, "pair rsp window arm fail rc=%d", ret);
                ship_protocol_rt.pair_retry_count++;
                ship_protocol_rt.pair_left = SHIP_PAIR_SEND_TIMES;
                ship_protocol_rt.wait_ticks = SHIP_WAIT_TICKS_DEFAULT;
                ship_protocol_rt.pair_wait_rsp_ticks = 0U;
                ship_protocol_rt.pair_wait_start_ms = 0UL;
                return;
            }
            ship_protocol_rt.state = SHIP_PROTOCOL_STATE_PAIR_WAIT_RSP;
            LOGI(SHIP_TAG, "pair req burst done, enter rsp wait on work-rx");
        }
    }
}

static void ship_protocol_step_work_rx(void)
{
    int8 ret;

    if (ship_protocol_rt.work_rx_configured == 0U) {
        ret = ship_protocol_apply_work_rx();
        if (ret != BOARD_WIRELESS_OK) {
            LOGE(SHIP_TAG, "enter work rx fail rc=%d", ret);
            return;
        }
        if (ship_protocol_rt.last_proto_rx_ms == 0UL) {
            ship_protocol_rt.last_proto_rx_ms = ship_protocol_rt.tick_ms;
        }
    }

    if (ship_protocol_rt.work_state_logged == 0U) {
        ship_protocol_rt.work_state_logged = 1U;
        LOGI(SHIP_TAG, "enter work-state rx_ch=%u tx_ch=%u",
             (u16)ship_protocol_rt.rf_channel[0],
             (u16)ship_protocol_rt.rf_channel[2]);
    } else {
        ship_protocol_rt.work_rx_reopen_ticks++;
        if (ship_protocol_rt.work_rx_reopen_ticks > SHIP_WORK_RX_REOPEN_TICKS) {
            ret = ship_protocol_apply_work_rx();
            if (ret == BOARD_WIRELESS_OK) {
                ship_protocol_rt.work_rx_reopen_total++;
            } else {
                LOGE(SHIP_TAG, "periodic work rx reopen fail rc=%d", ret);
            }
        }
    }
}

static void ship_protocol_check_timeouts(void)
{
    u32 rx_silence_ms;
    u32 throttle_silence_ms;

    if (ship_protocol_rt.pair_wait_rsp_ticks > 0U) {
        ship_protocol_rt.pair_wait_rsp_ticks--;
    } else if ((ship_protocol_rt.pair_wait_start_ms != 0UL) &&
               (ship_protocol_rt.pair_rsp_timeout_logged == 0U) &&
               ((ship_protocol_rt.tick_ms - ship_protocol_rt.pair_wait_start_ms) >= SHIP_PAIR_RSP_EXPIRE_LOG_MS) &&
               (ship_protocol_rt.paired == 0U)) {
        ship_protocol_rt.pair_rsp_timeout_logged = 1U;
        LOGW(SHIP_TAG, "pair rsp window expired, no rsp");
    }

    if ((ship_protocol_rt.state == SHIP_PROTOCOL_STATE_WORK_RX) ||
        (ship_protocol_rt.state == SHIP_PROTOCOL_STATE_PAIR_WAIT_RSP)) {
        rx_silence_ms = ship_protocol_rt.tick_ms - ship_protocol_rt.last_proto_rx_ms;
        if ((ship_protocol_rt.last_proto_rx_ms != 0UL) &&
            (ship_protocol_rt.rx_idle_warned == 0U) &&
            (rx_silence_ms >= SHIP_RX_IDLE_WARN_MS)) {
            ship_protocol_rt.rx_idle_warned = 1U;
            LOGW(SHIP_TAG, "work-rx idle %lums, no aa-bb frame", rx_silence_ms);
        }

        if ((ship_protocol_rt.remote_online != 0U) &&
            (rx_silence_ms >= SHIP_THROTTLE_TIMEOUT_MS)) {
            ship_protocol_rt.remote_online = 0U;
            if (ship_protocol_rt.throttle_online != 0U) {
                ship_protocol_rt.throttle_online = 0U;
                ShipControl_Stop(SHIP_CONTROL_STOP_REASON_REMOTE_TIMEOUT);
            }
            LOGW(SHIP_TAG, "remote link timeout by aa-bb-frame, dt=%lums", rx_silence_ms);
        }

        if ((ship_protocol_rt.paired != 0U) &&
            (ship_protocol_rt.throttle_recover_done == 0U) &&
            (ship_protocol_rt.last_proto_rx_ms != 0UL) &&
            (rx_silence_ms >= SHIP_THROTTLE_RECOVER_MS)) {
            ship_protocol_rt.throttle_recover_done = 1U;
            ship_protocol_apply_default_rf();
            ship_protocol_rt.work_rx_reopen_ticks = 0U;
            LOGW(SHIP_TAG, "legacy remote recovery after %lums link silence", rx_silence_ms);
            (void)ship_protocol_apply_work_rx();
        }
    }

    if (ship_protocol_rt.throttle_online != 0U) {
        throttle_silence_ms = ship_protocol_rt.tick_ms - ship_protocol_rt.last_throttle_rx_ms;
        if (throttle_silence_ms >= SHIP_THROTTLE_TIMEOUT_MS) {
            ship_protocol_rt.throttle_online = 0U;
            LOGW(SHIP_TAG, "manual control timeout by cmd=0x11, dt=%lums", throttle_silence_ms);
            ShipControl_Stop(SHIP_CONTROL_STOP_REASON_MANUAL_TIMEOUT);
        }
    }
}

void ship_protocol_init(void)
{
    u8 seed[4];

    ship_protocol_get_pair_seed(seed);
    ship_protocol_apply_default_rf();

    ship_protocol_rt.lr = SHIP_AXIS_CENTER;
    ship_protocol_rt.ud = SHIP_AXIS_CENTER;
    ship_protocol_rt.key = SHIP_KEY_NULL;
    ship_protocol_rt.last_key = SHIP_KEY_NULL;
    ship_protocol_rt.valid = 0U;
    ship_protocol_rt.paired = 0U;
    ship_protocol_rt.work_rx_configured = 0U;
    ship_protocol_rt.work_state_logged = 0U;
    ship_protocol_rt.state = SHIP_PROTOCOL_STATE_BOOT_WAIT;
    ship_protocol_rt.pair_wait_rsp_ticks = 0U;
    ship_protocol_rt.wait_ticks = SHIP_WAIT_TICKS_DEFAULT;
    ship_protocol_rt.pair_left = SHIP_PAIR_SEND_TIMES;
    ship_protocol_rt.pair_retry_count = 0U;
    ship_protocol_rt.work_rx_reopen_ticks = 0U;
    ship_protocol_rt.work_rx_reopen_total = 0U;
    ship_protocol_rt.tick_ms = 0UL;
    ship_protocol_rt.pair_wait_start_ms = 0UL;
    ship_protocol_rt.last_proto_rx_ms = 0UL;
    ship_protocol_rt.last_throttle_rx_ms = 0UL;
    ship_protocol_rt.pair_rsp_timeout_logged = 0U;
    ship_protocol_rt.rx_idle_warned = 0U;
    ship_protocol_rt.remote_online = 0U;
    ship_protocol_rt.throttle_online = 0U;
    ship_protocol_rt.throttle_recover_done = 0U;
    ship_protocol_rt.manual_boot_block_logged = 0U;
    ship_protocol_rt.manual_boot_ready_logged = 0U;
    ship_protocol_rt.power_level = SHIP_POWER_LEVEL_0;
    ship_protocol_rt.power_adc_ready = 0U;
    ship_protocol_rt.lowpower_return_latched = 0U;
    ship_protocol_rt.power_sample_divider_count = 0U;
    ship_protocol_rt.lowpower_check_ticks = 0U;
    ship_protocol_rt.power_sample_period_ms = (u32)SHIP_POWER_SAMPLE_DIVIDER * 10UL;
    ship_protocol_rt.power_sample.raw = 0U;
    ship_protocol_rt.power_sample.adc_mv = 0U;
    ship_protocol_rt.power_sample.bat_mv = 0UL;
    ship_protocol_rt.power_sample.report = SHIP_POWER_LEVEL_0;
    ship_protocol_rt.power_sample.valid = 0U;
    ship_protocol_rt.event.type = SHIP_PROTOCOL_EVENT_NONE;
    ship_protocol_rt.event.state = SHIP_PROTOCOL_EVENT_STATE_IDLE;
    ship_protocol_rt.event.cmd = 0U;
    ship_protocol_rt.event.payload_len = 0U;
    ship_protocol_rt.event.switch_state = 0U;
    ship_protocol_rt.event.point_valid = 0U;
    ship_protocol_rt.event.pending = 0U;
    ship_protocol_rt.event.sequence = 0U;
    ship_protocol_rt.event.tick_ms = 0UL;
    ship_protocol_rt.event.throttle.lr = SHIP_AXIS_CENTER;
    ship_protocol_rt.event.throttle.ud = SHIP_AXIS_CENTER;
    ship_protocol_rt.event.throttle.key = SHIP_KEY_NULL;
    ship_protocol_rt.event.throttle.key_changed = 0U;
    ship_protocol_rt.event.throttle.key_event = SHIP_KEY_NULL;
    ship_protocol_rt.event.throttle.throttle_input = 0;
    ship_protocol_rt.event.throttle.steering_input = 0;
    ship_protocol_clear_event_payload();
    ship_protocol_clear_point_event();
    ship_protocol_event_queue_reset();

    ship_protocol_parse_index = 0U;
    ship_protocol_parse_expected_len = 0U;
    ship_protocol_parse_state = SHIP_PARSE_WAIT_HEAD;
    ship_protocol_power_init();
    ship_protocol_read_power_sample(&ship_protocol_rt.power_sample);
    if (ship_protocol_rt.power_sample.valid != 0U) {
        ship_protocol_rt.power_level = ship_protocol_rt.power_sample.report;
    }
    ShipControl_Init();
    AutoDrive_Init();
    ship_protocol_initialized = 1U;

    LOGI(SHIP_TAG, "scheduler init wait=%u pair_send=%u pair_ch=0x%02X seed=%02X%02X%02X%02X",
         (u16)ship_protocol_rt.wait_ticks,
         (u16)ship_protocol_rt.pair_left,
         (u16)SHIP_PAIR_CHANNEL_DEFAULT,
         (u16)seed[0],
         (u16)seed[1],
         (u16)seed[2],
         (u16)seed[3]);
}

void ship_protocol_run_scheduler(void)
{
    u8 step_link_state;

    if (ship_protocol_initialized == 0U) {
        ship_protocol_init();
    }

    ship_protocol_poll_rx_frames();
    ship_protocol_rt.tick_ms += 10UL;
    AutoDrive_LinkAliveTick();
    ship_protocol_low_power_check();
    ship_protocol_check_timeouts();

    step_link_state = 1U;
    if (ship_protocol_rt.wait_ticks > 0U) {
        ship_protocol_rt.wait_ticks--;
        step_link_state = 0U;
    }

    if (step_link_state != 0U) {
        if (ship_protocol_rt.state == SHIP_PROTOCOL_STATE_BOOT_WAIT) {
            ship_protocol_rt.state = SHIP_PROTOCOL_STATE_PAIR_SEND;
        }

        switch (ship_protocol_rt.state) {
        case SHIP_PROTOCOL_STATE_PAIR_SEND:
            ship_protocol_step_pair_send();
            break;
        case SHIP_PROTOCOL_STATE_PAIR_WAIT_RSP:
            ship_protocol_step_work_rx();
            break;
        case SHIP_PROTOCOL_STATE_WORK_RX:
            ship_protocol_step_work_rx();
            break;
        default:
            ship_protocol_rt.state = SHIP_PROTOCOL_STATE_PAIR_SEND;
            break;
        }
    }

    AutoDrive_Poll();
    ship_protocol_service_autodrive_diag(platform_scheduler_get_tick_ms());
    ShipControl_Tick(platform_scheduler_get_tick_ms());
}

u8 ship_protocol_is_paired(void)
{
    return ship_protocol_rt.paired;
}

void ship_protocol_publish_spi_ps_frame(const u8 *buffer, u8 len, int8 status)
{
    u8 i;
    u8 stored_len;

    stored_len = len;
    if (stored_len > SHIP_PROTOCOL_SPI_PS_EVENT_DATA_MAX) {
        stored_len = SHIP_PROTOCOL_SPI_PS_EVENT_DATA_MAX;
    }

    ship_protocol_clear_event_payload();
    ship_protocol_rt.event.spi_ps.status = status;
    ship_protocol_rt.event.spi_ps.len = len;
    ship_protocol_rt.event.spi_ps.stored_len = stored_len;
    for (i = 0U; i < stored_len; i++) {
        ship_protocol_rt.event.spi_ps.bytes[i] =
            (buffer != 0) ? buffer[i] : 0U;
    }

    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX,
                                SHIP_PROTOCOL_EVENT_STATE_SPI_PS,
                                0U,
                                len);
}

u8 ship_protocol_get_event_snapshot(ship_protocol_event_snapshot_t *snapshot)
{
    if (snapshot == 0) {
        return 0U;
    }

    *snapshot = ship_protocol_rt.event;
    return ship_protocol_rt.event.pending;
}

u8 ship_protocol_take_event(ship_protocol_event_snapshot_t *snapshot)
{
    if ((snapshot == 0) || (ship_protocol_rt.event_queue_count == 0U)) {
        return 0U;
    }

    *snapshot = ship_protocol_rt.event_queue[ship_protocol_rt.event_queue_head];
    ship_protocol_rt.event_queue[ship_protocol_rt.event_queue_head].pending = 0U;
    ship_protocol_rt.event_queue_head++;
    if (ship_protocol_rt.event_queue_head >= SHIP_PROTOCOL_EVENT_QUEUE_DEPTH) {
        ship_protocol_rt.event_queue_head = 0U;
    }
    ship_protocol_rt.event_queue_count--;

    if (ship_protocol_rt.event_queue_count == 0U) {
        ship_protocol_rt.event.pending = 0U;
        ship_protocol_rt.event.state = SHIP_PROTOCOL_EVENT_STATE_IDLE;
        ship_protocol_rt.event.type = SHIP_PROTOCOL_EVENT_NONE;
        ship_protocol_rt.event.cmd = 0U;
        ship_protocol_rt.event.payload_len = 0U;
    }
    return 1U;
}
