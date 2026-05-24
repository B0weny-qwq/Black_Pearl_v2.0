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

void ShipControl_Init(void);
void ShipControl_Tick(u32 now_ms);
void ShipControl_UpdateManualInput(u8 lr, u8 ud, u8 key, u32 now_ms);
void ShipControl_RequestCruise(u16 heading_cd, int16 base_speed);
void ShipControl_RequestGpsAlign(u16 target_heading_cd);
void ShipControl_RequestGpsNav(u16 target_heading_cd, int16 base_speed);
void ShipControl_Stop(u8 reason);
void ShipControl_StopGpsNav(void);
void ShipControl_ResetYawHoldController(void);
u8 ShipControl_IsAutoMode(void);
u8 ShipControl_GetMode(void);
u8 ShipControl_GetManualAccelerator(void);

#endif
