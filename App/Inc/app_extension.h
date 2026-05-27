/**
 * @file app_extension.h
 * @brief App 层外包扩展入口。
 *
 * 本文件是甲方预留给外包二次业务的稳定插入点。新增按键动作、LED 闪烁、
 * 蜂鸣器提示或现场联调观察逻辑时，优先在 App/Src/app_extension.c 中实现；
 * 如需访问新硬件，先在 BoardDevices 增加板级 API，再从本扩展层调用。
 */

#ifndef __APP_EXTENSION_H__
#define __APP_EXTENSION_H__

#include "type_def.h"
#include "ship_protocol.h"

/**
 * @brief 初始化 App 扩展逻辑。
 *
 * 默认实现为空。外包逻辑如需建立本地状态、清零计数器或读取参数，可放在这里。
 * 不允许在此直接包含 STC 寄存器头文件或操作裸引脚。
 */
void app_extension_init(void);

/**
 * @brief 执行一次 App 扩展轮询。
 * @param now_ms 当前系统毫秒计数。
 *
 * 主循环每轮都会调用该接口。实现必须短小、非阻塞；需要定时动作时使用
 * now_ms 做差分计时，不要使用长延时。
 */
void app_extension_poll(u32 now_ms);

/**
 * @brief 观察一条船端协议事件。
 * @param event 协议层输出的事件快照，不能为空。
 *
 * 按键边沿、B/C/D 语义动作、返航/钓点/电量/SPI-PS 事件都会经过该接口。
 * 核心控制逻辑仍由 ship_protocol、ShipControl 和 AutoDrive 完成；本接口用于
 * 追加业务动作，例如按 A 键闪 LED、按 B 键切换提示灯模式等。
 */
void app_extension_on_ship_event(const ship_protocol_event_snapshot_t *event);

#endif
