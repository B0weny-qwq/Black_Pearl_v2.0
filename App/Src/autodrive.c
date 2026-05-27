#include "autodrive.h"
#include "autodrive_config.h"
#include "app.h"
#include "board_gps.h"
#include "logger.h"
#include "platform_scheduler.h"
#include "ship_control.h"

#define AUTODRIVE_WORK_OVERTIME            (10U * 60U * 100U)
#define AUTODRIVE_MIN_ACTIVE_DISTANCE_M    10U
#define AUTODRIVE_MAX_ACTIVE_DISTANCE_M    800U
#define AUTODRIVE_ARRIVE_DISTANCE_M        3U
#define AUTODRIVE_MANUAL_TIMEOUT_TICKS     (30U * 100U)
#define AUTODRIVE_MANUAL_CLOSE_TICKS       300U
#define AUTODRIVE_CRUISE_BASE_SPEED        850
#define AUTODRIVE_APPROACH_DISTANCE_M      20U
#define AUTODRIVE_CRAWL_DISTANCE_M         8U
#define AUTODRIVE_APPROACH_BASE_SPEED      700
#define AUTODRIVE_CRAWL_BASE_SPEED         500
#define AUTODRIVE_MINUTE_SCALE             10000UL
#define AUTODRIVE_MINUTES_PER_DEG          60UL
#define AUTODRIVE_METERS_PER_MINUTE        1850UL
#define AUTODRIVE_METERS_PER_DEG           111130UL
#define AUTODRIVE_ATAN_Q10                 1024UL
#define AUTODRIVE_ALIGN_TOLERANCE_CD       800
#define AUTODRIVE_ALIGN_ZERO_CROSS_CD      50
#define AUTODRIVE_ALIGN_STABLE_TICKS       20U
#define AUTODRIVE_ALIGN_TIMEOUT_TICKS      800U
#define AUTODRIVE_FISH_DUP_WAIT_MS         1500UL

static u8 autodrive_switch = 0U;
static u8 autodrive_state = AUTO_DRIVE_IDLE;
static u8 autodrive_mode = AUTO_DRIVE_CLOSE;
static u16 autodrive_work_overtime = 0U;
static u8 autodrive_fail_flag = 0U;

static AutoDrive_PointRaw_t idle_position;
static AutoDrive_PointRaw_t now_position;
static AutoDrive_PointRaw_t last_position;
static AutoDrive_PointRaw_t return_position;
static AutoDrive_PointRaw_t fish_position;
static AutoDrive_FishPointStore_t fish_points;
static u8 last_fish_cmd_index = 0U;
static u8 last_fish_save_result = AUTODRIVE_FISH_SAVE_NONE;
static AutoDrive_PointRaw_t last_fish_rx_point;
static u32 last_fish_rx_ms = 0UL;
static u8 last_fish_rx_valid = 0U;
static AutoDrive_ReturnConfig_t autodrv_cfg;

static u8 destination_direction = POSITION_EAST;
static u8 nowrun_direction = POSITION_EAST;
static u16 destination_angle = 0U;
static u16 autodrive_target_heading_cd = 0U;
static u8 autodrive_target_heading_valid = 0U;
static u16 autodrive_base_speed = AUTODRIVE_CRUISE_BASE_SPEED;
static u16 autodrive_align_ticks = 0U;
static u8 autodrive_align_stable_ticks = 0U;
static u8 autodrive_align_zero_seen = 0U;
static u8 autodrive_align_prev_valid = 0U;
static int16 autodrive_align_prev_error_cd = 0;

static u16 link_alive_ticks = 0U;
static u16 link_close_ticks = 0U;
static u32 last_run_update_seq = 0UL;
static u32 last_poll_tick_ms = 0UL;
static u32 last_link_tick_ms = 0UL;
static u8 last_diag_reason = AUTODRIVE_DIAG_REASON_NONE;

static u16 AutoDrive_ReadU16Wire(const u8 *data_m)
{
    return (u16)(((u16)data_m[0] << 8) | data_m[1]);
}

static void AutoDrive_PointFromLegacyWire(AutoDrive_PointRaw_t *point,
                                          const u8 *data_m)
{
    if ((point == 0) || (data_m == 0)) {
        return;
    }

    point->lon_ew = data_m[0];
    point->lon_whole = AutoDrive_ReadU16Wire(&data_m[1]);
    point->lon_frac = AutoDrive_ReadU16Wire(&data_m[3]);
    point->lat_ns = data_m[5];
    point->lat_whole = AutoDrive_ReadU16Wire(&data_m[6]);
    point->lat_frac = AutoDrive_ReadU16Wire(&data_m[8]);
}

static u32 AutoDrive_Abs32Diff(u32 lhs, u32 rhs)
{
    if (lhs >= rhs) {
        return lhs - rhs;
    }
    return rhs - lhs;
}

static u16 AutoDrive_Atan01Deg(u16 z_q10)
{
    u32 z;
    u32 curve;
    u32 angle_deg100;

    z = z_q10;
    if (z > AUTODRIVE_ATAN_Q10) {
        z = AUTODRIVE_ATAN_Q10;
    }

    curve = 4500UL + ((1564UL * (AUTODRIVE_ATAN_Q10 - z)) / AUTODRIVE_ATAN_Q10);
    angle_deg100 = (z * curve) / AUTODRIVE_ATAN_Q10;
    return (u16)((angle_deg100 + 50UL) / 100UL);
}

static u8 AutoDrive_PointRawValid(const AutoDrive_PointRaw_t *point)
{
    if (point == 0) {
        return 0U;
    }
    if ((point->lon_ew != 'E') && (point->lon_ew != 'W')) {
        return 0U;
    }
    if (point->lon_whole == 0U) {
        return 0U;
    }
    return 1U;
}

static void AutoDrive_CopyPoint(AutoDrive_PointRaw_t *dst, const AutoDrive_PointRaw_t *src)
{
    if ((dst == 0) || (src == 0)) {
        return;
    }
    *dst = *src;
}

static void AutoDrive_ClearPoint(AutoDrive_PointRaw_t *point)
{
    if (point == 0) {
        return;
    }

    point->lon_ew = 0U;
    point->lon_whole = 0U;
    point->lon_frac = 0U;
    point->lat_ns = 0U;
    point->lat_whole = 0U;
    point->lat_frac = 0U;
}

static void AutoDrive_ClearFishPoints(void)
{
    u8 i;

    for (i = 0U; i < AUTODRIVE_FISH_POINT_COUNT; i++) {
        AutoDrive_ClearPoint(&fish_points.point[i]);
    }
    fish_points.valid_mask = 0U;
    fish_points.next_index = 0U;
    fish_points.latest_index = 0xFFU;
}

static u8 AutoDrive_FishPointsReady(void)
{
    return ((fish_points.valid_mask & ((1U << AUTODRIVE_FISH_POINT_COUNT) - 1U)) ==
            ((1U << AUTODRIVE_FISH_POINT_COUNT) - 1U)) ? 1U : 0U;
}

static u8 AutoDrive_PointRawEqual(const AutoDrive_PointRaw_t *lhs,
                                  const AutoDrive_PointRaw_t *rhs)
{
    if ((lhs == 0) || (rhs == 0)) {
        return 0U;
    }
    return ((lhs->lon_ew == rhs->lon_ew) &&
            (lhs->lon_whole == rhs->lon_whole) &&
            (lhs->lon_frac == rhs->lon_frac) &&
            (lhs->lat_ns == rhs->lat_ns) &&
            (lhs->lat_whole == rhs->lat_whole) &&
            (lhs->lat_frac == rhs->lat_frac)) ? 1U : 0U;
}

static u8 AutoDrive_FindFishPointIndex(const AutoDrive_PointRaw_t *point)
{
    u8 i;

    if ((point == 0) || (AutoDrive_PointRawValid(point) == 0U)) {
        return 0U;
    }
    for (i = 0U; i < AUTODRIVE_FISH_POINT_COUNT; i++) {
        if ((fish_points.valid_mask & (u8)(1U << i)) == 0U) {
            continue;
        }
        if (AutoDrive_PointRawEqual(point, &fish_points.point[i]) != 0U) {
            return (u8)(i + 1U);
        }
    }
    return 0U;
}

static u8 AutoDrive_StoreFishPoint(const AutoDrive_PointRaw_t *point)
{
    u8 index;

    if ((point == 0) || (AutoDrive_PointRawValid(point) == 0U)) {
        return 0xFFU;
    }
    if (AutoDrive_FishPointsReady() != 0U) {
        return 0xFFU;
    }

    index = fish_points.next_index;
    if (index >= AUTODRIVE_FISH_POINT_COUNT) {
        return 0xFFU;
    }
    AutoDrive_CopyPoint(&fish_points.point[index], point);
    fish_points.valid_mask |= (u8)(1U << index);
    fish_points.latest_index = index;
    fish_points.next_index = (u8)(index + 1U);
    return fish_points.latest_index;
}

static u8 AutoDrive_IsFishRxDuplicate(const AutoDrive_PointRaw_t *point, u32 now_ms)
{
    if ((point == 0) || (last_fish_rx_valid == 0U)) {
        return 0U;
    }
    if (AutoDrive_PointRawEqual(point, &last_fish_rx_point) == 0U) {
        return 0U;
    }
    return ((now_ms - last_fish_rx_ms) < AUTODRIVE_FISH_DUP_WAIT_MS) ? 1U : 0U;
}

static void AutoDrive_RecordFishRxPoint(const AutoDrive_PointRaw_t *point, u32 now_ms)
{
    if ((point == 0) || (AutoDrive_PointRawValid(point) == 0U)) {
        return;
    }
    AutoDrive_CopyPoint(&last_fish_rx_point, point);
    last_fish_rx_ms = now_ms;
    last_fish_rx_valid = 1U;
}

static void AutoDrive_PointFromGps(AutoDrive_PointRaw_t *point,
                                   const board_gps_state_t *gps)
{
    u32 lat_abs;
    u32 lon_abs;
    u32 lat_deg;
    u32 lon_deg;
    u32 lat_min_x1e4;
    u32 lon_min_x1e4;

    if ((point == 0) || (gps == 0)) {
        return;
    }

    lat_abs = (u32)((gps->lat_deg1e7 < 0L) ? -gps->lat_deg1e7 : gps->lat_deg1e7);
    lon_abs = (u32)((gps->lon_deg1e7 < 0L) ? -gps->lon_deg1e7 : gps->lon_deg1e7);

    lat_deg = lat_abs / 10000000UL;
    lon_deg = lon_abs / 10000000UL;
    lat_min_x1e4 = ((lat_abs % 10000000UL) * 6UL + 50UL) / 100UL;
    lon_min_x1e4 = ((lon_abs % 10000000UL) * 6UL + 50UL) / 100UL;

    point->lon_ew = (gps->lon_deg1e7 < 0L) ? 'W' : 'E';
    point->lon_whole = (u16)(lon_deg * 100UL + (lon_min_x1e4 / 10000UL));
    point->lon_frac = (u16)(lon_min_x1e4 % 10000UL);
    point->lat_ns = (gps->lat_deg1e7 < 0L) ? 'S' : 'N';
    point->lat_whole = (u16)(lat_deg * 100UL + (lat_min_x1e4 / 10000UL));
    point->lat_frac = (u16)(lat_min_x1e4 % 10000UL);
}

void AutoDrive_GetCurrentPointRaw(AutoDrive_PointRaw_t *point)
{
    const board_gps_state_t *gps;

    gps = board_gps_get_state();
    if ((point == 0) || (gps == 0)) {
        return;
    }
    AutoDrive_PointFromGps(point, gps);
}

static u8 AutoDrive_GpsReady(void)
{
    const board_gps_state_t *gps;
    u8 sat_count;

    gps = board_gps_get_state();
    if (gps == 0) {
        return 0U;
    }
    sat_count = (gps->satellites_used_gsa > 0U) ?
                gps->satellites_used_gsa :
                gps->satellites_used;
    if (sat_count < 7U) {
        return 0U;
    }
    if ((gps->lat_deg1e7 == 0L) || (gps->lon_deg1e7 == 0L)) {
        return 0U;
    }
    return 1U;
}

static u8 AutoDrive_GetSatCount(void)
{
    const board_gps_state_t *gps;

    gps = board_gps_get_state();
    if (gps == 0) {
        return 0U;
    }
    if (gps->satellites_used_gsa > 0U) {
        return gps->satellites_used_gsa;
    }
    return gps->satellites_used;
}

static u8 AutoDrive_GetReadyCurrentPoint(AutoDrive_PointRaw_t *point)
{
    const board_gps_state_t *gps;

    if (point == 0) {
        return 0U;
    }
    if (AutoDrive_GpsReady() == 0U) {
        return 0U;
    }
    gps = board_gps_get_state();
    if (gps == 0) {
        return 0U;
    }
    AutoDrive_PointFromGps(point, gps);
    return AutoDrive_PointRawValid(point);
}

static void AutoDrive_SetDiagReason(u8 reason)
{
    last_diag_reason = reason;
}

static void AutoDrive_ResetApproachTracker(void)
{
    autodrive_base_speed = AUTODRIVE_CRUISE_BASE_SPEED;
}

static void AutoDrive_ResetAlignTracker(void)
{
    autodrive_align_ticks = 0U;
    autodrive_align_stable_ticks = 0U;
    autodrive_align_zero_seen = 0U;
    autodrive_align_prev_valid = 0U;
    autodrive_align_prev_error_cd = 0;
}

static int16 AutoDrive_Abs16(int16 value)
{
    return (value >= 0) ? value : (int16)(-value);
}

static int16 AutoDrive_WrapSignedCd(int32 angle_cd)
{
    while (angle_cd >= 18000L) {
        angle_cd -= 36000L;
    }
    while (angle_cd < -18000L) {
        angle_cd += 36000L;
    }
    return (int16)angle_cd;
}

static u8 AutoDrive_GetHeadingErrorCd(int16 *error_cd)
{
    if (error_cd == 0) {
        return 0U;
    }
    if ((autodrive_target_heading_valid == 0U) ||
        (app_get_heading_ready() == 0U)) {
        return 0U;
    }
    *error_cd =
        AutoDrive_WrapSignedCd((int32)autodrive_target_heading_cd -
                               (int32)app_get_heading_deg100());
    return 1U;
}

static u8 AutoDrive_AlignTargetReached(void)
{
    int16 heading_error_cd;
    u8 crossed_zero;

    if (AutoDrive_GetHeadingErrorCd(&heading_error_cd) == 0U) {
        autodrive_align_stable_ticks = 0U;
        autodrive_align_prev_valid = 0U;
        return 0U;
    }
    if (autodrive_align_ticks < 0xFFFFU) {
        autodrive_align_ticks++;
    }

    crossed_zero = 0U;
    if (AutoDrive_Abs16(heading_error_cd) <= (int16)AUTODRIVE_ALIGN_ZERO_CROSS_CD) {
        crossed_zero = 1U;
    } else if (autodrive_align_prev_valid != 0U) {
        if (((autodrive_align_prev_error_cd < 0) && (heading_error_cd > 0)) ||
            ((autodrive_align_prev_error_cd > 0) && (heading_error_cd < 0))) {
            crossed_zero = 1U;
        }
    }
    if (crossed_zero != 0U) {
        autodrive_align_zero_seen = 1U;
    }
    autodrive_align_prev_error_cd = heading_error_cd;
    autodrive_align_prev_valid = 1U;

    if ((autodrive_align_zero_seen != 0U) &&
        (AutoDrive_Abs16(heading_error_cd) <= (int16)AUTODRIVE_ALIGN_TOLERANCE_CD)) {
        if (autodrive_align_stable_ticks < 255U) {
            autodrive_align_stable_ticks++;
        }
    } else {
        autodrive_align_stable_ticks = 0U;
    }

    if (autodrive_align_stable_ticks >= (u8)AUTODRIVE_ALIGN_STABLE_TICKS) {
        return 1U;
    }
    if (autodrive_align_ticks >= AUTODRIVE_ALIGN_TIMEOUT_TICKS) {
        return 1U;
    }
    return 0U;
}

static u8 AutoDrive_TickWorkOvertime(void)
{
    if (autodrive_work_overtime > 0U) {
        autodrive_work_overtime--;
        return 1U;
    }
    AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
    AutoDrive_WorkOvertimeFail();
    autodrive_state = AUTO_DRIVE_TIMEOUT;
    return 0U;
}

static u16 AutoDrive_InterpolateSpeed(u16 distance_m,
                                      u16 near_distance_m,
                                      u16 far_distance_m,
                                      u16 near_speed,
                                      u16 far_speed)
{
    u32 distance_span;
    u32 speed_span;
    u32 distance_offset;

    if (distance_m <= near_distance_m) {
        return near_speed;
    }
    if (distance_m >= far_distance_m) {
        return far_speed;
    }
    if (far_distance_m <= near_distance_m) {
        return near_speed;
    }

    distance_span = (u32)far_distance_m - (u32)near_distance_m;
    speed_span = (u32)far_speed - (u32)near_speed;
    distance_offset = (u32)distance_m - (u32)near_distance_m;
    return (u16)((u32)near_speed +
                 ((distance_offset * speed_span) / distance_span));
}

static u16 AutoDrive_CalcBaseSpeed(u16 distance_m)
{
    if (distance_m >= AUTODRIVE_APPROACH_DISTANCE_M) {
        return AUTODRIVE_CRUISE_BASE_SPEED;
    }
    if (distance_m >= AUTODRIVE_CRAWL_DISTANCE_M) {
        return AutoDrive_InterpolateSpeed(distance_m,
                                          AUTODRIVE_CRAWL_DISTANCE_M,
                                          AUTODRIVE_APPROACH_DISTANCE_M,
                                          AUTODRIVE_APPROACH_BASE_SPEED,
                                          AUTODRIVE_CRUISE_BASE_SPEED);
    }
    return AutoDrive_InterpolateSpeed(distance_m,
                                      AUTODRIVE_ARRIVE_DISTANCE_M,
                                      AUTODRIVE_CRAWL_DISTANCE_M,
                                      AUTODRIVE_CRAWL_BASE_SPEED,
                                      AUTODRIVE_APPROACH_BASE_SPEED);
}

static void AutoDrive_UpdateApproachSpeed(u16 distance_m)
{
    autodrive_base_speed = AutoDrive_CalcBaseSpeed(distance_m);
}

static u8 AutoDrive_GetTargetPoint(const AutoDrive_PointRaw_t **target)
{
    if (target == 0) {
        return 0U;
    }
    if (autodrive_mode == AUTO_DRIVE_GO_FISISH_POSITION) {
        *target = &fish_position;
        return 1U;
    }
    if (autodrive_mode == AUTO_DRIVE_GO_HOME_POSITION) {
        *target = &return_position;
        return 1U;
    }
    *target = 0;
    return 0U;
}

static u8 AutoDrive_UpdateTargetHeading(const AutoDrive_PointRaw_t *current_point,
                                        const AutoDrive_PointRaw_t *target_point)
{
    u16 target_angle;
    u8 target_direction;

    if ((current_point == 0) || (target_point == 0)) {
        return 0U;
    }

    target_angle = AutoDrive_GetAngelNowToDestination((const u8 *)current_point,
                                                      (const u8 *)target_point);
    if (target_angle == 65535U) {
        return 0U;
    }
    target_direction =
        AutoDrive_GetDirectionNowToDestination((const u8 *)current_point,
                                               (const u8 *)target_point);
    destination_direction = target_direction;
    destination_angle = AutoDrive_GetNorthAngel(target_direction, (u8)target_angle);
    if (destination_angle >= 360U) {
        destination_angle = (u16)(destination_angle % 360U);
    }
    autodrive_target_heading_cd = (u16)(destination_angle * 100U);
    autodrive_target_heading_valid = 1U;
    return 1U;
}

static u8 AutoDrive_ApplyHeadingHold(u16 base_speed)
{
    if ((autodrive_target_heading_valid == 0U) ||
        (app_get_heading_ready() == 0U)) {
        ShipControl_StopGpsNav();
        return 0U;
    }
    ShipControl_RequestGpsNav(autodrive_target_heading_cd, (int16)base_speed);
    return 1U;
}

static u8 AutoDrive_ApplyAlignHeadingHold(void)
{
    if ((autodrive_target_heading_valid == 0U) ||
        (app_get_heading_ready() == 0U)) {
        ShipControl_StopGpsNav();
        return 0U;
    }
    ShipControl_RequestGpsAlign(autodrive_target_heading_cd);
    return 1U;
}

void AutoDrive_StopMotion(void)
{
    autodrive_target_heading_valid = 0U;
    AutoDrive_ResetApproachTracker();
    AutoDrive_ResetAlignTracker();
    ShipControl_StopGpsNav();
}

void AutoDrive_Stop(void)
{
    autodrive_state = AUTO_DRIVE_IDLE;
    AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_STOP);
    AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
}

void AutoDrive_SetMode(u8 mode)
{
    if ((autodrive_mode != AUTO_DRIVE_CLOSE) && (mode == AUTO_DRIVE_CLOSE)) {
        AutoDrive_StopMotion();
    }
    autodrive_mode = mode;
    if (autodrive_mode == AUTO_DRIVE_CLOSE) {
        autodrive_state = AUTO_DRIVE_IDLE;
    }
}

u8 AutoDrive_GetMode(void)
{
    return autodrive_mode;
}

u8 AutoDrive_InActive(void)
{
    if (autodrive_state != AUTO_DRIVE_IDLE) {
        if (autodrive_mode == AUTO_DRIVE_GO_HOME_POSITION) {
            return 1U;
        }
        if (autodrive_mode == AUTO_DRIVE_GO_FISISH_POSITION) {
            return 2U;
        }
    }
    return 0U;
}

u8 AutoDrive_IsBusy(void)
{
    return (autodrive_state != AUTO_DRIVE_IDLE) ? 1U : 0U;
}

u8 AutoDrive_IsCanActive(const AutoDrive_PointRaw_t *point)
{
    AutoDrive_PointRaw_t current_point;
    u16 distance;

    if (autodrive_state != AUTO_DRIVE_IDLE) {
        return 0U;
    }
    if (AutoDrive_PointRawValid(point) == 0U) {
        return 0U;
    }
    if (AutoDrive_GetReadyCurrentPoint(&current_point) == 0U) {
        return 0U;
    }
    distance = AutoDrive_GetDistanceNowToDestination((const u8 *)point,
                                                     (const u8 *)&current_point);
    if ((distance > AUTODRIVE_MIN_ACTIVE_DISTANCE_M) &&
        (distance < AUTODRIVE_MAX_ACTIVE_DISTANCE_M)) {
        AutoDrive_CopyPoint(&idle_position, &current_point);
        return 1U;
    }
    return 0U;
}

void AutoDrive_SetReturnPositionRaw(const u8 *data_m)
{
    AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_CMD_RETURN_HOME);
    if ((data_m == 0) || (autodrive_state != AUTO_DRIVE_IDLE)) {
        return;
    }

    AutoDrive_PointFromLegacyWire(&return_position, data_m);
    AutoDrive_CopyPoint(&autodrv_cfg.ret_point, &return_position);
    (void)AutoDriveCfg_Save(&autodrv_cfg);
    if (AutoDrive_IsCanActive(&return_position) == 0U) {
        autodrive_state = AUTO_DRIVE_REJECT;
        AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
        return;
    }

    AutoDrive_SetMode(AUTO_DRIVE_GO_HOME_POSITION);
    autodrive_state = AUTO_DRIVE_START;
    autodrive_work_overtime = AUTODRIVE_WORK_OVERTIME;
    autodrive_fail_flag = 0U;
}

u8 AutoDrive_SetFishPositionRaw(const u8 *data_m)
{
    AutoDrive_PointRaw_t rx_point;
    u8 matched_index;
    u8 stored_index;
    u32 now_ms;

    AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_CMD_GOTO_POINT);
    last_fish_cmd_index = 0U;
    last_fish_save_result = AUTODRIVE_FISH_SAVE_NONE;
    if (autodrive_state != AUTO_DRIVE_IDLE) {
        last_fish_save_result = AUTODRIVE_FISH_SAVE_BUSY;
        return AUTODRIVE_FISH_CMD_BUSY;
    }
    if (data_m == 0) {
        last_fish_save_result = AUTODRIVE_FISH_SAVE_INVALID;
        return AUTODRIVE_FISH_CMD_INVALID;
    }

    AutoDrive_PointFromLegacyWire(&rx_point, data_m);
    if (AutoDrive_PointRawValid(&rx_point) == 0U) {
        last_fish_save_result = AUTODRIVE_FISH_SAVE_INVALID;
        return AUTODRIVE_FISH_CMD_INVALID;
    }

    now_ms = platform_scheduler_get_tick_ms();
    matched_index = AutoDrive_FindFishPointIndex(&rx_point);
    if (AutoDrive_IsFishRxDuplicate(&rx_point, now_ms) != 0U) {
        last_fish_cmd_index = matched_index;
        if (matched_index != 0U) {
            last_fish_save_result = AUTODRIVE_FISH_SAVE_EXISTS;
        }
        AutoDrive_RecordFishRxPoint(&rx_point, now_ms);
        return AUTODRIVE_FISH_CMD_DUP_WAIT;
    }

    if (matched_index == 0U) {
        if (AutoDrive_FishPointsReady() == 0U) {
            stored_index = AutoDrive_StoreFishPoint(&rx_point);
            if (stored_index == 0xFFU) {
                last_fish_save_result = AUTODRIVE_FISH_SAVE_FULL_TEMP;
                return AUTODRIVE_FISH_CMD_REJECT_UNKNOWN;
            }
            last_fish_cmd_index = (u8)(stored_index + 1U);
            matched_index = last_fish_cmd_index;
            last_fish_save_result = AUTODRIVE_FISH_SAVE_STORED;
        } else {
            last_fish_save_result = AUTODRIVE_FISH_SAVE_FULL_TEMP;
            return AUTODRIVE_FISH_CMD_REJECT_UNKNOWN;
        }
    } else {
        last_fish_cmd_index = matched_index;
        last_fish_save_result = AUTODRIVE_FISH_SAVE_EXISTS;
    }

    AutoDrive_RecordFishRxPoint(&rx_point, now_ms);
    AutoDrive_CopyPoint(&fish_position, &rx_point);
    if (AutoDrive_IsCanActive(&fish_position) == 0U) {
        return AUTODRIVE_FISH_CMD_REJECT_DISTANCE;
    }

    AutoDrive_SetMode(AUTO_DRIVE_GO_FISISH_POSITION);
    autodrive_state = AUTO_DRIVE_START;
    autodrive_work_overtime = AUTODRIVE_WORK_OVERTIME;
    autodrive_fail_flag = 0U;
    return AUTODRIVE_FISH_CMD_STARTED;
}

void AutoDrive_TriggerReturnWithReason(u8 reason)
{
    AutoDrive_SetDiagReason(reason);
    if (autodrv_cfg.auto_ret_onoff != 0x30U) {
        if ((autodrive_fail_flag != 0U) || (autodrive_state != AUTO_DRIVE_IDLE)) {
            return;
        }
        if (AutoDrive_IsCanActive(&autodrv_cfg.ret_point) != 0U) {
            AutoDrive_CopyPoint(&return_position, &autodrv_cfg.ret_point);
            AutoDrive_SetMode(AUTO_DRIVE_GO_HOME_POSITION);
            autodrive_state = AUTO_DRIVE_START;
            autodrive_work_overtime = AUTODRIVE_WORK_OVERTIME;
        }
    }
}

void AutoDrive_TriggerReturn(void)
{
    AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_GENERIC_TRIGGER);
}

void AutoDrive_WorkOvertimeFail(void)
{
    autodrive_fail_flag = 1U;
    AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_OVERTIME);
}

void AutoDrive_SetSwitchRaw(const u8 *data_m, u8 len)
{
    if ((data_m == 0) || (len == 0U)) {
        return;
    }
    autodrive_switch = data_m[0];
    autodrv_cfg.auto_ret_onoff = data_m[0];
    if (len >= (u8)(1U + AUTODRIVE_LEGACY_POINT_WIRE_LEN)) {
        AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_RETURN_SWITCH_SAVE);
        AutoDrive_PointFromLegacyWire(&autodrv_cfg.ret_point, &data_m[1]);
        if (autodrive_state == AUTO_DRIVE_IDLE) {
            AutoDrive_CopyPoint(&return_position, &autodrv_cfg.ret_point);
        }
    }
    (void)AutoDriveCfg_Save(&autodrv_cfg);
    if (autodrv_cfg.auto_ret_onoff != 0x30U) {
        AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_RETURN_SWITCH_SAVE);
    }
}

void AutoDrive_GetStoredConfig(AutoDrive_ReturnConfig_t *cfg)
{
    if (cfg != 0) {
        *cfg = autodrv_cfg;
    }
}

u8 AutoDrive_GetReturnPositionRaw(AutoDrive_PointRaw_t *point)
{
    if (point != 0) {
        AutoDrive_CopyPoint(point, &return_position);
    }
    return AutoDrive_PointRawValid(&return_position);
}

u8 AutoDrive_GetFishPositionRaw(AutoDrive_PointRaw_t *point)
{
    if (point != 0) {
        AutoDrive_CopyPoint(point, &fish_position);
    }
    return AutoDrive_PointRawValid(&fish_position);
}

u8 AutoDrive_GetFishPositionByIndexRaw(u8 index, AutoDrive_PointRaw_t *point)
{
    u8 slot;

    if ((index == 0U) || (index > AUTODRIVE_FISH_POINT_COUNT)) {
        return 0U;
    }
    slot = (u8)(index - 1U);
    if ((fish_points.valid_mask & (u8)(1U << slot)) == 0U) {
        return 0U;
    }
    if (point != 0) {
        AutoDrive_CopyPoint(point, &fish_points.point[slot]);
    }
    return AutoDrive_PointRawValid(&fish_points.point[slot]);
}

u8 AutoDrive_GetLastFishCommandIndex(void)
{
    return last_fish_cmd_index;
}

u8 AutoDrive_GetLastFishSaveResult(void)
{
    return last_fish_save_result;
}

u8 AutoDrive_GetDirectionNowToDestination(const u8 *nowpositionData,
                                          const u8 *despositionData)
{
    const AutoDrive_PointRaw_t *nowposition;
    const AutoDrive_PointRaw_t *desposition;
    u8 direction;

    nowposition = (const AutoDrive_PointRaw_t *)nowpositionData;
    desposition = (const AutoDrive_PointRaw_t *)despositionData;
    direction = 0U;

    if ((nowposition->lon_whole > desposition->lon_whole) ||
        ((nowposition->lon_whole == desposition->lon_whole) &&
         (nowposition->lon_frac > desposition->lon_frac))) {
        direction = POSITION_WEST;
        if ((nowposition->lat_whole > desposition->lat_whole) ||
            ((nowposition->lat_whole == desposition->lat_whole) &&
             (nowposition->lat_frac > desposition->lat_frac))) {
            direction = POSITION_WEST_SOUTH;
        } else if ((nowposition->lat_whole < desposition->lat_whole) ||
                   ((nowposition->lat_whole == desposition->lat_whole) &&
                    (nowposition->lat_frac < desposition->lat_frac))) {
            direction = POSITION_WEST_NORTH;
        }
    }

    if ((nowposition->lon_whole < desposition->lon_whole) ||
        ((nowposition->lon_whole == desposition->lon_whole) &&
         (nowposition->lon_frac < desposition->lon_frac))) {
        direction = POSITION_EAST;
        if ((nowposition->lat_whole > desposition->lat_whole) ||
            ((nowposition->lat_whole == desposition->lat_whole) &&
             (nowposition->lat_frac > desposition->lat_frac))) {
            direction = POSITION_EAST_SOUTH;
        } else if ((nowposition->lat_whole < desposition->lat_whole) ||
                   ((nowposition->lat_whole == desposition->lat_whole) &&
                    (nowposition->lat_frac < desposition->lat_frac))) {
            direction = POSITION_EAST_NORTH;
        }
    }

    if (direction == 0U) {
        if ((nowposition->lat_whole > desposition->lat_whole) ||
            ((nowposition->lat_whole == desposition->lat_whole) &&
             (nowposition->lat_frac > desposition->lat_frac))) {
            direction = POSITION_SOUTH;
        } else {
            direction = POSITION_NORTH;
        }
    }
    return direction;
}

static u32 AutoDrive_CalDistanceLon(const AutoDrive_PointRaw_t *now,
                                    const AutoDrive_PointRaw_t *des)
{
    u16 dd1;
    u16 dd2;
    u32 sec1;
    u32 sec2;
    u16 ddiff;
    u32 sec_diff;

    dd1 = now->lon_whole / 100U;
    dd2 = des->lon_whole / 100U;
    sec1 = ((u32)(now->lon_whole % 100U) * AUTODRIVE_MINUTE_SCALE) + (u32)now->lon_frac;
    sec2 = ((u32)(des->lon_whole % 100U) * AUTODRIVE_MINUTE_SCALE) + (u32)des->lon_frac;

    if (dd1 == dd2) {
        sec_diff = AutoDrive_Abs32Diff(sec1, sec2);
        return (sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE;
    }

    if (dd1 > dd2) {
        ddiff = (u16)(dd1 - dd2);
        if (sec1 > sec2) {
            sec_diff = sec1 - sec2;
        } else {
            ddiff--;
            sec1 += (AUTODRIVE_MINUTES_PER_DEG * AUTODRIVE_MINUTE_SCALE);
            sec_diff = sec1 - sec2;
        }
    } else {
        ddiff = (u16)(dd2 - dd1);
        if (sec2 > sec1) {
            sec_diff = sec2 - sec1;
        } else {
            ddiff--;
            sec2 += (AUTODRIVE_MINUTES_PER_DEG * AUTODRIVE_MINUTE_SCALE);
            sec_diff = sec2 - sec1;
        }
    }

    if (ddiff != 0U) {
        return ((u32)ddiff * AUTODRIVE_METERS_PER_DEG) +
               ((sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE);
    }
    return (sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE;
}

static u32 AutoDrive_CalDistanceLat(const AutoDrive_PointRaw_t *now,
                                    const AutoDrive_PointRaw_t *des)
{
    u16 dd1;
    u16 dd2;
    u32 sec1;
    u32 sec2;
    u16 ddiff;
    u32 sec_diff;

    dd1 = now->lat_whole / 100U;
    dd2 = des->lat_whole / 100U;
    sec1 = ((u32)(now->lat_whole % 100U) * AUTODRIVE_MINUTE_SCALE) + (u32)now->lat_frac;
    sec2 = ((u32)(des->lat_whole % 100U) * AUTODRIVE_MINUTE_SCALE) + (u32)des->lat_frac;

    if (dd1 == dd2) {
        sec_diff = AutoDrive_Abs32Diff(sec1, sec2);
        return (sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE;
    }

    if (dd1 > dd2) {
        ddiff = (u16)(dd1 - dd2);
        if (sec1 > sec2) {
            sec_diff = sec1 - sec2;
        } else {
            ddiff--;
            sec1 += (AUTODRIVE_MINUTES_PER_DEG * AUTODRIVE_MINUTE_SCALE);
            sec_diff = sec1 - sec2;
        }
    } else {
        ddiff = (u16)(dd2 - dd1);
        if (sec2 > sec1) {
            sec_diff = sec2 - sec1;
        } else {
            ddiff--;
            sec2 += (AUTODRIVE_MINUTES_PER_DEG * AUTODRIVE_MINUTE_SCALE);
            sec_diff = sec2 - sec1;
        }
    }

    if (ddiff != 0U) {
        return ((u32)ddiff * AUTODRIVE_METERS_PER_DEG) +
               ((sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE);
    }
    return (sec_diff * AUTODRIVE_METERS_PER_MINUTE) / AUTODRIVE_MINUTE_SCALE;
}

u16 AutoDrive_GetAngelNowToDestination(const u8 *nowpositionData,
                                       const u8 *despositionData)
{
    const AutoDrive_PointRaw_t *nowposition;
    const AutoDrive_PointRaw_t *desposition;
    u32 distance_width;
    u32 distance_height;
    u32 z_q10;
    u16 base;

    nowposition = (const AutoDrive_PointRaw_t *)nowpositionData;
    desposition = (const AutoDrive_PointRaw_t *)despositionData;
    distance_width = AutoDrive_CalDistanceLon(nowposition, desposition);
    distance_height = AutoDrive_CalDistanceLat(nowposition, desposition);

    if (distance_width == 0UL) {
        if (distance_height == 0UL) {
            return 65535U;
        }
        return 90U;
    }
    if (distance_height <= distance_width) {
        z_q10 = ((distance_height << 10) + (distance_width >> 1)) / distance_width;
        return AutoDrive_Atan01Deg((u16)z_q10);
    }
    z_q10 = ((distance_width << 10) + (distance_height >> 1)) / distance_height;
    base = AutoDrive_Atan01Deg((u16)z_q10);
    return (u16)(90U - base);
}

u16 AutoDrive_GetDistanceNowToDestination(const u8 *nowpositionData,
                                          const u8 *despositionData)
{
    const AutoDrive_PointRaw_t *nowposition;
    const AutoDrive_PointRaw_t *desposition;
    u32 distance_width;
    u32 distance_height;
    u32 max_distance;
    u32 min_distance;
    u32 distance;

    nowposition = (const AutoDrive_PointRaw_t *)nowpositionData;
    desposition = (const AutoDrive_PointRaw_t *)despositionData;
    distance_width = AutoDrive_CalDistanceLon(nowposition, desposition);
    distance_height = AutoDrive_CalDistanceLat(nowposition, desposition);
    if (distance_width >= distance_height) {
        max_distance = distance_width;
        min_distance = distance_height;
    } else {
        max_distance = distance_height;
        min_distance = distance_width;
    }
    distance = max_distance + ((min_distance * 3UL) >> 3);
    if (distance > 65535UL) {
        return 65535U;
    }
    return (u16)distance;
}

u16 AutoDrive_GetNorthAngel(u8 direction, u8 angel)
{
    switch (direction) {
    case POSITION_NORTH:
        return 0U;
    case POSITION_EAST_NORTH:
        return (u16)(90U - angel);
    case POSITION_EAST:
        return 90U;
    case POSITION_EAST_SOUTH:
        return (u16)(90U + angel);
    case POSITION_SOUTH:
        return 180U;
    case POSITION_WEST_SOUTH:
        return (u16)(270U - angel);
    case POSITION_WEST:
        return 270U;
    case POSITION_WEST_NORTH:
        return (u16)(270U + angel);
    default:
        return 0U;
    }
}

static u16 AutoDrive_GetStartHeadingDeg(void)
{
    u16 heading_cd;
    u16 heading;

    if (app_get_heading_ready() != 0U) {
        heading_cd = (u16)(app_get_heading_deg100() % 36000U);
        heading = (u16)((heading_cd + 50U) / 100U);
        if (heading >= 360U) {
            heading = 0U;
        }
        return heading;
    }
    return 0U;
}

static u16 AutoDrive_GetRunHeadingDeg(void)
{
    u16 heading_cd;
    u16 heading;

    if (app_get_heading_ready() != 0U) {
        heading_cd = (u16)(app_get_heading_deg100() % 36000U);
        heading = (u16)((heading_cd + 50U) / 100U);
        if (heading >= 360U) {
            heading = 0U;
        }
        return heading;
    }
    return 65535U;
}

static void AutoDrive_UpdateIdlePosition(void)
{
    if (AutoDrive_GpsReady() == 0U) {
        return;
    }
    AutoDrive_PointFromGps(&idle_position, board_gps_get_state());
}

static void AutoDrive_UpdateGpsStepPoints(void)
{
    const board_gps_state_t *gps;

    gps = board_gps_get_state();
    if (gps == 0) {
        return;
    }
    AutoDrive_PointFromGps(&now_position, gps);
}

static void AutoDrive_GetSnapshotTargetPoint(AutoDrive_PointRaw_t *point)
{
    if (point == 0) {
        return;
    }
    AutoDrive_ClearPoint(point);

    if ((autodrive_mode == AUTO_DRIVE_GO_HOME_POSITION) ||
        (last_diag_reason == AUTODRIVE_DIAG_REASON_CMD_RETURN_HOME)) {
        AutoDrive_CopyPoint(point, &return_position);
    } else if ((autodrive_mode == AUTO_DRIVE_GO_FISISH_POSITION) ||
               (last_diag_reason == AUTODRIVE_DIAG_REASON_CMD_GOTO_POINT)) {
        AutoDrive_CopyPoint(point, &fish_position);
    } else {
        AutoDrive_CopyPoint(point, &autodrv_cfg.ret_point);
    }
}

static u8 AutoDrive_CanActivateTargetPoint(const AutoDrive_PointRaw_t *point,
                                           const AutoDrive_PointRaw_t *current_point)
{
    u16 distance;

    if ((AutoDrive_PointRawValid(point) == 0U) ||
        (AutoDrive_PointRawValid(current_point) == 0U) ||
        (AutoDrive_GpsReady() == 0U)) {
        return 0U;
    }
    distance = AutoDrive_GetDistanceNowToDestination((const u8 *)point,
                                                     (const u8 *)current_point);
    if ((distance > AUTODRIVE_MIN_ACTIVE_DISTANCE_M) &&
        (distance < AUTODRIVE_MAX_ACTIVE_DISTANCE_M)) {
        return 1U;
    }
    return 0U;
}

void AutoDrive_Init(void)
{
    AutoDriveCfg_Init();
    AutoDriveCfg_Load(&autodrv_cfg);
    if (autodrv_cfg.auto_ret_onoff == 0xFFU) {
        autodrv_cfg.auto_ret_onoff = 0x30U;
    }
    AutoDrive_CopyPoint(&return_position, &autodrv_cfg.ret_point);
    AutoDrive_ClearPoint(&fish_position);
    AutoDrive_ClearFishPoints();
    last_fish_cmd_index = 0U;
    last_fish_save_result = AUTODRIVE_FISH_SAVE_NONE;
    AutoDrive_ClearPoint(&last_fish_rx_point);
    last_fish_rx_ms = 0UL;
    last_fish_rx_valid = 0U;

    autodrive_switch = autodrv_cfg.auto_ret_onoff;
    autodrive_state = AUTO_DRIVE_IDLE;
    autodrive_mode = AUTO_DRIVE_CLOSE;
    autodrive_work_overtime = 0U;
    autodrive_fail_flag = 0U;
    link_alive_ticks = 0U;
    link_close_ticks = 0U;
    last_run_update_seq = 0UL;
    last_poll_tick_ms = platform_scheduler_get_tick_ms();
    last_link_tick_ms = last_poll_tick_ms;
    last_diag_reason = AUTODRIVE_DIAG_REASON_NONE;
    autodrive_target_heading_cd = 0U;
    autodrive_target_heading_valid = 0U;
    AutoDrive_ResetAlignTracker();
    AutoDrive_ResetApproachTracker();
    AutoDrive_StopMotion();
}

void AutoDrive_LinkAliveKick(void)
{
    link_alive_ticks = 0U;
    link_close_ticks = AUTODRIVE_MANUAL_CLOSE_TICKS;
}

void AutoDrive_LinkAliveTick(void)
{
    u32 now_ms;

    now_ms = platform_scheduler_get_tick_ms();
    if ((now_ms - last_link_tick_ms) < 10U) {
        return;
    }
    last_link_tick_ms = now_ms;

    if (link_alive_ticks < AUTODRIVE_MANUAL_TIMEOUT_TICKS) {
        link_alive_ticks++;
    } else {
        AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_LINK_TIMEOUT);
        link_alive_ticks = 0U;
    }

    if (link_close_ticks > 0U) {
        link_close_ticks--;
        if ((link_close_ticks == 0U) && (AutoDrive_GetMode() == AUTO_DRIVE_CLOSE)) {
            AutoDrive_StopMotion();
        }
    }
}

void AutoDrive_Poll(void)
{
    const board_gps_state_t *gps;
    const AutoDrive_PointRaw_t *target_point;
    u16 destination_distance;
    u32 now_ms;

    now_ms = platform_scheduler_get_tick_ms();
    if ((now_ms - last_poll_tick_ms) < 10U) {
        return;
    }
    last_poll_tick_ms = now_ms;

    gps = board_gps_get_state();
    if ((gps != 0) && (autodrive_state == AUTO_DRIVE_IDLE)) {
        AutoDrive_UpdateIdlePosition();
    }

    switch (autodrive_state) {
    case AUTO_DRIVE_IDLE:
        break;

    case AUTO_DRIVE_START:
        if (AutoDrive_TickWorkOvertime() == 0U) {
            break;
        }
        if (AutoDrive_GetTargetPoint(&target_point) == 0U) {
            AutoDrive_Stop();
            break;
        }
        if ((gps == 0) || (AutoDrive_GpsReady() == 0U) ||
            (app_get_heading_ready() == 0U)) {
            ShipControl_StopGpsNav();
            break;
        }
        AutoDrive_PointFromGps(&last_position, gps);
        AutoDrive_CopyPoint(&idle_position, &last_position);
        if (AutoDrive_UpdateTargetHeading(&last_position, target_point) == 0U) {
            AutoDrive_Stop();
            break;
        }
        AutoDrive_ResetApproachTracker();
        destination_distance =
            AutoDrive_GetDistanceNowToDestination((const u8 *)&last_position,
                                                  (const u8 *)target_point);
        AutoDrive_UpdateApproachSpeed(destination_distance);
        if (destination_distance <= AUTODRIVE_ARRIVE_DISTANCE_M) {
            AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_ARRIVE);
            autodrive_state = AUTO_DRIVE_ARRIVE;
            AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
            AutoDrive_StopMotion();
            break;
        }
        ShipControl_ResetYawHoldController();
        AutoDrive_ResetAlignTracker();
        last_run_update_seq = gps->update_sequence;
        autodrive_state = AUTO_DRIVE_ALIGN;
        (void)AutoDrive_ApplyAlignHeadingHold();
        break;

    case AUTO_DRIVE_ALIGN:
        if (AutoDrive_TickWorkOvertime() == 0U) {
            break;
        }
        if (AutoDrive_GetTargetPoint(&target_point) == 0U) {
            AutoDrive_Stop();
            break;
        }
        if ((gps != 0) && (AutoDrive_GpsReady() != 0U)) {
            AutoDrive_PointFromGps(&now_position, gps);
            destination_distance =
                AutoDrive_GetDistanceNowToDestination((const u8 *)&now_position,
                                                      (const u8 *)target_point);
            AutoDrive_UpdateApproachSpeed(destination_distance);
            if (destination_distance <= AUTODRIVE_ARRIVE_DISTANCE_M) {
                AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_ARRIVE);
                autodrive_state = AUTO_DRIVE_ARRIVE;
                AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
                AutoDrive_StopMotion();
                break;
            }
            if (AutoDrive_UpdateTargetHeading(&now_position, target_point) != 0U) {
                AutoDrive_CopyPoint(&last_position, &now_position);
                last_run_update_seq = gps->update_sequence;
            }
        }
        if (AutoDrive_ApplyAlignHeadingHold() == 0U) {
            autodrive_align_stable_ticks = 0U;
            break;
        }
        if (AutoDrive_AlignTargetReached() != 0U) {
            ShipControl_ResetYawHoldController();
            autodrive_state = AUTO_DRIVE_RUN;
            (void)AutoDrive_ApplyHeadingHold(autodrive_base_speed);
        }
        break;

    case AUTO_DRIVE_RUN:
        if (AutoDrive_TickWorkOvertime() == 0U) {
            break;
        }
        if ((gps != 0) && (AutoDrive_GpsReady() != 0U) &&
            (gps->update_sequence != last_run_update_seq)) {
            if (AutoDrive_GetTargetPoint(&target_point) == 0U) {
                AutoDrive_Stop();
                break;
            }
            AutoDrive_UpdateGpsStepPoints();
            destination_distance =
                AutoDrive_GetDistanceNowToDestination((const u8 *)&now_position,
                                                      (const u8 *)target_point);
            AutoDrive_UpdateApproachSpeed(destination_distance);
            if (destination_distance <= AUTODRIVE_ARRIVE_DISTANCE_M) {
                AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_ARRIVE);
                autodrive_state = AUTO_DRIVE_ARRIVE;
                AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
                AutoDrive_StopMotion();
                break;
            }
            nowrun_direction =
                AutoDrive_GetDirectionNowToDestination((const u8 *)&last_position,
                                                       (const u8 *)&now_position);
            if (AutoDrive_UpdateTargetHeading(&now_position, target_point) == 0U) {
                break;
            }
            AutoDrive_CopyPoint(&last_position, &now_position);
            last_run_update_seq = gps->update_sequence;
        }
        (void)AutoDrive_ApplyHeadingHold(autodrive_base_speed);
        break;

    case AUTO_DRIVE_ARRIVE:
    case AUTO_DRIVE_REJECT:
    case AUTO_DRIVE_TIMEOUT:
    case AUTO_DRIVE_STOP:
        AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
        break;

    default:
        autodrive_work_overtime = 0U;
        AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
        break;
    }
}

void AutoDrive_GetDebugSnapshot(AutoDrive_DebugSnapshot_t *snapshot)
{
    u16 heading;

    if (snapshot == 0) {
        return;
    }

    snapshot->state = autodrive_state;
    snapshot->mode = autodrive_mode;
    snapshot->auto_ret_onoff = autodrv_cfg.auto_ret_onoff;
    snapshot->fail_flag = autodrive_fail_flag;
    snapshot->last_reason = last_diag_reason;
    snapshot->gps_ready = AutoDrive_GpsReady();
    snapshot->sat_count = AutoDrive_GetSatCount();
    AutoDrive_ClearPoint(&snapshot->current_point);
    AutoDrive_GetCurrentPointRaw(&snapshot->current_point);
    AutoDrive_GetSnapshotTargetPoint(&snapshot->target_point);
    snapshot->can_activate_target =
        AutoDrive_CanActivateTargetPoint(&snapshot->target_point,
                                         &snapshot->current_point);
    if ((AutoDrive_PointRawValid(&snapshot->current_point) != 0U) &&
        (AutoDrive_PointRawValid(&snapshot->target_point) != 0U)) {
        snapshot->distance_to_target_m =
            AutoDrive_GetDistanceNowToDestination((const u8 *)&snapshot->current_point,
                                                  (const u8 *)&snapshot->target_point);
    } else {
        snapshot->distance_to_target_m = 0U;
    }
    heading = AutoDrive_GetRunHeadingDeg();
    if ((heading == 65535U) || (heading > 360U)) {
        heading = AutoDrive_GetStartHeadingDeg();
    }
    snapshot->current_heading_deg = heading;
    snapshot->target_heading_deg = destination_angle;
}
