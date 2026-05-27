/**
 * @file ship_control_internal.h
 * @brief 船体控制 App 内部拆分接口。
 *
 * 本头文件只给 `App/Src/ship_control*.c` 使用，用来共享旧工程迁移后的运行态、
 * 电机输出写入点和 yaw 闭环入口。外部模块仍只包含 `ship_control.h`。
 */

#ifndef __SHIP_CONTROL_INTERNAL_H__
#define __SHIP_CONTROL_INTERNAL_H__

#include "ship_control.h"
#include "app_config.h"
#include "PID.h"

#define SHIP_CONTROL_TAG                  "CTRL"
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

/** @brief 船体运动方向，用于控制日志和开环差速判定。 */
typedef enum
{
    SHIP_CONTROL_MOTION_STOP = 0,
    SHIP_CONTROL_MOTION_FORWARD,
    SHIP_CONTROL_MOTION_BACKWARD,
    SHIP_CONTROL_MOTION_LEFT,
    SHIP_CONTROL_MOTION_RIGHT
} ShipControl_Motion_t;

/** @brief 船体控制运行态，保持旧工程 ShipControl 的状态集中语义。 */
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

extern ShipControl_Runtime_t ship_ctrl;
extern PID_Controller_t ship_ctrl_yaw_pid;
extern PID_Controller_t ship_ctrl_align_pid;

/** @brief 将电机命令限制到旧工程允许范围。 */
int16 ShipControl_LimitSpeed(int16 speed);

/** @brief 计算带符号速度绝对值。 */
int16 ShipControl_AbsSpeed(int16 speed);

/** @brief 计算摇杆原始值到中心值的偏差绝对值。 */
u8 ShipControl_AbsAxisDiff(u8 value);

/** @brief 规整有符号航向误差到 -18000..17999。 */
int16 ShipControl_WrapSignedCd(int32 angle_cd);

/** @brief 规整无符号航向到 0..35999。 */
u16 ShipControl_WrapUnsignedCd(int32 angle_cd);

/** @brief 重置手动摇杆滤波器。 */
void ShipControl_ResetAxisFilter(void);

/** @brief 判断手动 yaw 自稳入口是否已连续稳定。 */
u8 ShipControl_YawHoldGateStable(void);

/** @brief 写入左右电机目标；唯一通往 BoardDevices 电机 API 的内部入口。 */
void ShipControl_SetMotorTargets(int16 left_speed, int16 right_speed);

/** @brief 切换控制模式并按需输出上位机控制模式日志。 */
void ShipControl_SetModeInternal(u8 mode, u8 reason);

/** @brief 输出电机状态日志；force 非 0 时忽略节流。 */
void ShipControl_LogMotorOutput(u8 force);

/** @brief 更新手动油门加速等级，供低电保护和外部状态使用。 */
void ShipControl_UpdateManualAcceleratorRaw(u8 left_right, u8 front_back);

/** @brief 执行一次手动控制输出。 */
void ShipControl_ApplyManualControl(void);

/** @brief 执行 yaw 航向保持输出。 */
u8 ShipControl_ApplyYawHoldTargetEx(u16 target_heading_cd,
                                    int16 base_speed,
                                    u8 mode,
                                    u8 use_align_pid);

#endif
