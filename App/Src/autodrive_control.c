#include "autodrive_internal.h"
#include "app.h"
#include "ship_control.h"

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

void AutoDrive_ResetApproachTracker(void)
{
    autodrive_base_speed = AUTODRIVE_CRUISE_BASE_SPEED;
}

void AutoDrive_ResetAlignTracker(void)
{
    autodrive_align_ticks = 0U;
    autodrive_align_stable_ticks = 0U;
    autodrive_align_zero_seen = 0U;
    autodrive_align_prev_valid = 0U;
    autodrive_align_prev_error_cd = 0;
}

u8 AutoDrive_AlignTargetReached(void)
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

    /* 先看到误差过零，再满足容差，避免刚启动时误判对准完成。 */
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

void AutoDrive_UpdateApproachSpeed(u16 distance_m)
{
    autodrive_base_speed = AutoDrive_CalcBaseSpeed(distance_m);
}

u8 AutoDrive_GetTargetPoint(const AutoDrive_PointRaw_t **target)
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

u8 AutoDrive_UpdateTargetHeading(const AutoDrive_PointRaw_t *current_point,
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

u8 AutoDrive_ApplyHeadingHold(u16 base_speed)
{
    if ((autodrive_target_heading_valid == 0U) ||
        (app_get_heading_ready() == 0U)) {
        ShipControl_StopGpsNav();
        return 0U;
    }
    ShipControl_RequestGpsNav(autodrive_target_heading_cd, (int16)base_speed);
    return 1U;
}

u8 AutoDrive_ApplyAlignHeadingHold(void)
{
    if ((autodrive_target_heading_valid == 0U) ||
        (app_get_heading_ready() == 0U)) {
        ShipControl_StopGpsNav();
        return 0U;
    }
    ShipControl_RequestGpsAlign(autodrive_target_heading_cd);
    return 1U;
}

u16 AutoDrive_GetStartHeadingDeg(void)
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

u16 AutoDrive_GetRunHeadingDeg(void)
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
