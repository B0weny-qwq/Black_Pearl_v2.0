/**
 * @file north_calib.h
 * @brief D 键 GPS 北向校准状态机接口。
 *
 * 本模块属于 App 层，只编排校准状态、遥控输入、GPS/航向快照和 ShipControl 请求。
 * 持久化通过 Services/parameter_store 完成，电机输出仍统一由 ShipControl 写入。
 */

#ifndef __NORTH_CALIB_H__
#define __NORTH_CALIB_H__

#include "type_def.h"

typedef enum
{
    NORTH_CALIB_FAIL_NONE = 0,
    NORTH_CALIB_FAIL_GPS_NOT_READY,
    NORTH_CALIB_FAIL_HEADING_NOT_READY,
    NORTH_CALIB_FAIL_REMOTE_TIMEOUT,
    NORTH_CALIB_FAIL_AUTODRIVE_BUSY,
    NORTH_CALIB_FAIL_MANUAL_OVERRIDE,
    NORTH_CALIB_FAIL_DISTANCE_SHORT,
    NORTH_CALIB_FAIL_YAW_UNSTABLE,
    NORTH_CALIB_FAIL_OFFSET_JUMP,
    NORTH_CALIB_FAIL_EEPROM,
    NORTH_CALIB_FAIL_TIMEOUT,
    NORTH_CALIB_FAIL_USER_CANCEL
} NorthCalib_FailReason_t;

/**
 * @brief 初始化北向校准状态机并加载持久化 offset。
 *
 * 函数会从参数服务的 A/B 槽中选择最新有效记录；读不到有效记录时使用 0 offset。
 * 不直接访问 EEPROM 驱动，也不会启动电机。
 */
void NorthCalib_Init(void);

/**
 * @brief 更新最近一次遥控器输入快照。
 * @param lr 左右摇杆原始值，旧协议中心值为 100。
 * @param ud 前后摇杆原始值，旧协议中心值为 100。
 * @param key 当前按键字节。
 * @param now_ms 输入采样时间，单位 ms。
 */
void NorthCalib_UpdateRemoteInput(u8 lr, u8 ud, u8 key, u32 now_ms);

/**
 * @brief 尝试启动一次北向校准。
 * @return 1 表示已进入 CHECK_READY；0 表示当前 busy 或被拒绝。
 */
u8 NorthCalib_RequestStart(void);

/**
 * @brief 推进北向校准状态机。
 *
 * 需要由 10 ms 调度链路周期调用。状态机内部通过 ShipControl_RequestGpsAlign/Nav()
 * 请求控制权，不直接写电机。
 */
void NorthCalib_Poll(void);

/**
 * @brief 取消当前校准流程。
 * @param reason NorthCalib_FailReason_t 失败或取消原因。
 */
void NorthCalib_Cancel(u8 reason);

/**
 * @brief 查询北向校准是否占用控制权。
 * @return 1 表示校准状态机未处于 IDLE，0 表示空闲。
 */
u8 NorthCalib_IsBusy(void);

/**
 * @brief 获取当前生效北向 offset。
 * @return 有符号 offset，单位 deg*100，范围约为 -18000..17999。
 */
int16 NorthCalib_GetHeadingOffsetCd(void);

#endif
