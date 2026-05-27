#include "autodrive_internal.h"

void AutoDrive_GetSnapshotTargetPoint(AutoDrive_PointRaw_t *point)
{
    if (point == 0) {
        return;
    }
    AutoDrive_ClearPoint(point);

    /* 诊断帧优先展示当前任务目标，其次展示最近一次命令关联点。 */
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

u8 AutoDrive_CanActivateTargetPoint(const AutoDrive_PointRaw_t *point,
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
