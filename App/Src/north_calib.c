#include "north_calib.h"

#include "app.h"
#include "app_config.h"
#include "autodrive.h"
#include "board_gps.h"
#include "logger.h"
#include "parameter_store.h"
#include "platform_scheduler.h"
#include "ship_control.h"

#define NCAL_TAG                         "NCAL"

#define NCAL_REMOTE_TIMEOUT_MS           SHIP_THROTTLE_TIMEOUT_MS
#define NCAL_TOTAL_TIMEOUT_MS            60000UL
#define NCAL_ALIGN_TIMEOUT_MS            20000UL
#define NCAL_RUN_TIMEOUT_MS              30000UL
#define NCAL_LOG_PERIOD_MS               1000UL

#define NCAL_TARGET_HEADING_CD           0U
#define NCAL_RUN_BASE_SPEED              500
#define NCAL_TARGET_DISTANCE_M           10U
#define NCAL_MIN_SAVE_DISTANCE_M         8U
#define NCAL_MIN_SATELLITES              7U
#define NCAL_ALIGN_EXIT_ERROR_CD         500
#define NCAL_YAW_UNSTABLE_DPS            45
#define NCAL_YAW_UNSTABLE_LIMIT_TICKS    80U
#define NCAL_HEADING_ERR_LIMIT_CD        1500
#define NCAL_HEADING_ERR_LIMIT_TICKS     120U
#define NCAL_OFFSET_SAVE_JUMP_LIMIT_CD   4500

#define NCAL_RECORD_MAGIC                0x4E43414CUL
#define NCAL_RECORD_VERSION              1U
#define NCAL_RECORD_SIZE                 16U
#define NCAL_SLOT_A                      0U
#define NCAL_SLOT_B                      1U

typedef enum
{
    NCAL_STATE_IDLE = 0,
    NCAL_STATE_CHECK_READY,
    NCAL_STATE_ALIGN_NORTH,
    NCAL_STATE_RUN_STRAIGHT,
    NCAL_STATE_CALC,
    NCAL_STATE_SAVE,
    NCAL_STATE_DONE,
    NCAL_STATE_FAILED
} NorthCalib_State_t;

typedef struct
{
    u32 magic;
    u8 version;
    int16 north_offset_cd;
    u8 confidence;
    u16 sample_distance_m;
    u16 update_count;
    u16 checksum;
} NorthCalib_Record_t;

typedef struct
{
    u8 initialized;
    u8 state;
    u8 fail_reason;
    u8 run_start_reason;
    u8 lr;
    u8 ud;
    u8 key;
    u32 last_remote_ms;
    u32 active_start_ms;
    u32 state_start_ms;
    u32 last_log_ms;
    int32 start_lat;
    int32 start_lon;
    int32 end_lat;
    int32 end_lon;
    u32 last_sample_seq;
    u16 run_target_heading_cd;
    int16 heading_ref_cd;
    int32 heading_sum_cd;
    u16 heading_samples;
    u16 run_distance_m;
    u16 yaw_unstable_ticks;
    u16 heading_err_bad_ticks;
    int16 active_offset_cd;
    int16 pending_offset_cd;
    u8 has_saved_record;
    u8 active_record_slot;
    u16 update_count;
} NorthCalib_Runtime_t;

static NorthCalib_Runtime_t g_ncal;

static void NorthCalib_EnterState(u8 state);
static void NorthCalib_BeginRunStraight(const board_gps_state_t *gps,
                                        u16 target_heading_cd,
                                        u8 reason);
static u8 NorthCalib_GpsReady(const board_gps_state_t *gps);
static u8 NorthCalib_RemoteOnline(u32 now_ms);
static u8 NorthCalib_ManualOverride(void);
static int16 NorthCalib_WrapSignedCd(int32 angle_cd);
static u16 NorthCalib_WrapUnsignedCd(int32 angle_cd);
static u16 NorthCalib_BearingDeg100(int32 lat_from, int32 lon_from,
                                    int32 lat_to, int32 lon_to);
static u16 NorthCalib_DistanceMeters(int32 lat_from, int32 lon_from,
                                     int32 lat_to, int32 lon_to);
static void NorthCalib_LoadRecord(void);
static u8 NorthCalib_LoadRecordFrom(u8 slot, NorthCalib_Record_t *record);
static u8 NorthCalib_RecordValid(const NorthCalib_Record_t *record);
static u8 NorthCalib_RecordNewer(u16 candidate_count, u16 current_count);
static u8 NorthCalib_NextSaveSlot(void);
static u8 NorthCalib_SaveRecord(int16 offset_cd, u8 confidence, u16 distance_m);
static u16 NorthCalib_RecordChecksum(const NorthCalib_Record_t *record);

void NorthCalib_Init(void)
{
    if (g_ncal.initialized != 0U) {
        return;
    }

    g_ncal.initialized = 1U;
    g_ncal.state = NCAL_STATE_IDLE;
    g_ncal.fail_reason = NORTH_CALIB_FAIL_NONE;
    g_ncal.run_start_reason = 0U;
    g_ncal.lr = SHIP_AXIS_CENTER;
    g_ncal.ud = SHIP_AXIS_CENTER;
    g_ncal.key = 0U;
    g_ncal.last_remote_ms = 0UL;
    g_ncal.active_start_ms = 0UL;
    g_ncal.state_start_ms = 0UL;
    g_ncal.last_log_ms = 0UL;
    g_ncal.active_offset_cd = 0;
    g_ncal.pending_offset_cd = 0;
    g_ncal.run_target_heading_cd = NCAL_TARGET_HEADING_CD;
    g_ncal.has_saved_record = 0U;
    g_ncal.active_record_slot = NCAL_SLOT_A;
    g_ncal.update_count = 0U;
    NorthCalib_LoadRecord();
}

void NorthCalib_UpdateRemoteInput(u8 lr, u8 ud, u8 key, u32 now_ms)
{
    if (g_ncal.initialized == 0U) {
        NorthCalib_Init();
    }

    g_ncal.lr = lr;
    g_ncal.ud = ud;
    g_ncal.key = key;
    g_ncal.last_remote_ms = now_ms;
}

u8 NorthCalib_RequestStart(void)
{
    if (g_ncal.initialized == 0U) {
        NorthCalib_Init();
    }
    if (g_ncal.state != NCAL_STATE_IDLE) {
        LOGW(NCAL_TAG, "start reject busy st=%u", (u16)g_ncal.state);
        return 0U;
    }

    NorthCalib_EnterState(NCAL_STATE_CHECK_READY);
    LOGI(NCAL_TAG, "start");
    return 1U;
}

void NorthCalib_Poll(void)
{
    const board_gps_state_t *gps;
    const AHRS_State_t *att;
    u32 now_ms;
    int16 heading_error_cd;
    int16 yaw_dps;
    int16 avg_heading_cd;
    int16 offset_cd;
    int16 old_offset_cd;
    int16 raw_heading_signed_cd;
    u16 raw_heading_cd;
    u16 gps_course_cd;
    u16 distance_m;
    u8 confidence;

    if (g_ncal.initialized == 0U) {
        NorthCalib_Init();
    }

    now_ms = platform_scheduler_get_tick_ms();
    gps = board_gps_get_state();

    if ((g_ncal.state != NCAL_STATE_IDLE) &&
        (g_ncal.state != NCAL_STATE_DONE) &&
        (g_ncal.state != NCAL_STATE_FAILED) &&
        (g_ncal.active_start_ms != 0UL) &&
        ((now_ms - g_ncal.active_start_ms) >= NCAL_TOTAL_TIMEOUT_MS)) {
        NorthCalib_Cancel(NORTH_CALIB_FAIL_TIMEOUT);
        return;
    }

    switch (g_ncal.state) {
    case NCAL_STATE_IDLE:
        return;

    case NCAL_STATE_CHECK_READY:
        if (NorthCalib_RemoteOnline(now_ms) == 0U) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_REMOTE_TIMEOUT);
            return;
        }
        if (NorthCalib_GpsReady(gps) == 0U) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_GPS_NOT_READY);
            return;
        }
        if (app_get_heading_ready() == 0U) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_HEADING_NOT_READY);
            return;
        }
        if ((AutoDrive_IsBusy() != 0U) || (ShipControl_IsAutoMode() != 0U)) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_AUTODRIVE_BUSY);
            return;
        }
        ShipControl_ResetYawHoldController();
        NorthCalib_EnterState(NCAL_STATE_ALIGN_NORTH);
        return;

    case NCAL_STATE_ALIGN_NORTH:
        if ((NorthCalib_RemoteOnline(now_ms) == 0U) ||
            (NorthCalib_ManualOverride() != 0U)) {
            NorthCalib_Cancel((NorthCalib_RemoteOnline(now_ms) == 0U) ?
                              NORTH_CALIB_FAIL_REMOTE_TIMEOUT :
                              NORTH_CALIB_FAIL_MANUAL_OVERRIDE);
            return;
        }
        if (NorthCalib_GpsReady(gps) == 0U) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_GPS_NOT_READY);
            return;
        }
        if (app_get_heading_ready() == 0U) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_HEADING_NOT_READY);
            return;
        }
        ShipControl_RequestGpsAlign(NCAL_TARGET_HEADING_CD);
        heading_error_cd =
            NorthCalib_WrapSignedCd((int32)NCAL_TARGET_HEADING_CD -
                                    (int32)app_get_heading_deg100());
        if ((now_ms - g_ncal.last_log_ms) >= NCAL_LOG_PERIOD_MS) {
            g_ncal.last_log_ms = now_ms;
            LOGI(NCAL_TAG, "align heading=%u err=%d",
                 app_get_heading_deg100(),
                 heading_error_cd);
        }
        if ((heading_error_cd <= (int16)NCAL_ALIGN_EXIT_ERROR_CD) &&
            (heading_error_cd >= (int16)(-NCAL_ALIGN_EXIT_ERROR_CD))) {
            NorthCalib_BeginRunStraight(gps, NCAL_TARGET_HEADING_CD, 0U);
            return;
        }
        if ((now_ms - g_ncal.state_start_ms) >= NCAL_ALIGN_TIMEOUT_MS) {
            LOGW(NCAL_TAG, "align timeout heading=%u err=%d",
                 app_get_heading_deg100(),
                 heading_error_cd);
            NorthCalib_BeginRunStraight(gps, app_get_heading_deg100(), 1U);
        }
        return;

    case NCAL_STATE_RUN_STRAIGHT:
        if ((NorthCalib_RemoteOnline(now_ms) == 0U) ||
            (NorthCalib_ManualOverride() != 0U)) {
            NorthCalib_Cancel((NorthCalib_RemoteOnline(now_ms) == 0U) ?
                              NORTH_CALIB_FAIL_REMOTE_TIMEOUT :
                              NORTH_CALIB_FAIL_MANUAL_OVERRIDE);
            return;
        }
        if (NorthCalib_GpsReady(gps) == 0U) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_GPS_NOT_READY);
            return;
        }
        if (app_get_heading_ready() == 0U) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_HEADING_NOT_READY);
            return;
        }

        ShipControl_RequestGpsNav(g_ncal.run_target_heading_cd, NCAL_RUN_BASE_SPEED);
        raw_heading_cd = app_get_raw_heading_deg100();
        raw_heading_signed_cd = NorthCalib_WrapSignedCd((int32)raw_heading_cd);
        if (g_ncal.heading_samples == 0U) {
            g_ncal.heading_ref_cd = raw_heading_signed_cd;
        }
        g_ncal.heading_sum_cd +=
            (int32)NorthCalib_WrapSignedCd((int32)raw_heading_signed_cd -
                                           (int32)g_ncal.heading_ref_cd);
        if (g_ncal.heading_samples < 65535U) {
            g_ncal.heading_samples++;
        }

        yaw_dps = 0;
        att = app_get_attitude_state();
        if (att != 0) {
            yaw_dps = (int16)(att->gyro_z_dps100 / 100);
        }
        if ((yaw_dps > (int16)NCAL_YAW_UNSTABLE_DPS) ||
            (yaw_dps < (int16)(-NCAL_YAW_UNSTABLE_DPS))) {
            g_ncal.yaw_unstable_ticks++;
        }

        heading_error_cd =
            NorthCalib_WrapSignedCd((int32)g_ncal.run_target_heading_cd -
                                    (int32)app_get_heading_deg100());
        if ((heading_error_cd > (int16)NCAL_HEADING_ERR_LIMIT_CD) ||
            (heading_error_cd < (int16)(-NCAL_HEADING_ERR_LIMIT_CD))) {
            g_ncal.heading_err_bad_ticks++;
        }

        if ((gps != 0) && (gps->update_sequence != g_ncal.last_sample_seq)) {
            g_ncal.last_sample_seq = gps->update_sequence;
            g_ncal.end_lat = gps->lat_deg1e7;
            g_ncal.end_lon = gps->lon_deg1e7;
            g_ncal.run_distance_m =
                NorthCalib_DistanceMeters(g_ncal.start_lat,
                                          g_ncal.start_lon,
                                          g_ncal.end_lat,
                                          g_ncal.end_lon);
        }
        if ((now_ms - g_ncal.last_log_ms) >= NCAL_LOG_PERIOD_MS) {
            g_ncal.last_log_ms = now_ms;
            LOGI(NCAL_TAG, "run dist=%u heading=%u tgt=%u gps=%ld/%ld",
                 g_ncal.run_distance_m,
                 app_get_heading_deg100(),
                 g_ncal.run_target_heading_cd,
                 (long)((gps != 0) ? gps->lat_deg1e7 : 0L),
                 (long)((gps != 0) ? gps->lon_deg1e7 : 0L));
        }
        if (g_ncal.yaw_unstable_ticks > NCAL_YAW_UNSTABLE_LIMIT_TICKS) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_YAW_UNSTABLE);
            return;
        }
        if (g_ncal.heading_err_bad_ticks > NCAL_HEADING_ERR_LIMIT_TICKS) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_YAW_UNSTABLE);
            return;
        }
        if (g_ncal.run_distance_m >= NCAL_TARGET_DISTANCE_M) {
            NorthCalib_EnterState(NCAL_STATE_CALC);
            return;
        }
        if ((now_ms - g_ncal.state_start_ms) >= NCAL_RUN_TIMEOUT_MS) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_TIMEOUT);
        }
        return;

    case NCAL_STATE_CALC:
        distance_m = g_ncal.run_distance_m;
        if ((distance_m < NCAL_MIN_SAVE_DISTANCE_M) ||
            (g_ncal.heading_samples == 0U)) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_DISTANCE_SHORT);
            return;
        }
        gps_course_cd = NorthCalib_BearingDeg100(g_ncal.start_lat,
                                                 g_ncal.start_lon,
                                                 g_ncal.end_lat,
                                                 g_ncal.end_lon);
        avg_heading_cd =
            NorthCalib_WrapSignedCd((int32)g_ncal.heading_ref_cd +
                                    (g_ncal.heading_sum_cd /
                                     (int32)g_ncal.heading_samples));
        offset_cd = NorthCalib_WrapSignedCd((int32)gps_course_cd -
                                            (int32)avg_heading_cd);
        old_offset_cd = g_ncal.active_offset_cd;
        LOGI(NCAL_TAG, "calc course=%u avg_heading=%d offset=%d old=%d",
             gps_course_cd,
             avg_heading_cd,
             offset_cd,
             old_offset_cd);
        heading_error_cd =
            NorthCalib_WrapSignedCd((int32)offset_cd - (int32)old_offset_cd);
        if ((g_ncal.has_saved_record != 0U) &&
            ((heading_error_cd > (int16)NCAL_OFFSET_SAVE_JUMP_LIMIT_CD) ||
             (heading_error_cd < (int16)(-NCAL_OFFSET_SAVE_JUMP_LIMIT_CD)))) {
            g_ncal.active_offset_cd = offset_cd;
            g_ncal.pending_offset_cd = offset_cd;
            LOGW(NCAL_TAG, "offset jump temp offset=%d old=%d",
                 offset_cd,
                 old_offset_cd);
            NorthCalib_Cancel(NORTH_CALIB_FAIL_OFFSET_JUMP);
            return;
        }
        g_ncal.pending_offset_cd = offset_cd;
        NorthCalib_EnterState(NCAL_STATE_SAVE);
        return;

    case NCAL_STATE_SAVE:
        distance_m = g_ncal.run_distance_m;
        confidence = (distance_m >= NCAL_TARGET_DISTANCE_M) ? 90U : 80U;
        if (NorthCalib_SaveRecord(g_ncal.pending_offset_cd,
                                  confidence,
                                  distance_m) == 0U) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_EEPROM);
            return;
        }
        g_ncal.active_offset_cd = g_ncal.pending_offset_cd;
        LOGI(NCAL_TAG, "save ok offset=%d conf=%u dist=%u",
             g_ncal.active_offset_cd,
             (u16)confidence,
             distance_m);
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_GPS_NAV_STOP);
        NorthCalib_EnterState(NCAL_STATE_DONE);
        return;

    case NCAL_STATE_DONE:
        NorthCalib_EnterState(NCAL_STATE_IDLE);
        return;

    case NCAL_STATE_FAILED:
        NorthCalib_EnterState(NCAL_STATE_IDLE);
        return;

    default:
        NorthCalib_Cancel(NORTH_CALIB_FAIL_TIMEOUT);
        return;
    }
}

void NorthCalib_Cancel(u8 reason)
{
    const board_gps_state_t *gps;
    u8 sat_count;

    if (g_ncal.initialized == 0U) {
        NorthCalib_Init();
    }
    if (g_ncal.state == NCAL_STATE_IDLE) {
        return;
    }

    gps = board_gps_get_state();
    sat_count = 0U;
    if (gps != 0) {
        sat_count = (gps->satellites_used_gsa > 0U) ?
                    gps->satellites_used_gsa :
                    gps->satellites_used;
    }

    g_ncal.fail_reason = reason;
    ShipControl_Stop(SHIP_CONTROL_STOP_REASON_GPS_NAV_STOP);
    LOGW(NCAL_TAG, "fail r=%u st=%u rs=%u lr=%u ud=%u hd=%u gps=%u sat=%u dist=%u tgt=%u",
         (u16)reason,
         (u16)g_ncal.state,
         (u16)g_ncal.run_start_reason,
         (u16)g_ncal.lr,
         (u16)g_ncal.ud,
         app_get_heading_deg100(),
         (u16)((gps != 0) ? gps->fix_valid : 0U),
         (u16)sat_count,
         g_ncal.run_distance_m,
         g_ncal.run_target_heading_cd);
    NorthCalib_EnterState(NCAL_STATE_FAILED);
}

u8 NorthCalib_IsBusy(void)
{
    if (g_ncal.initialized == 0U) {
        NorthCalib_Init();
    }
    return (g_ncal.state != NCAL_STATE_IDLE) ? 1U : 0U;
}

int16 NorthCalib_GetHeadingOffsetCd(void)
{
    if (g_ncal.initialized == 0U) {
        NorthCalib_Init();
    }
    return g_ncal.active_offset_cd;
}

static void NorthCalib_EnterState(u8 state)
{
    g_ncal.state = state;
    g_ncal.state_start_ms = platform_scheduler_get_tick_ms();
    g_ncal.last_log_ms = 0UL;
    if (state == NCAL_STATE_CHECK_READY) {
        g_ncal.active_start_ms = g_ncal.state_start_ms;
    } else if (state == NCAL_STATE_IDLE) {
        g_ncal.active_start_ms = 0UL;
    }
}

static void NorthCalib_BeginRunStraight(const board_gps_state_t *gps,
                                        u16 target_heading_cd,
                                        u8 reason)
{
    if (gps == 0) {
        NorthCalib_Cancel(NORTH_CALIB_FAIL_GPS_NOT_READY);
        return;
    }

    g_ncal.run_start_reason = reason;
    g_ncal.start_lat = gps->lat_deg1e7;
    g_ncal.start_lon = gps->lon_deg1e7;
    g_ncal.end_lat = gps->lat_deg1e7;
    g_ncal.end_lon = gps->lon_deg1e7;
    g_ncal.last_sample_seq = gps->update_sequence;
    g_ncal.run_target_heading_cd =
        NorthCalib_WrapUnsignedCd((int32)target_heading_cd);
    g_ncal.heading_ref_cd = 0;
    g_ncal.heading_sum_cd = 0L;
    g_ncal.heading_samples = 0U;
    g_ncal.run_distance_m = 0U;
    g_ncal.yaw_unstable_ticks = 0U;
    g_ncal.heading_err_bad_ticks = 0U;
    ShipControl_ResetYawHoldController();
    LOGI(NCAL_TAG, "run start tgt=%u reason=%u heading=%u",
         g_ncal.run_target_heading_cd,
         (u16)g_ncal.run_start_reason,
         app_get_heading_deg100());
    NorthCalib_EnterState(NCAL_STATE_RUN_STRAIGHT);
}

static u8 NorthCalib_GpsReady(const board_gps_state_t *gps)
{
    u8 sat_count;

    if (gps == 0) {
        return 0U;
    }
    sat_count = (gps->satellites_used_gsa > 0U) ?
                gps->satellites_used_gsa :
                gps->satellites_used;
    if (gps->fix_valid == 0U) {
        return 0U;
    }
    if (sat_count < NCAL_MIN_SATELLITES) {
        return 0U;
    }
    if ((gps->lat_deg1e7 == 0L) || (gps->lon_deg1e7 == 0L)) {
        return 0U;
    }
    return 1U;
}

static u8 NorthCalib_RemoteOnline(u32 now_ms)
{
    if (g_ncal.last_remote_ms == 0UL) {
        return 0U;
    }
    return ((now_ms - g_ncal.last_remote_ms) < NCAL_REMOTE_TIMEOUT_MS) ? 1U : 0U;
}

static u8 NorthCalib_ManualOverride(void)
{
    int16 throttle_input;
    int16 steering_input;

    throttle_input = (int16)((int16)g_ncal.ud - (int16)SHIP_AXIS_CENTER);
    steering_input = (int16)((int16)g_ncal.lr - (int16)SHIP_AXIS_CENTER);
    if ((throttle_input > 15) || (throttle_input < -15) ||
        (steering_input > 15) || (steering_input < -15)) {
        return 1U;
    }
    return 0U;
}

static int16 NorthCalib_WrapSignedCd(int32 angle_cd)
{
    while (angle_cd >= 18000L) {
        angle_cd -= 36000L;
    }
    while (angle_cd < -18000L) {
        angle_cd += 36000L;
    }
    return (int16)angle_cd;
}

static u16 NorthCalib_WrapUnsignedCd(int32 angle_cd)
{
    while (angle_cd >= 36000L) {
        angle_cd -= 36000L;
    }
    while (angle_cd < 0L) {
        angle_cd += 36000L;
    }
    return (u16)angle_cd;
}

static u32 NorthCalib_Abs32(int32 value)
{
    return (value < 0L) ? (u32)(-value) : (u32)value;
}

static u16 NorthCalib_LonScaleQ10(int32 lat_deg1e7)
{
    u32 abs_lat;
    u32 deg;

    abs_lat = NorthCalib_Abs32(lat_deg1e7);
    deg = abs_lat / 10000000UL;
    if (deg < 10UL) {
        return 1008U;
    }
    if (deg < 20UL) {
        return 962U;
    }
    if (deg < 30UL) {
        return 887U;
    }
    if (deg < 40UL) {
        return 784U;
    }
    if (deg < 50UL) {
        return 658U;
    }
    if (deg < 60UL) {
        return 512U;
    }
    return 350U;
}

static u16 NorthCalib_AtanDeg100(u32 z_q10)
{
    u32 z;
    u32 curve;
    u32 angle_deg100;

    z = z_q10;
    if (z > 1024UL) {
        z = 1024UL;
    }
    curve = 4500UL + ((1564UL * (1024UL - z)) / 1024UL);
    angle_deg100 = (z * curve) / 1024UL;
    return (u16)angle_deg100;
}

static u16 NorthCalib_BearingDeg100(int32 lat_from, int32 lon_from,
                                    int32 lat_to, int32 lon_to)
{
    int32 dlat;
    int32 dlon;
    u32 abs_lat;
    u32 abs_lon;
    u32 width;
    u32 height;
    u32 z_q10;
    u16 angle;
    u16 bearing;

    dlat = lat_to - lat_from;
    dlon = lon_to - lon_from;
    abs_lat = NorthCalib_Abs32(dlat);
    abs_lon = NorthCalib_Abs32(dlon);
    width = ((abs_lon * (u32)NorthCalib_LonScaleQ10(lat_from)) + 512UL) >> 10;
    height = abs_lat;

    if ((width == 0UL) && (height == 0UL)) {
        return 0U;
    }
    if (width == 0UL) {
        return (dlat >= 0L) ? 0U : 18000U;
    }
    if (height == 0UL) {
        return (dlon >= 0L) ? 9000U : 27000U;
    }

    if (height <= width) {
        z_q10 = ((height << 10) + (width >> 1)) / width;
        angle = NorthCalib_AtanDeg100(z_q10);
        if ((dlon >= 0L) && (dlat >= 0L)) {
            bearing = (u16)(9000U - angle);
        } else if ((dlon >= 0L) && (dlat < 0L)) {
            bearing = (u16)(9000U + angle);
        } else if ((dlon < 0L) && (dlat < 0L)) {
            bearing = (u16)(27000U - angle);
        } else {
            bearing = (u16)(27000U + angle);
        }
    } else {
        z_q10 = ((width << 10) + (height >> 1)) / height;
        angle = NorthCalib_AtanDeg100(z_q10);
        if ((dlon >= 0L) && (dlat >= 0L)) {
            bearing = angle;
        } else if ((dlon >= 0L) && (dlat < 0L)) {
            bearing = (u16)(18000U - angle);
        } else if ((dlon < 0L) && (dlat < 0L)) {
            bearing = (u16)(18000U + angle);
        } else {
            bearing = (u16)(36000U - angle);
        }
    }

    return NorthCalib_WrapUnsignedCd((int32)bearing);
}

static u16 NorthCalib_DistanceMeters(int32 lat_from, int32 lon_from,
                                     int32 lat_to, int32 lon_to)
{
    u32 dlat;
    u32 dlon;
    u32 width_m;
    u32 height_m;
    u32 max_m;
    u32 min_m;
    u32 distance_m;

    dlat = NorthCalib_Abs32(lat_to - lat_from);
    dlon = NorthCalib_Abs32(lon_to - lon_from);
    height_m = (dlat * 111130UL) / 10000000UL;
    width_m = (dlon * 111130UL) / 10000000UL;
    width_m = ((width_m * (u32)NorthCalib_LonScaleQ10(lat_from)) + 512UL) >> 10;

    if (width_m >= height_m) {
        max_m = width_m;
        min_m = height_m;
    } else {
        max_m = height_m;
        min_m = width_m;
    }
    distance_m = max_m + ((min_m * 3UL) >> 3);
    if (distance_m > 65535UL) {
        return 65535U;
    }
    return (u16)distance_m;
}

static void NorthCalib_WriteLe16(u8 *buf, u8 index, u16 value)
{
    buf[index] = (u8)(value & 0xFFU);
    buf[index + 1U] = (u8)(value >> 8);
}

static u16 NorthCalib_ReadLe16(const u8 *buf, u8 index)
{
    return (u16)((u16)buf[index] | ((u16)buf[index + 1U] << 8));
}

static void NorthCalib_WriteLe32(u8 *buf, u8 index, u32 value)
{
    buf[index] = (u8)(value & 0xFFUL);
    buf[index + 1U] = (u8)((value >> 8) & 0xFFUL);
    buf[index + 2U] = (u8)((value >> 16) & 0xFFUL);
    buf[index + 3U] = (u8)((value >> 24) & 0xFFUL);
}

static u32 NorthCalib_ReadLe32(const u8 *buf, u8 index)
{
    return ((u32)buf[index]) |
           ((u32)buf[index + 1U] << 8) |
           ((u32)buf[index + 2U] << 16) |
           ((u32)buf[index + 3U] << 24);
}

static void NorthCalib_RecordToBytes(const NorthCalib_Record_t *record, u8 *buf)
{
    NorthCalib_WriteLe32(buf, 0U, record->magic);
    buf[4] = record->version;
    NorthCalib_WriteLe16(buf, 5U, (u16)record->north_offset_cd);
    buf[7] = record->confidence;
    NorthCalib_WriteLe16(buf, 8U, record->sample_distance_m);
    NorthCalib_WriteLe16(buf, 10U, record->update_count);
    buf[12] = 0U;
    buf[13] = 0U;
    NorthCalib_WriteLe16(buf, 14U, record->checksum);
}

static void NorthCalib_BytesToRecord(const u8 *buf, NorthCalib_Record_t *record)
{
    record->magic = NorthCalib_ReadLe32(buf, 0U);
    record->version = buf[4];
    record->north_offset_cd = (int16)NorthCalib_ReadLe16(buf, 5U);
    record->confidence = buf[7];
    record->sample_distance_m = NorthCalib_ReadLe16(buf, 8U);
    record->update_count = NorthCalib_ReadLe16(buf, 10U);
    record->checksum = NorthCalib_ReadLe16(buf, 14U);
}

static u16 NorthCalib_RecordChecksum(const NorthCalib_Record_t *record)
{
    u8 buf[NCAL_RECORD_SIZE];
    u8 i;
    u16 sum;

    NorthCalib_RecordToBytes(record, buf);
    buf[14] = 0U;
    buf[15] = 0U;
    sum = 0U;
    for (i = 0U; i < NCAL_RECORD_SIZE; i++) {
        sum = (u16)(sum + (u16)buf[i]);
    }
    return (u16)(sum ^ 0xA55AU);
}

static u8 NorthCalib_RecordValid(const NorthCalib_Record_t *record)
{
    u16 checksum;

    if (record == 0) {
        return 0U;
    }
    checksum = NorthCalib_RecordChecksum(record);
    if ((record->magic == NCAL_RECORD_MAGIC) &&
        (record->version == NCAL_RECORD_VERSION) &&
        (record->checksum == checksum)) {
        return 1U;
    }
    return 0U;
}

static u8 NorthCalib_RecordNewer(u16 candidate_count, u16 current_count)
{
    u16 delta;

    delta = (u16)(candidate_count - current_count);
    if ((delta != 0U) && (delta < 32768U)) {
        return 1U;
    }
    return 0U;
}

static u8 NorthCalib_LoadRecordFrom(u8 slot, NorthCalib_Record_t *record)
{
    u8 buf[NCAL_RECORD_SIZE];

    if (record == 0) {
        return 0U;
    }
    if (parameter_store_load_north_calib_slot(slot, buf, NCAL_RECORD_SIZE) !=
        PARAMETER_STORE_OK) {
        return 0U;
    }
    NorthCalib_BytesToRecord(buf, record);
    return NorthCalib_RecordValid(record);
}

static u8 NorthCalib_NextSaveSlot(void)
{
    if (g_ncal.has_saved_record == 0U) {
        return NCAL_SLOT_A;
    }
    return (g_ncal.active_record_slot == NCAL_SLOT_A) ? NCAL_SLOT_B : NCAL_SLOT_A;
}

static void NorthCalib_LoadRecord(void)
{
    NorthCalib_Record_t record_a;
    NorthCalib_Record_t record_b;
    NorthCalib_Record_t *chosen;
    u8 valid_a;
    u8 valid_b;
    u8 chosen_slot;

    valid_a = NorthCalib_LoadRecordFrom(NCAL_SLOT_A, &record_a);
    valid_b = NorthCalib_LoadRecordFrom(NCAL_SLOT_B, &record_b);
    chosen = 0;
    chosen_slot = NCAL_SLOT_A;

    if ((valid_a != 0U) && (valid_b != 0U)) {
        if (NorthCalib_RecordNewer(record_b.update_count,
                                   record_a.update_count) != 0U) {
            chosen = &record_b;
            chosen_slot = NCAL_SLOT_B;
        } else {
            chosen = &record_a;
            chosen_slot = NCAL_SLOT_A;
        }
    } else if (valid_a != 0U) {
        chosen = &record_a;
        chosen_slot = NCAL_SLOT_A;
    } else if (valid_b != 0U) {
        chosen = &record_b;
        chosen_slot = NCAL_SLOT_B;
    }

    if (chosen != 0) {
        g_ncal.active_offset_cd = chosen->north_offset_cd;
        g_ncal.update_count = chosen->update_count;
        g_ncal.has_saved_record = 1U;
        g_ncal.active_record_slot = chosen_slot;
        LOGI(NCAL_TAG, "load slot=%u offset=%d conf=%u dist=%u cnt=%u",
             (u16)chosen_slot,
             chosen->north_offset_cd,
             (u16)chosen->confidence,
             chosen->sample_distance_m,
             chosen->update_count);
    } else {
        g_ncal.active_offset_cd = 0;
        g_ncal.update_count = 0U;
        g_ncal.has_saved_record = 0U;
        g_ncal.active_record_slot = NCAL_SLOT_A;
        LOGW(NCAL_TAG, "load default offset=0");
    }
}

static u8 NorthCalib_SaveRecord(int16 offset_cd, u8 confidence, u16 distance_m)
{
    u8 buf[NCAL_RECORD_SIZE];
    u8 slot;
    NorthCalib_Record_t record;

    slot = NorthCalib_NextSaveSlot();
    record.magic = NCAL_RECORD_MAGIC;
    record.version = NCAL_RECORD_VERSION;
    record.north_offset_cd = offset_cd;
    record.confidence = confidence;
    record.sample_distance_m = distance_m;
    record.update_count = (u16)(g_ncal.update_count + 1U);
    record.checksum = 0U;
    record.checksum = NorthCalib_RecordChecksum(&record);
    NorthCalib_RecordToBytes(&record, buf);

    if (parameter_store_save_north_calib_slot(slot, buf, NCAL_RECORD_SIZE) !=
        PARAMETER_STORE_OK) {
        return 0U;
    }
    g_ncal.update_count = record.update_count;
    g_ncal.has_saved_record = 1U;
    g_ncal.active_record_slot = slot;
    return 1U;
}
