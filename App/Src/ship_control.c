#include "ship_control.h"
#include "app.h"
#include "app_config.h"
#include "board_motor.h"
#include "PID.h"
#include "logger.h"
#include "platform_scheduler.h"

#define SHIP_CONTROL_TAG                  "CTRL"
#define SHIP_CONTROL_DATA_TAG             "DATA"
#define SHIP_LR_DEAD_LOW                  90U
#define SHIP_LR_DEAD_HIGH                 110U
#define SHIP_FB_DEAD_LOW                  90U
#define SHIP_FB_DEAD_HIGH                 110U
#define SHIP_CENTER_STOP_CONFIRM_FRAMES   2U
#define SHIP_TURN_COMPARE_BIAS            5U

#define SHIP_CONTROL_REASON_MANUAL_OPEN   20U
#define SHIP_CONTROL_REASON_MANUAL_YAW    21U
#define SHIP_CONTROL_REASON_CRUISE        22U
#define SHIP_CONTROL_REASON_GPS_NAV       23U

typedef enum
{
    SHIP_CONTROL_MOTION_STOP = 0,
    SHIP_CONTROL_MOTION_FORWARD,
    SHIP_CONTROL_MOTION_BACKWARD,
    SHIP_CONTROL_MOTION_LEFT,
    SHIP_CONTROL_MOTION_RIGHT
} ShipControl_Motion_t;

typedef struct
{
    u8 initialized;
    u8 mode;
    u8 lr;
    u8 ud;
    u8 key;
    u8 manual_valid;
    u8 manual_accelerator;
    int32 filtered_lr_q8;
    int32 filtered_ud_q8;
    u32 manual_last_apply_ms;
    u32 auto_last_apply_ms;
    u8 center_stop_count;
    u8 yaw_hold_active;
    u16 yaw_hold_target_cd;
    int16 yaw_hold_error_cd;
    int16 yaw_hold_error_ctrl;
    int16 yaw_hold_output;
    int16 yaw_hold_last_yaw_speed;
    u32 yaw_hold_last_update_ms;
    u8 yaw_hold_stable_count;
    u32 cruise_start_ms;
    int16 left_speed;
    int16 right_speed;
    int16 throttle_speed;
    int16 base_speed;
    int16 steering_speed;
    int16 yaw_diff_speed;
    u32 motor_last_log_ms;
    int16 motor_last_log_left;
    int16 motor_last_log_right;
    u8 motor_last_log_mode;
    u8 motor_last_log_motion;
    u8 last_logged_mode;
    ShipControl_Motion_t motion;
} ShipControl_Runtime_t;

static ShipControl_Runtime_t ship_ctrl;
static PID_Controller_t ship_ctrl_yaw_pid;
static PID_Controller_t ship_ctrl_align_pid;

/* 基础限幅和角度规整：所有电机输出在进入 BoardDevices 前都先走这里。 */
static int16 ShipControl_LimitSpeed(int16 speed)
{
    if (speed > SHIP_MOTOR_OUTPUT_MAX_COMMAND) {
        return SHIP_MOTOR_OUTPUT_MAX_COMMAND;
    }
    if (speed < -SHIP_MOTOR_OUTPUT_MAX_COMMAND) {
        return -SHIP_MOTOR_OUTPUT_MAX_COMMAND;
    }
    return speed;
}

static int16 ShipControl_AbsSpeed(int16 speed)
{
    return (speed >= 0) ? speed : (int16)(-speed);
}

static u8 ShipControl_AbsAxisDiff(u8 value)
{
    return (value > SHIP_AXIS_CENTER) ?
           (u8)(value - SHIP_AXIS_CENTER) :
           (u8)(SHIP_AXIS_CENTER - value);
}

static int16 ShipControl_WrapSignedCd(int32 angle_cd)
{
    while (angle_cd >= 18000L) {
        angle_cd -= 36000L;
    }
    while (angle_cd < -18000L) {
        angle_cd += 36000L;
    }
    return (int16)angle_cd;
}

static u16 ShipControl_WrapUnsignedCd(int32 angle_cd)
{
    while (angle_cd >= 36000L) {
        angle_cd -= 36000L;
    }
    while (angle_cd < 0L) {
        angle_cd += 36000L;
    }
    return (u16)angle_cd;
}

static void ShipControl_ResetAxisFilter(void)
{
    ship_ctrl.filtered_lr_q8 = ((int32)SHIP_AXIS_CENTER << 8);
    ship_ctrl.filtered_ud_q8 = ((int32)SHIP_AXIS_CENTER << 8);
}

static u8 ShipControl_YawHoldGateStable(void)
{
    if (ship_ctrl.yaw_hold_stable_count < SHIP_YAW_HOLD_STEER_STABLE_FRAMES) {
        ship_ctrl.yaw_hold_stable_count++;
        return 0U;
    }
    return 1U;
}

/* 状态跳变日志：用于上位机控制模式卡片和现场判断停机原因。 */
#if (SHIP_CONTROL_LOG_ENABLE != 0U)
static void ShipControl_LogModeEvent(u8 old_mode, u8 new_mode, u8 reason)
{
    LOGI(SHIP_CONTROL_TAG,
         "event=mode old=%u new=%u reason=%u yaw=%u tgt=%u",
         (u16)old_mode,
         (u16)new_mode,
         (u16)reason,
         (u16)ship_ctrl.yaw_hold_active,
         ship_ctrl.yaw_hold_target_cd);
}
#else
#define ShipControl_LogModeEvent(old_mode, new_mode, reason) do { } while (0)
#endif

#if (SHIP_CONTROL_LOG_ENABLE != 0U)
static void ShipControl_SetMode(u8 mode, u8 reason)
#else
static void ShipControl_SetMode(u8 mode)
#endif
{
    u8 old_mode;

    old_mode = ship_ctrl.mode;
    ship_ctrl.mode = mode;
    if ((old_mode != mode) || (ship_ctrl.last_logged_mode != mode)) {
#if (SHIP_CONTROL_LOG_ENABLE != 0U)
        ShipControl_LogModeEvent(old_mode, mode, reason);
#endif
        ship_ctrl.last_logged_mode = mode;
    }
}

/* 电机输出日志：这里是上位机“电机输出”卡片追到底层 board_motor 的唯一 App 源头。 */
#if (SHIP_CONTROL_LOG_ENABLE != 0U)
static void ShipControl_LogMotorOutput(u8 force)
{
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
}
#else
#define ShipControl_LogMotorOutput(force) do { } while (0)
#endif

static void ShipControl_SetMotorTargets(int16 left_speed, int16 right_speed)
{
    ship_ctrl.left_speed = ShipControl_LimitSpeed(left_speed);
    ship_ctrl.right_speed = ShipControl_LimitSpeed(right_speed);
    /* 最终电机写入点：App 不碰 PWM/引脚，只调用 BoardDevices。 */
    (void)board_motor_set_both_speed(ship_ctrl.left_speed, ship_ctrl.right_speed);
    ShipControl_LogMotorOutput(0U);
}

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

/* 手动摇杆链路：原始 0x11 轴值 -> 滤波 -> 死区/曲线 -> 左右电机差速。 */
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

static void ShipControl_UpdateManualAcceleratorRaw(u8 left_right, u8 front_back)
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

/* 航向闭环链路：目标航向和 app_get_heading_* 快照 -> PID/阻尼/限幅 -> 左右电机。 */
static u8 ShipControl_ApplyYawHoldTargetEx(u16 target_heading_cd,
                                           int16 base_speed,
                                           u8 mode,
                                           u8 use_align_pid)
{
    u32 now_ms;
    u16 current_heading_cd;
    int16 yaw_error_cd;
    int16 yaw_error_ctrl;
    int16 yaw_base_speed;
    int16 yaw_output;
    int16 left_speed;
    int16 right_speed;
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
        yaw_error_ctrl = ShipControl_YawErrorToControl(yaw_error_cd);
        ship_ctrl.yaw_hold_error_cd = yaw_error_cd;
        ship_ctrl.yaw_hold_error_ctrl = yaw_error_ctrl;
        if (yaw_error_ctrl == 0) {
            PID_Reset(pid);
            ship_ctrl.yaw_hold_output = ShipControl_ApplyYawHoldDamping(0);
        } else {
            pid_output = PID_UpdateTarget(pid, yaw_error_ctrl, 0);
            ship_ctrl.yaw_hold_output = ShipControl_ApplyYawHoldDamping(pid_output);
        }
    }

    if (mode == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) {
        yaw_base_speed = ShipControl_ApplyCruiseBaseRamp(base_speed);
    } else {
        yaw_base_speed = ShipControl_ApplyYawHoldBaseDerate(base_speed,
                                                            ship_ctrl.yaw_hold_error_cd);
    }
    yaw_output = ShipControl_YawControlToSpeed(ship_ctrl.yaw_hold_output,
                                               yaw_base_speed);
#if SHIP_YAW_HOLD_OUTPUT_SIGN < 0
    yaw_output = (int16)(-yaw_output);
#endif
    if (use_align_pid != 0U) {
        yaw_output = ShipControl_LimitGpsAlignYawOutput(yaw_output);
    }
    yaw_output = ShipControl_ApplyYawOutputSlew(yaw_output);
    left_speed = ShipControl_LimitSpeed((int16)(yaw_base_speed + yaw_output));
    right_speed = ShipControl_LimitSpeed((int16)(yaw_base_speed - yaw_output));

#if (SHIP_CONTROL_LOG_ENABLE != 0U)
    ShipControl_SetMode(mode,
                        (mode == SHIP_CONTROL_MODE_MANUAL_YAW_HOLD) ?
                        SHIP_CONTROL_REASON_MANUAL_YAW :
                        ((mode == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) ?
                         SHIP_CONTROL_REASON_CRUISE :
                         SHIP_CONTROL_REASON_GPS_NAV));
#else
    ShipControl_SetMode(mode);
#endif
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
    ShipControl_SetMotorTargets(left_speed, right_speed);
    return 1U;
}

/* 开环差速：只有手动摇杆明显转向或 AHRS 航向不可用时使用。 */
static void ShipControl_ApplyOpenLoop(ShipControl_Motion_t motion,
                                      int16 left_speed,
                                      int16 right_speed,
                                      int16 throttle_speed,
                                      int16 steering_speed)
{
    ShipControl_SetMode((motion == SHIP_CONTROL_MOTION_STOP) ?
                        SHIP_CONTROL_MODE_MANUAL_IDLE :
                        SHIP_CONTROL_MODE_MANUAL_OPEN_LOOP
#if (SHIP_CONTROL_LOG_ENABLE != 0U)
                        ,
                        SHIP_CONTROL_REASON_MANUAL_OPEN
#endif
                        );
    ship_ctrl.motion = motion;
    ship_ctrl.throttle_speed = throttle_speed;
    ship_ctrl.base_speed = 0;
    ship_ctrl.steering_speed = steering_speed;
    ship_ctrl.yaw_diff_speed = 0;
    ShipControl_SetMotorTargets(left_speed, right_speed);
}

/* 手动控制入口：普通开环、直线 yaw 自稳、摇杆回中停机都在这里判定。 */
static void ShipControl_ApplyManualControl(void)
{
    int16 throttle_speed;
    int16 steering_speed;
    int16 left_speed;
    int16 right_speed;
    int16 abs_throttle;
    int16 abs_steering;
    int16 abs_left;
    int16 abs_right;
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
#if (SHIP_CONTROL_LOG_ENABLE != 0U)
        ShipControl_SetMode(SHIP_CONTROL_MODE_MANUAL_IDLE, SHIP_CONTROL_REASON_MANUAL_OPEN);
#else
        ShipControl_SetMode(SHIP_CONTROL_MODE_MANUAL_IDLE);
#endif
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
    abs_left = ShipControl_AbsSpeed(left_speed);
    abs_right = ShipControl_AbsSpeed(right_speed);
    max_input = (abs_left >= abs_right) ? abs_left : abs_right;
    diff_input = ShipControl_AbsSpeed((int16)(left_speed - right_speed));
    diff_gate = (int16)(((int32)max_input *
                         (int32)SHIP_MANUAL_YAW_HOLD_DIFF_PERCENT) / 100L);
    yaw_gate_open = 0U;

    if ((throttle_speed > 0) &&
        (max_input > 0) &&
        (diff_input < diff_gate)) {
        yaw_gate_open = 1U;
        if (ShipControl_YawHoldGateStable() != 0U) {
            if (app_get_heading_ready() != 0U) {
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
                                               ship_ctrl.base_speed,
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
    ShipControl_SetMode((reason == SHIP_CONTROL_STOP_REASON_FAILSAFE) ?
                        SHIP_CONTROL_MODE_FAILSAFE_STOP :
                        SHIP_CONTROL_MODE_STOP
#if (SHIP_CONTROL_LOG_ENABLE != 0U)
                        ,
                        reason
#endif
                        );
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
