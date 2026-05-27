/**
 * @file ship_control.h
 * @brief 船体手动、巡航和 GPS 航向保持控制状态机。
 *
 * 本模块拥有电机输出控制权。协议层只提交摇杆、巡航和 GPS 导航请求，
 * 不直接写电机。
 */

#ifndef __SHIP_CONTROL_H__
#define __SHIP_CONTROL_H__

#include "type_def.h"

typedef enum
{
    SHIP_CONTROL_MODE_STOP = 0,
    SHIP_CONTROL_MODE_POWER_GUARD,
    SHIP_CONTROL_MODE_WAIT_HEADING_READY,
    SHIP_CONTROL_MODE_MANUAL_IDLE,
    SHIP_CONTROL_MODE_MANUAL_OPEN_LOOP,
    SHIP_CONTROL_MODE_MANUAL_YAW_HOLD,
    SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD,
    SHIP_CONTROL_MODE_GPS_NAV_HEADING_HOLD,
    SHIP_CONTROL_MODE_FAILSAFE_STOP
} ShipControl_Mode_t;

typedef enum
{
    SHIP_CONTROL_STOP_REASON_NONE = 0,
    SHIP_CONTROL_STOP_REASON_MANUAL_CENTER,
    SHIP_CONTROL_STOP_REASON_MANUAL_TIMEOUT,
    SHIP_CONTROL_STOP_REASON_REMOTE_TIMEOUT,
    SHIP_CONTROL_STOP_REASON_CRUISE_KEY,
    SHIP_CONTROL_STOP_REASON_GPS_NAV_STOP,
    SHIP_CONTROL_STOP_REASON_HEADING_LOST,
    SHIP_CONTROL_STOP_REASON_FAILSAFE
} ShipControl_StopReason_t;

/**
 * @brief 初始化船体运动控制状态机和 PID 控制器。
 *
 * 函数会清零手动输入滤波、自动航向保持状态和电机输出缓存；实际电机 PWM
 * 初始化由 BoardDevices 层负责。
 */
void ShipControl_Init(void);

/**
 * @brief 推进运动控制状态机。
 * @param now_ms 当前调度时刻，单位 ms。
 *
 * 函数按内部节拍刷新手动控制、巡航航向保持、GPS 导航航向保持和电机目标输出。
 */
void ShipControl_Tick(u32 now_ms);

/**
 * @brief 更新遥控手动输入。
 * @param lr 左右摇杆原始值，旧协议中心值为 100。
 * @param ud 前后摇杆原始值，旧协议中心值为 100。
 * @param key 当前按键字节。
 * @param now_ms 输入采样时刻，单位 ms。
 */
void ShipControl_UpdateManualInput(u8 lr, u8 ud, u8 key, u32 now_ms);

/**
 * @brief 请求进入定航向巡航模式。
 * @param heading_cd 目标航向，单位 deg*100。
 * @param base_speed 基础前进速度命令。
 */
void ShipControl_RequestCruise(u16 heading_cd, int16 base_speed);

/**
 * @brief 请求 GPS 导航前的原地对准航向。
 * @param target_heading_cd 目标航向，单位 deg*100。
 */
void ShipControl_RequestGpsAlign(u16 target_heading_cd);

/**
 * @brief 请求 GPS 导航航向保持。
 * @param target_heading_cd 目标航向，单位 deg*100。
 * @param base_speed 基础前进速度命令。
 */
void ShipControl_RequestGpsNav(u16 target_heading_cd, int16 base_speed);

/**
 * @brief 停止当前运动控制并切换到安全停止状态。
 * @param reason ShipControl_StopReason_t 或内部诊断原因码。
 */
void ShipControl_Stop(u8 reason);

/** @brief 退出 GPS 导航相关模式并停止电机目标输出。 */
void ShipControl_StopGpsNav(void);

/** @brief 重置航向保持 PID 和误差历史，保留当前模式。 */
void ShipControl_ResetYawHoldController(void);

/**
 * @brief 查询当前是否处于自动控制模式。
 * @return 1 表示巡航/GPS 自动模式，0 表示手动、停止或故障保护模式。
 */
u8 ShipControl_IsAutoMode(void);

/**
 * @brief 获取当前运动控制模式。
 * @return ShipControl_Mode_t 枚举值。
 */
u8 ShipControl_GetMode(void);

/**
 * @brief 获取手动模式下当前油门加速等级。
 * @return 0 表示未加速，非 0 表示手动油门正在输出。
 */
u8 ShipControl_GetManualAccelerator(void);

#endif
