#include "ship_control_internal.h"
#include "app.h"
#include "board_motor.h"
#include "platform_scheduler.h"

/* 公开 API 保持旧工程调用语义，内部实现已拆到 ship_control_* 小文件。 */
void ShipControl_Init(void)
{
    ship_ctrl.initialized = 1U;
    ship_ctrl.mode = SHIP_CONTROL_MODE_STOP;
    ship_ctrl.lr = SHIP_AXIS_CENTER;
    ship_ctrl.ud = SHIP_AXIS_CENTER;
    ship_ctrl.key = 0U;
    ship_ctrl.manual_valid = 0U;
    ship_ctrl.manual_accelerator = 0U;
    ShipControl_ResetAxisFilter();
    ship_ctrl.manual_last_apply_ms = 0UL;
    ship_ctrl.auto_last_apply_ms = 0UL;
    ship_ctrl.center_stop_count = 0U;
    ship_ctrl.yaw_hold_active = 0U;
    ship_ctrl.yaw_hold_target_cd = 0U;
    ship_ctrl.yaw_hold_error_cd = 0;
    ship_ctrl.yaw_hold_error_ctrl = 0;
    ship_ctrl.yaw_hold_output = 0;
    ship_ctrl.yaw_hold_last_yaw_speed = 0;
    ship_ctrl.yaw_hold_last_update_ms = 0UL;
    ship_ctrl.yaw_hold_stable_count = 0U;
    ship_ctrl.cruise_start_ms = 0UL;
    ship_ctrl.left_speed = 0;
    ship_ctrl.right_speed = 0;
    ship_ctrl.throttle_speed = 0;
    ship_ctrl.base_speed = 0;
    ship_ctrl.steering_speed = 0;
    ship_ctrl.yaw_diff_speed = 0;
    ship_ctrl.motor_last_log_ms = 0UL;
    ship_ctrl.motor_last_log_left = 0;
    ship_ctrl.motor_last_log_right = 0;
    ship_ctrl.motor_last_log_mode = SHIP_CONTROL_MODE_STOP;
    ship_ctrl.motor_last_log_motion = SHIP_CONTROL_MOTION_STOP;
    ship_ctrl.last_logged_mode = SHIP_CONTROL_MODE_STOP;
    ship_ctrl.motion = SHIP_CONTROL_MOTION_STOP;

    PID_Init(&ship_ctrl_yaw_pid,
             SHIP_YAW_HOLD_KP_Q10,
             SHIP_YAW_HOLD_KI_Q10,
             SHIP_YAW_HOLD_KD_Q10,
             -SHIP_YAW_HOLD_OUTPUT_LIMIT,
             SHIP_YAW_HOLD_OUTPUT_LIMIT,
             -((int32)SHIP_YAW_HOLD_OUTPUT_LIMIT * 64L),
             ((int32)SHIP_YAW_HOLD_OUTPUT_LIMIT * 64L));
    PID_Init(&ship_ctrl_align_pid,
             SHIP_GPS_ALIGN_KP_Q10,
             SHIP_GPS_ALIGN_KI_Q10,
             SHIP_GPS_ALIGN_KD_Q10,
             -SHIP_YAW_HOLD_OUTPUT_LIMIT,
             SHIP_YAW_HOLD_OUTPUT_LIMIT,
             -((int32)SHIP_YAW_HOLD_OUTPUT_LIMIT * 64L),
             ((int32)SHIP_YAW_HOLD_OUTPUT_LIMIT * 64L));
    ShipControl_LogMotorOutput(1U);
}

void ShipControl_Tick(u32 now_ms)
{
    if (ship_ctrl.initialized == 0U) {
        ShipControl_Init();
    }

    if ((ship_ctrl.manual_valid != 0U) &&
        ((ship_ctrl.mode == SHIP_CONTROL_MODE_MANUAL_IDLE) ||
         (ship_ctrl.mode == SHIP_CONTROL_MODE_MANUAL_OPEN_LOOP) ||
         (ship_ctrl.mode == SHIP_CONTROL_MODE_MANUAL_YAW_HOLD) ||
         (ship_ctrl.mode == SHIP_CONTROL_MODE_STOP))) {
        if ((now_ms - ship_ctrl.manual_last_apply_ms) >= SHIP_MANUAL_CONTROL_PERIOD_MS) {
            ship_ctrl.manual_last_apply_ms = now_ms;
            ShipControl_UpdateManualAcceleratorRaw(ship_ctrl.lr, ship_ctrl.ud);
            ShipControl_ApplyManualControl();
        }
    } else {
        ship_ctrl.center_stop_count = 0U;
    }

    if ((ship_ctrl.mode == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) &&
        ((now_ms - ship_ctrl.auto_last_apply_ms) >= SHIP_MANUAL_CONTROL_PERIOD_MS)) {
        ship_ctrl.auto_last_apply_ms = now_ms;
        (void)ShipControl_ApplyYawHoldTargetEx(ship_ctrl.yaw_hold_target_cd,
                                               ship_ctrl.throttle_speed,
                                               SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD,
                                               0U);
    }
}

void ShipControl_UpdateManualInput(u8 lr, u8 ud, u8 key, u32 now_ms)
{
    if (ship_ctrl.initialized == 0U) {
        ShipControl_Init();
    }

    ship_ctrl.lr = lr;
    ship_ctrl.ud = ud;
    ship_ctrl.key = key;
    ship_ctrl.manual_valid = 1U;
    ShipControl_UpdateManualAcceleratorRaw(lr, ud);
    ship_ctrl.manual_last_apply_ms = now_ms;

    if ((ship_ctrl.mode == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) ||
        (ship_ctrl.mode == SHIP_CONTROL_MODE_GPS_NAV_HEADING_HOLD)) {
        return;
    }

    ShipControl_ApplyManualControl();
}

void ShipControl_RequestCruise(u16 heading_cd, int16 base_speed)
{
    if (ship_ctrl.initialized == 0U) {
        ShipControl_Init();
    }
    if (app_get_heading_ready() == 0U) {
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_HEADING_LOST);
        return;
    }

    ShipControl_ResetYawHoldController();
    ship_ctrl.auto_last_apply_ms = platform_scheduler_get_tick_ms();
    ship_ctrl.cruise_start_ms = ship_ctrl.auto_last_apply_ms;
    ship_ctrl.yaw_hold_target_cd = ShipControl_WrapUnsignedCd((int32)heading_cd);
    ship_ctrl.base_speed = ShipControl_LimitSpeed(base_speed);
    if (ship_ctrl.base_speed == 0) {
        ship_ctrl.base_speed = SHIP_CRUISE_BASE_SPEED;
    }
    (void)ShipControl_ApplyYawHoldTargetEx(ship_ctrl.yaw_hold_target_cd,
                                           ship_ctrl.base_speed,
                                           SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD,
                                           0U);
}

void ShipControl_RequestGpsNav(u16 target_heading_cd, int16 base_speed)
{
    if (ship_ctrl.initialized == 0U) {
        ShipControl_Init();
    }
    if (app_get_heading_ready() == 0U) {
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_HEADING_LOST);
        return;
    }
    if (ship_ctrl.mode != SHIP_CONTROL_MODE_GPS_NAV_HEADING_HOLD) {
        ShipControl_ResetYawHoldController();
    }
    ship_ctrl.auto_last_apply_ms = platform_scheduler_get_tick_ms();
    ship_ctrl.yaw_hold_target_cd = ShipControl_WrapUnsignedCd((int32)target_heading_cd);
    ship_ctrl.base_speed = ShipControl_LimitSpeed(base_speed);
    (void)ShipControl_ApplyYawHoldTargetEx(ship_ctrl.yaw_hold_target_cd,
                                           ship_ctrl.base_speed,
                                           SHIP_CONTROL_MODE_GPS_NAV_HEADING_HOLD,
                                           0U);
}

void ShipControl_RequestGpsAlign(u16 target_heading_cd)
{
    if (ship_ctrl.initialized == 0U) {
        ShipControl_Init();
    }
    if (app_get_heading_ready() == 0U) {
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_HEADING_LOST);
        return;
    }
    if (ship_ctrl.mode != SHIP_CONTROL_MODE_GPS_NAV_HEADING_HOLD) {
        ShipControl_ResetYawHoldController();
    }
    ship_ctrl.auto_last_apply_ms = platform_scheduler_get_tick_ms();
    ship_ctrl.yaw_hold_target_cd = ShipControl_WrapUnsignedCd((int32)target_heading_cd);
    ship_ctrl.base_speed = 0;
    (void)ShipControl_ApplyYawHoldTargetEx(ship_ctrl.yaw_hold_target_cd,
                                           0,
                                           SHIP_CONTROL_MODE_GPS_NAV_HEADING_HOLD,
                                           1U);
}

void ShipControl_Stop(u8 reason)
{
    u8 was_running;

    if (ship_ctrl.initialized == 0U) {
        ShipControl_Init();
    }

    was_running = ((ship_ctrl.mode != SHIP_CONTROL_MODE_STOP) ||
                   (ship_ctrl.left_speed != 0) ||
                   (ship_ctrl.right_speed != 0)) ? 1U : 0U;

    ShipControl_ResetYawHoldController();
    ShipControl_ResetAxisFilter();
    ShipControl_SetModeInternal((reason == SHIP_CONTROL_STOP_REASON_FAILSAFE) ?
                                SHIP_CONTROL_MODE_FAILSAFE_STOP :
                                SHIP_CONTROL_MODE_STOP,
                                reason);
    ship_ctrl.manual_valid = 0U;
    ship_ctrl.manual_accelerator = 0U;
    ship_ctrl.center_stop_count = 0U;
    ship_ctrl.auto_last_apply_ms = 0UL;
    ship_ctrl.throttle_speed = 0;
    ship_ctrl.base_speed = 0;
    ship_ctrl.steering_speed = 0;
    ship_ctrl.yaw_diff_speed = 0;
    ship_ctrl.left_speed = 0;
    ship_ctrl.right_speed = 0;
    ship_ctrl.motion = SHIP_CONTROL_MOTION_STOP;
    (void)board_motor_stop_all();
    if (was_running != 0U) {
        ShipControl_LogMotorOutput(1U);
    }
}

void ShipControl_StopGpsNav(void)
{
    if (ship_ctrl.mode == SHIP_CONTROL_MODE_GPS_NAV_HEADING_HOLD) {
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_GPS_NAV_STOP);
    }
}

u8 ShipControl_IsAutoMode(void)
{
    if ((ship_ctrl.mode == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) ||
        (ship_ctrl.mode == SHIP_CONTROL_MODE_GPS_NAV_HEADING_HOLD)) {
        return 1U;
    }
    return 0U;
}

u8 ShipControl_GetMode(void)
{
    return ship_ctrl.mode;
}

u8 ShipControl_GetManualAccelerator(void)
{
    return ship_ctrl.manual_accelerator;
}
