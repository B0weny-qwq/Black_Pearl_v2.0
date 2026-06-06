/**
 * @file app.h
 * @brief Black Pearl 应用主循环对外入口。
 *
 * 本接口属于 App 层，负责初始化板级设备、服务、协议状态机和姿态航向组件。
 * 其它模块只能读取应用导出的姿态/航向快照，不直接访问传感器、无线或电机硬件。
 */

#ifndef __APP_H_
#define __APP_H_

#include "type_def.h"
#include "AHRS.h"

/**
 * @brief 初始化应用层和板级设备。
 *
 * 函数会初始化控制台、日志、GPS、IMU、磁力计、电源、无线、SPI-PS、电机和
 * AHRS/Heading 状态。底层硬件访问全部通过 BoardDevices 完成。
 * 该函数可能因板级外设 bring-up 和芯片复位短延时而阻塞。
 */
void app_init(void);

/**
 * @brief 执行一次应用主循环调度。
 *
 * 调度 GPS 解析、无线协议、SPI-PS 事件、AHRS 航向融合和电机服务。
 * 调用者应在主循环中高频调用；函数内部按各模块节拍限频。
 */
void app_loop(void);

/**
 * @brief 获取最近一次 AHRS 姿态快照。
 * @return 指向 AHRS 内部只读状态的指针；调用者不得修改其内容。
 */
const AHRS_State_t *app_get_attitude_state(void);

/**
 * @brief 查询应用层航向估计是否已建立。
 * @return 1 表示航向可用，0 表示尚未完成磁航向定零或 AHRS 未 ready。
 */
u8 app_get_heading_ready(void);

/**
 * @brief 获取未叠加北向校准 offset 的原始融合航向。
 * @return 0..35999 范围的航向角，单位 deg*100。
 */
u16 app_get_raw_heading_deg100(void);

/**
 * @brief 获取叠加北向校准 offset 后的导航航向。
 * @return 0..35999 范围的航向角，单位 deg*100。
 */
u16 app_get_heading_deg100(void);

/**
 * @brief 获取相对启动零点的航向偏差。
 * @return -18000..17999 附近的航向偏差，单位 deg*100。
 */
int16 app_get_heading_relative_deg100(void);

#endif
