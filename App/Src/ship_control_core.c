#include "ship_control_internal.h"
#include "board_motor.h"
#include "logger.h"
#include "platform_scheduler.h"

ShipControl_Runtime_t ship_ctrl;
PID_Controller_t ship_ctrl_yaw_pid;
PID_Controller_t ship_ctrl_align_pid;

/* 公共数学工具：保持旧控制层整数限幅和角度规整习惯。 */
int16 ShipControl_LimitSpeed(int16 speed)
{
    if (speed > SHIP_MOTOR_OUTPUT_MAX_COMMAND) {
        return SHIP_MOTOR_OUTPUT_MAX_COMMAND;
    }
    if (speed < -SHIP_MOTOR_OUTPUT_MAX_COMMAND) {
        return -SHIP_MOTOR_OUTPUT_MAX_COMMAND;
    }
    return speed;
}

int16 ShipControl_AbsSpeed(int16 speed)
{
    return (speed >= 0) ? speed : (int16)(-speed);
}

u8 ShipControl_AbsAxisDiff(u8 value)
{
    return (value > SHIP_AXIS_CENTER) ?
           (u8)(value - SHIP_AXIS_CENTER) :
           (u8)(SHIP_AXIS_CENTER - value);
}

int16 ShipControl_WrapSignedCd(int32 angle_cd)
{
    while (angle_cd >= 18000L) {
        angle_cd -= 36000L;
    }
    while (angle_cd < -18000L) {
        angle_cd += 36000L;
    }
    return (int16)angle_cd;
}

u16 ShipControl_WrapUnsignedCd(int32 angle_cd)
{
    while (angle_cd >= 36000L) {
        angle_cd -= 36000L;
    }
    while (angle_cd < 0L) {
        angle_cd += 36000L;
    }
    return (u16)angle_cd;
}

void ShipControl_ResetAxisFilter(void)
{
    ship_ctrl.filtered_lr_q8 = ((int32)SHIP_AXIS_CENTER << 8);
    ship_ctrl.filtered_ud_q8 = ((int32)SHIP_AXIS_CENTER << 8);
}

u8 ShipControl_YawHoldGateStable(void)
{
    if (ship_ctrl.yaw_hold_stable_count < SHIP_YAW_HOLD_STEER_STABLE_FRAMES) {
        ship_ctrl.yaw_hold_stable_count++;
        return 0U;
    }
    return 1U;
}

void ShipControl_SetModeInternal(u8 mode, u8 reason)
{
    u8 old_mode;

    old_mode = ship_ctrl.mode;
    ship_ctrl.mode = mode;
    if ((old_mode != mode) || (ship_ctrl.last_logged_mode != mode)) {
#if (SHIP_CONTROL_LOG_ENABLE != 0U)
        LOGI(SHIP_CONTROL_TAG,
             "event=mode old=%u new=%u reason=%u yaw=%u tgt=%u",
             (u16)old_mode,
             (u16)mode,
             (u16)reason,
             (u16)ship_ctrl.yaw_hold_active,
             ship_ctrl.yaw_hold_target_cd);
#else
        (void)reason;
#endif
        ship_ctrl.last_logged_mode = mode;
    }
}

void ShipControl_LogMotorOutput(u8 force)
{
#if (SHIP_CONTROL_LOG_ENABLE != 0U)
    u32 now_ms;

    now_ms = platform_scheduler_get_tick_ms();
    if ((force == 0U) &&
        (ship_ctrl.left_speed == ship_ctrl.motor_last_log_left) &&
        (ship_ctrl.right_speed == ship_ctrl.motor_last_log_right) &&
        (ship_ctrl.mode == ship_ctrl.motor_last_log_mode) &&
        ((u8)ship_ctrl.motion == ship_ctrl.motor_last_log_motion) &&
        ((now_ms - ship_ctrl.motor_last_log_ms) < SHIP_MOT_LOG_PERIOD_MS)) {
        return;
    }

    ship_ctrl.motor_last_log_ms = now_ms;
    ship_ctrl.motor_last_log_left = ship_ctrl.left_speed;
    ship_ctrl.motor_last_log_right = ship_ctrl.right_speed;
    ship_ctrl.motor_last_log_mode = ship_ctrl.mode;
    ship_ctrl.motor_last_log_motion = (u8)ship_ctrl.motion;

    LOGI(SHIP_CONTROL_TAG,
         "out m=%u mo=%u th=%d base=%d st=%d df=%d l=%d r=%d",
         (u16)ship_ctrl.mode,
         (u16)ship_ctrl.motion,
         ship_ctrl.throttle_speed,
         ship_ctrl.base_speed,
         ship_ctrl.steering_speed,
         ship_ctrl.yaw_diff_speed,
         ship_ctrl.left_speed,
         ship_ctrl.right_speed);
#else
    (void)force;
#endif
}

void ShipControl_SetMotorTargets(int16 left_speed, int16 right_speed)
{
    ship_ctrl.left_speed = ShipControl_LimitSpeed(left_speed);
    ship_ctrl.right_speed = ShipControl_LimitSpeed(right_speed);
    /* 最终电机写入点：App 不碰 PWM/引脚，只调用 BoardDevices。 */
    (void)board_motor_set_both_speed(ship_ctrl.left_speed, ship_ctrl.right_speed);
    ShipControl_LogMotorOutput(0U);
}

void ShipControl_ResetYawHoldController(void)
{
    ship_ctrl.yaw_hold_active = 0U;
    ship_ctrl.yaw_hold_target_cd = 0U;
    ship_ctrl.yaw_hold_error_cd = 0;
    ship_ctrl.yaw_hold_error_ctrl = 0;
    ship_ctrl.yaw_hold_output = 0;
    ship_ctrl.yaw_hold_last_yaw_speed = 0;
    ship_ctrl.yaw_hold_last_update_ms = 0UL;
    ship_ctrl.yaw_hold_stable_count = 0U;
    ship_ctrl.cruise_start_ms = 0UL;
    PID_Reset(&ship_ctrl_yaw_pid);
    PID_Reset(&ship_ctrl_align_pid);
}
