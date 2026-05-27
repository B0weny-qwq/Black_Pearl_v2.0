#include "autodrive_internal.h"
#include "app.h"
#include "platform_scheduler.h"
#include "ship_control.h"

void AutoDrive_UpdateIdlePosition(void)
{
    if (AutoDrive_GpsReady() == 0U) {
        return;
    }
    AutoDrive_PointFromGps(&idle_position, board_gps_get_state());
}

void AutoDrive_UpdateGpsStepPoints(void)
{
    const board_gps_state_t *gps;

    gps = board_gps_get_state();
    if (gps == 0) {
        return;
    }
    AutoDrive_PointFromGps(&now_position, gps);
}

/* AutoDrive 状态机：START -> ALIGN -> RUN，输出只通过 ShipControl_RequestGps*。 */
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
