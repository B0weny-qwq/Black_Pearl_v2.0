#include "ship_control_internal.h"
#include "app.h"

static u8 ShipControl_ConfirmCenterStop(void)
{
    if (ship_ctrl.motion == SHIP_CONTROL_MOTION_STOP) {
        ship_ctrl.center_stop_count = 0U;
        return 1U;
    }

    if (ship_ctrl.center_stop_count < SHIP_CENTER_STOP_CONFIRM_FRAMES) {
        ship_ctrl.center_stop_count++;
    }
    return (ship_ctrl.center_stop_count >= SHIP_CENTER_STOP_CONFIRM_FRAMES) ? 1U : 0U;
}

static int16 ShipControl_FilterAxis(u8 raw, int32 *state_q8)
{
    int32 target_q8;

    target_q8 = ((int32)raw << 8);
    *state_q8 += ((target_q8 - *state_q8) >> SHIP_AXIS_FILTER_SHIFT);
    return (int16)((*state_q8 + 128L) >> 8);
}

static int16 ShipControl_ApplyAxisCurve(int16 value,
                                        int16 deadband,
                                        int16 min_command,
                                        int16 max_command)
{
    int16 delta;
    int16 sign;
    int16 magnitude;
    int16 range;
    int32 command;

    delta = (int16)(value - (int16)SHIP_AXIS_CENTER);
    if (delta == 0) {
        return 0;
    }

    sign = (delta > 0) ? 1 : -1;
    magnitude = (delta > 0) ? delta : (int16)(-delta);
    if (magnitude <= deadband) {
        return 0;
    }

    range = (int16)(SHIP_RC_AXIS_MAX_DELTA - deadband);
    magnitude = (int16)(magnitude - deadband);
    if (range <= 0) {
        return 0;
    }
    if (magnitude > range) {
        magnitude = range;
    }

    command = min_command;
    command += ((int32)magnitude * (int32)magnitude *
                (int32)(max_command - min_command)) /
               ((int32)range * (int32)range);
    if (command > max_command) {
        command = max_command;
    }

    return (int16)(sign * (int16)command);
}

/* 手动摇杆链路：0x11 原始轴值 -> 滤波 -> 曲线 -> 开环或 yaw 自稳。 */
static int16 ShipControl_ThrottleToSignedSpeed(int16 value)
{
    return ShipControl_ApplyAxisCurve(value,
                                      SHIP_THROTTLE_DEADBAND,
                                      SHIP_THROTTLE_MIN_COMMAND,
                                      SHIP_THROTTLE_MAX_COMMAND);
}

static int16 ShipControl_SteeringToSignedSpeed(int16 value)
{
    return ShipControl_ApplyAxisCurve(value,
                                      SHIP_STEERING_DEADBAND,
                                      0,
                                      SHIP_STEERING_MAX_COMMAND);
}

void ShipControl_UpdateManualAcceleratorRaw(u8 left_right, u8 front_back)
{
    u8 abs_left_right;
    u8 abs_front_back;

    abs_left_right = ShipControl_AbsAxisDiff(left_right);
    abs_front_back = ShipControl_AbsAxisDiff(front_back);
    abs_front_back = (u8)(abs_front_back + 5U);

    if ((abs_front_back > abs_left_right) &&
        ((abs_left_right > 10U) || (abs_front_back > 20U)) &&
        (front_back > 110U)) {
        ship_ctrl.manual_accelerator = (u8)(front_back - SHIP_AXIS_CENTER);
    } else {
        ship_ctrl.manual_accelerator = 0U;
    }
}

static void ShipControl_ApplyOpenLoop(ShipControl_Motion_t motion,
                                      int16 left_speed,
                                      int16 right_speed,
                                      int16 throttle_speed,
                                      int16 steering_speed)
{
    ShipControl_SetModeInternal((motion == SHIP_CONTROL_MOTION_STOP) ?
                                SHIP_CONTROL_MODE_MANUAL_IDLE :
                                SHIP_CONTROL_MODE_MANUAL_OPEN_LOOP,
                                SHIP_CONTROL_REASON_MANUAL_OPEN);
    ship_ctrl.motion = motion;
    ship_ctrl.throttle_speed = throttle_speed;
    ship_ctrl.base_speed = 0;
    ship_ctrl.steering_speed = steering_speed;
    ship_ctrl.yaw_diff_speed = 0;
    ShipControl_SetMotorTargets(left_speed, right_speed);
}

void ShipControl_ApplyManualControl(void)
{
    int16 throttle_speed;
    int16 steering_speed;
    int16 left_speed;
    int16 right_speed;
    int16 abs_throttle;
    int16 abs_steering;
    int16 max_input;
    int16 diff_input;
    int16 diff_gate;
    u8 yaw_gate_open;
    ShipControl_Motion_t motion;

    if ((ship_ctrl.lr >= SHIP_LR_DEAD_LOW) && (ship_ctrl.lr <= SHIP_LR_DEAD_HIGH) &&
        (ship_ctrl.ud >= SHIP_FB_DEAD_LOW) && (ship_ctrl.ud <= SHIP_FB_DEAD_HIGH)) {
        if (ShipControl_ConfirmCenterStop() == 0U) {
            return;
        }
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_MANUAL_CENTER);
        ship_ctrl.manual_valid = 1U;
        ShipControl_SetModeInternal(SHIP_CONTROL_MODE_MANUAL_IDLE,
                                    SHIP_CONTROL_REASON_MANUAL_OPEN);
        return;
    }
    ship_ctrl.center_stop_count = 0U;

    throttle_speed = ShipControl_ThrottleToSignedSpeed(
        ShipControl_FilterAxis(ship_ctrl.ud, &ship_ctrl.filtered_ud_q8));
    steering_speed = ShipControl_SteeringToSignedSpeed(
        ShipControl_FilterAxis(ship_ctrl.lr, &ship_ctrl.filtered_lr_q8));
    abs_throttle = ShipControl_AbsSpeed(throttle_speed);
    abs_steering = ShipControl_AbsSpeed(steering_speed);
    left_speed = ShipControl_LimitSpeed((int16)(throttle_speed + steering_speed));
    right_speed = ShipControl_LimitSpeed((int16)(throttle_speed - steering_speed));
    max_input = (ShipControl_AbsSpeed(left_speed) >= ShipControl_AbsSpeed(right_speed)) ?
                ShipControl_AbsSpeed(left_speed) : ShipControl_AbsSpeed(right_speed);
    diff_input = ShipControl_AbsSpeed((int16)(left_speed - right_speed));
    diff_gate = (int16)(((int32)max_input *
                         (int32)SHIP_MANUAL_YAW_HOLD_DIFF_PERCENT) / 100L);
    yaw_gate_open = 0U;

    if ((throttle_speed > 0) && (max_input > 0) && (diff_input < diff_gate)) {
        yaw_gate_open = 1U;
        if ((ShipControl_YawHoldGateStable() != 0U) &&
            (app_get_heading_ready() != 0U)) {
            if (ship_ctrl.mode != SHIP_CONTROL_MODE_MANUAL_YAW_HOLD) {
                ShipControl_ResetYawHoldController();
                ship_ctrl.yaw_hold_target_cd = app_get_heading_deg100();
                ship_ctrl.yaw_hold_stable_count = SHIP_YAW_HOLD_STEER_STABLE_FRAMES;
            }
            (void)ShipControl_ApplyYawHoldTargetEx(ship_ctrl.yaw_hold_target_cd,
                                                   throttle_speed,
                                                   SHIP_CONTROL_MODE_MANUAL_YAW_HOLD,
                                                   0U);
            return;
        }
    }
    if (yaw_gate_open == 0U) {
        ship_ctrl.yaw_hold_stable_count = 0U;
        ship_ctrl.yaw_hold_last_yaw_speed = 0;
    }
    if (ship_ctrl.mode == SHIP_CONTROL_MODE_MANUAL_YAW_HOLD) {
        ShipControl_ResetYawHoldController();
    }

    if ((abs_throttle + SHIP_TURN_COMPARE_BIAS) >= abs_steering) {
        motion = (throttle_speed >= 0) ?
                 SHIP_CONTROL_MOTION_FORWARD :
                 SHIP_CONTROL_MOTION_BACKWARD;
    } else {
        motion = (steering_speed >= 0) ?
                 SHIP_CONTROL_MOTION_RIGHT :
                 SHIP_CONTROL_MOTION_LEFT;
    }

    ShipControl_ApplyOpenLoop(motion,
                              left_speed,
                              right_speed,
                              throttle_speed,
                              steering_speed);
}
