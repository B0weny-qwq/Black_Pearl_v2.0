#include "ship_control_internal.h"
#include "app.h"
#include "platform_scheduler.h"

static int16 ShipControl_YawErrorToControl(int16 yaw_error_cd)
{
    int16 sign;
    int32 abs_error_cd;
    int32 active_range_cd;
    int32 control;

    if (yaw_error_cd == 0) {
        return 0;
    }

    sign = (yaw_error_cd > 0) ? 1 : -1;
    abs_error_cd = (yaw_error_cd > 0) ? (int32)yaw_error_cd : -(int32)yaw_error_cd;
    if (abs_error_cd <= (int32)SHIP_YAW_HOLD_DEADBAND_CD) {
        return 0;
    }

    active_range_cd = (int32)SHIP_YAW_HOLD_FULL_ERROR_CD -
                      (int32)SHIP_YAW_HOLD_DEADBAND_CD;
    if (active_range_cd <= 0L) {
        return (int16)(sign * SHIP_YAW_HOLD_OUTPUT_LIMIT);
    }

    abs_error_cd -= (int32)SHIP_YAW_HOLD_DEADBAND_CD;
    if (abs_error_cd >= active_range_cd) {
        return (int16)(sign * SHIP_YAW_HOLD_OUTPUT_LIMIT);
    }

    control = (abs_error_cd * (int32)SHIP_YAW_HOLD_OUTPUT_LIMIT) / active_range_cd;
    return (int16)(sign * (int16)control);
}

/* yaw 阻尼严格跟随旧工程：使用 AHRS yaw 轴角速度快照做阻尼。 */
static int16 ShipControl_ApplyYawHoldDamping(int16 yaw_control)
{
    const AHRS_State_t *att;
    int32 damp;
    int32 output;

    att = app_get_attitude_state();
    damp = 0L;
    if (att != 0) {
        damp = ((int32)att->gyro_z_dps100 *
                (int32)SHIP_YAW_HOLD_GYRO_DAMP_Q10) /
               (100L * 1024L);
    }
    output = (int32)yaw_control - damp;
    if (output > (int32)SHIP_YAW_HOLD_OUTPUT_LIMIT) {
        output = (int32)SHIP_YAW_HOLD_OUTPUT_LIMIT;
    } else if (output < -(int32)SHIP_YAW_HOLD_OUTPUT_LIMIT) {
        output = -(int32)SHIP_YAW_HOLD_OUTPUT_LIMIT;
    }
    return (int16)output;
}

static int16 ShipControl_ApplyYawOutputSlew(int16 yaw_output)
{
    int16 delta;
    int16 step;

    step = (int16)SHIP_YAW_HOLD_DIFF_SLEW_PER_STEP;
    if (step <= 0) {
        ship_ctrl.yaw_hold_last_yaw_speed = yaw_output;
        return yaw_output;
    }

    delta = (int16)(yaw_output - ship_ctrl.yaw_hold_last_yaw_speed);
    if (delta > step) {
        yaw_output = (int16)(ship_ctrl.yaw_hold_last_yaw_speed + step);
    } else if (delta < (int16)(-step)) {
        yaw_output = (int16)(ship_ctrl.yaw_hold_last_yaw_speed - step);
    }
    ship_ctrl.yaw_hold_last_yaw_speed = yaw_output;
    return yaw_output;
}

static int16 ShipControl_LimitGpsAlignYawOutput(int16 yaw_output)
{
    int32 limit;

    limit = ((int32)SHIP_MOTOR_OUTPUT_MAX_COMMAND *
             (int32)SHIP_GPS_ALIGN_DIFF_PERCENT) / 100L;
    if (yaw_output > (int16)limit) {
        return (int16)limit;
    }
    if (yaw_output < (int16)(-limit)) {
        return (int16)(-limit);
    }
    return yaw_output;
}

static int16 ShipControl_ApplyCruiseBaseRamp(int16 base_speed)
{
    u32 now_ms;
    u32 elapsed_ms;
    int16 sign;
    int32 abs_base;
    int32 min_base;
    int32 ramped_base;

    if ((base_speed == 0) || (ship_ctrl.cruise_start_ms == 0UL)) {
        return base_speed;
    }
    sign = (base_speed >= 0) ? 1 : -1;
    abs_base = (base_speed >= 0) ? (int32)base_speed : -(int32)base_speed;
    min_base = (int32)SHIP_CRUISE_RAMP_MIN_BASE;
    if (min_base >= abs_base) {
        return base_speed;
    }

    now_ms = platform_scheduler_get_tick_ms();
    elapsed_ms = now_ms - ship_ctrl.cruise_start_ms;
    if (elapsed_ms >= SHIP_CRUISE_RAMP_MS) {
        return base_speed;
    }

    ramped_base = min_base +
                  (((abs_base - min_base) * (int32)elapsed_ms) /
                   (int32)SHIP_CRUISE_RAMP_MS);
    if (ramped_base > abs_base) {
        ramped_base = abs_base;
    }
    return (int16)(sign * (int16)ramped_base);
}

static int16 ShipControl_ApplyYawHoldBaseDerate(int16 base_speed, int16 yaw_error_cd)
{
    int16 sign;
    int32 abs_base;
    int32 abs_error_cd;
    int32 span_cd;
    int32 max_base;

    if (base_speed == 0) {
        return 0;
    }

    sign = (base_speed >= 0) ? 1 : -1;
    abs_base = (base_speed >= 0) ? (int32)base_speed : -(int32)base_speed;
    abs_error_cd = (yaw_error_cd >= 0) ? (int32)yaw_error_cd : -(int32)yaw_error_cd;
    if ((abs_error_cd <= SHIP_YAW_HOLD_DERATE_START_CD) ||
        (abs_base <= SHIP_YAW_HOLD_DERATE_MIN_BASE)) {
        return base_speed;
    }

    if (abs_error_cd >= SHIP_YAW_HOLD_DERATE_FULL_CD) {
        max_base = SHIP_YAW_HOLD_DERATE_MIN_BASE;
    } else {
        span_cd = (int32)SHIP_YAW_HOLD_DERATE_FULL_CD -
                  (int32)SHIP_YAW_HOLD_DERATE_START_CD;
        max_base = abs_base -
                   ((abs_base - SHIP_YAW_HOLD_DERATE_MIN_BASE) *
                    (abs_error_cd - SHIP_YAW_HOLD_DERATE_START_CD)) / span_cd;
    }
    if (abs_base > max_base) {
        abs_base = max_base;
    }
    return (int16)(sign * (int16)abs_base);
}

static int16 ShipControl_YawControlToSpeed(int16 yaw_control, int16 base_speed)
{
    int32 scale;
    int32 yaw_limit;
    int32 yaw_speed;

    scale = (base_speed >= 0) ? (int32)base_speed : -(int32)base_speed;
    if (scale == 0L) {
        scale = (int32)SHIP_MOTOR_OUTPUT_MAX_COMMAND;
    }

    yaw_limit = (scale * (int32)SHIP_YAW_HOLD_DIFF_LIMIT_PERMILLE) / 1000L;
    if (yaw_limit <= 0L) {
        return 0;
    }

    yaw_speed = ((int32)yaw_control * yaw_limit) /
                (int32)SHIP_YAW_HOLD_OUTPUT_LIMIT;
    if (yaw_speed > yaw_limit) {
        yaw_speed = yaw_limit;
    } else if (yaw_speed < -yaw_limit) {
        yaw_speed = -yaw_limit;
    }
    return ShipControl_LimitSpeed((int16)yaw_speed);
}

u8 ShipControl_ApplyYawHoldTargetEx(u16 target_heading_cd,
                                    int16 base_speed,
                                    u8 mode,
                                    u8 use_align_pid)
{
    u32 now_ms;
    u16 current_heading_cd;
    int16 yaw_error_cd;
    int16 yaw_base_speed;
    int16 yaw_output;
    int16 pid_output;
    PID_Controller_t *pid;

    if (app_get_heading_ready() == 0U) {
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_HEADING_LOST);
        return 0U;
    }

    now_ms = platform_scheduler_get_tick_ms();
    current_heading_cd = app_get_heading_deg100();
    target_heading_cd = ShipControl_WrapUnsignedCd((int32)target_heading_cd);

    if (ship_ctrl.yaw_hold_active == 0U) {
        ship_ctrl.yaw_hold_active = 1U;
        ship_ctrl.yaw_hold_output = 0;
        ship_ctrl.yaw_hold_last_yaw_speed = 0;
        ship_ctrl.yaw_hold_last_update_ms = now_ms - SHIP_YAW_HOLD_PERIOD_MS;
        ship_ctrl.yaw_hold_stable_count = SHIP_YAW_HOLD_STEER_STABLE_FRAMES;
        PID_Reset(&ship_ctrl_yaw_pid);
        PID_Reset(&ship_ctrl_align_pid);
        PID_SetTarget(&ship_ctrl_yaw_pid, 0);
        PID_SetTarget(&ship_ctrl_align_pid, 0);
    }

    pid = (use_align_pid != 0U) ? &ship_ctrl_align_pid : &ship_ctrl_yaw_pid;
    ship_ctrl.yaw_hold_target_cd = target_heading_cd;
    if ((now_ms - ship_ctrl.yaw_hold_last_update_ms) >= SHIP_YAW_HOLD_PERIOD_MS) {
        ship_ctrl.yaw_hold_last_update_ms = now_ms;
        yaw_error_cd = ShipControl_WrapSignedCd((int32)target_heading_cd -
                                                (int32)current_heading_cd);
        ship_ctrl.yaw_hold_error_cd = yaw_error_cd;
        ship_ctrl.yaw_hold_error_ctrl = ShipControl_YawErrorToControl(yaw_error_cd);
        if (ship_ctrl.yaw_hold_error_ctrl == 0) {
            PID_Reset(pid);
            ship_ctrl.yaw_hold_output = ShipControl_ApplyYawHoldDamping(0);
        } else {
            pid_output = PID_UpdateTarget(pid, ship_ctrl.yaw_hold_error_ctrl, 0);
            ship_ctrl.yaw_hold_output = ShipControl_ApplyYawHoldDamping(pid_output);
        }
    }

    yaw_base_speed = (mode == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) ?
                     ShipControl_ApplyCruiseBaseRamp(base_speed) :
                     ShipControl_ApplyYawHoldBaseDerate(base_speed,
                                                        ship_ctrl.yaw_hold_error_cd);
    yaw_output = ShipControl_YawControlToSpeed(ship_ctrl.yaw_hold_output,
                                               yaw_base_speed);
#if SHIP_YAW_HOLD_OUTPUT_SIGN < 0
    yaw_output = (int16)(-yaw_output);
#endif
    if (use_align_pid != 0U) {
        yaw_output = ShipControl_LimitGpsAlignYawOutput(yaw_output);
    }
    yaw_output = ShipControl_ApplyYawOutputSlew(yaw_output);
    ShipControl_SetModeInternal(mode,
                                (mode == SHIP_CONTROL_MODE_MANUAL_YAW_HOLD) ?
                                SHIP_CONTROL_REASON_MANUAL_YAW :
                                ((mode == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) ?
                                 SHIP_CONTROL_REASON_CRUISE :
                                 SHIP_CONTROL_REASON_GPS_NAV));

    if (yaw_base_speed > 0) {
        ship_ctrl.motion = SHIP_CONTROL_MOTION_FORWARD;
    } else if (yaw_base_speed < 0) {
        ship_ctrl.motion = SHIP_CONTROL_MOTION_BACKWARD;
    } else if (yaw_output > 0) {
        ship_ctrl.motion = SHIP_CONTROL_MOTION_RIGHT;
    } else if (yaw_output < 0) {
        ship_ctrl.motion = SHIP_CONTROL_MOTION_LEFT;
    } else {
        ship_ctrl.motion = SHIP_CONTROL_MOTION_STOP;
    }
    ship_ctrl.throttle_speed = base_speed;
    ship_ctrl.base_speed = yaw_base_speed;
    ship_ctrl.steering_speed = 0;
    ship_ctrl.yaw_diff_speed = yaw_output;
    ShipControl_SetMotorTargets(ShipControl_LimitSpeed((int16)(yaw_base_speed + yaw_output)),
                                ShipControl_LimitSpeed((int16)(yaw_base_speed - yaw_output)));
    return 1U;
}
