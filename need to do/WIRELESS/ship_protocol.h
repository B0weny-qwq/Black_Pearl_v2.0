/**
 * @file    ship_protocol.h
 * @brief   船端无线业务协议解析与调度接口。
 * @author  boweny
 * @date    2026-05-07
 * @version v1.2
 *
 * @details
 * 定义船端无线帧格式的帧头、帧尾、命令字和对外调度函数。
 * 本模块是旧版 `Wireless_other/wirelessProtocal.c` 的船端业务移植层，
 * 目标是保持旧遥控器不可修改时的数据格式和调度行为一致，而不是重新设计协议。
 *
 * 旧业务帧格式固定为：
 * `AA | len | cmd | payload... | xor | BB`，其中 `len = 2 + payload_len`，
 * `xor` 为从 `len` 到 payload 末尾所有字节异或。无线底层返回的是 LT8920
 * RF payload，不保证刚好是一帧协议数据；调度器内部按旧版逻辑逐字节寻找
 * `0xAA`、按长度字段收帧并校验后分发。
 *
 * 当前已对齐的旧业务：
 * - 发送 10 次 `PAIR_REQ(0x10)` 后先按 `RF_Encrypt_Config()` 只写同步寄存器，
 *   等待旧版 30 tick 节拍后再切到工作 RX。
 * - `PAIR_RSP(0x0F)` 仅在有效窗口内置配对成功并打印。
 * - 任意合法协议帧分发结束后立即回发一次 `GPS_REPORT(0x12)`。
 * - `GPS_REPORT(0x12)` payload 保持老版 15 字节，不新增字段。
 * - `THROTTLE(0x11)` 空口仍保持老版 `lr/ud/key` 载荷，但应用层已加入
 *   轴滤波、差速映射和可选 yaw-hold 手动直线保持，不再等同旧版纯开环。
 * - `A/C/D` 按键入口保留老版语义，其中 A 键灯控因当前 v1.1 板级引脚未确认，
 *   只保留日志提示，不在本层擅自绑定到未知引脚。
 *
 * @note
 * 当前主路径应调用 ShipProtocol_RunScheduler()，由调度器统一消费无线接收
 * 队列并维护配对状态。ShipProtocol_Poll() 仅保留为兼容入口，不应与调度器
 * 在同一主循环中同时启用，以免重复消费无线接收队列。
 *
 * @warning
 * 遥控器程序不可修改，后续修改本模块时必须优先对齐 `Wireless_other`
 * 的包格式、通道/同步字派生和回包节奏；不要随意新增 payload 字段。
 *
 * @see     Code_boweny/Device/WIRELESS/ship_protocol.c
 * @see     Wireless_other/wirelessProtocal.c
 */

#ifndef __SHIP_PROTOCOL_H__
#define __SHIP_PROTOCOL_H__

#include "config.h"

#define SHIP_PROTO_HEAD          0xAAU  /**< 旧版协议帧头字节。 */
#define SHIP_PROTO_TAIL          0xBBU  /**< 旧版协议帧尾字节。 */
#define SHIP_PROTO_MAX_FRAME_LEN 64U    /**< 本地接收缓冲区最大长度，单位 byte。 */

#define SHIP_CMD_PAIR_RSP        0x0FU  /**< 旧遥控器配对响应命令。 */
#define SHIP_CMD_PAIR            0x10U  /**< 船端配对请求命令，payload 固定 4 字节 seed。 */
#define SHIP_CMD_THROTTLE        0x11U  /**< 遥控器油门/转向/按键命令，payload 为 lr/ud/key。 */
#define SHIP_CMD_GPS_REPORT      0x12U  /**< 船端 GPS/状态回传命令，payload 固定 15 字节。 */
#define SHIP_CMD_RETURN_HOME     0x13U  /**< 遥控器设置返航点命令。 */
#define SHIP_CMD_GOTO_POINT      0x14U  /**< 遥控器设置目标点命令。 */
#define SHIP_CMD_RETURN_SWITCH   0x15U  /**< 遥控器自动返航开关命令。 */
#define SHIP_CMD_AUTODRIVE_DIAG  0x16U  /**< 船端返航/去点诊断上报命令，payload 固定长度。 */

/**
 * @brief   轮询无线接收数据并尝试解析协议帧。
 * @return  无。
 *
 * @note
 * 兼容入口。当前最小业务模式下由 ShipProtocol_RunScheduler() 负责收包和调度。
 * 不要在同一主循环里同时调用两个入口。
 */
void ShipProtocol_Poll(void);

/**
 * @brief      解析一帧已经完整截出的旧版协议数据。
 * @param[in]  frame      指向完整协议帧的指针，必须以 `0xAA` 开始、`0xBB` 结束。
 * @param[in]  frame_len  完整协议帧长度，单位 byte。
 * @return     SUCCESS=解析并处理成功，WIRELESS_ERR_* 表示参数、校验或业务处理失败。
 *
 * @note
 * 正常主路径不直接把 RF payload 传给本函数；调度器内部会先按旧版
 * `WirelessProtocal_Receive_Handle()` 行为逐字节截帧。
 */
s8 ShipProtocol_ParseFrame(const u8 *frame, u8 frame_len);

/**
 * @brief   运行协议层周期任务。
 * @return  无。
 *
 * @details
 * 用于执行固定 seed 配对、配对响应窗口、工作信道监听和旧版协议流式解析。
 * 每收到一帧合法协议数据都会按旧版业务回发一次 `SHIP_CMD_GPS_REPORT(0x12)`；
 * 收到 `SHIP_CMD_THROTTLE(0x11)` 时更新遥控输入，并交给 `ShipControl`
 * 统一仲裁手动开环、手动自稳、定速巡航和 GPS 航向保持。
 *
 * @warning
 * 当前工程保留 `0x13/0x14/0x15` 的旧版点位格式，并实际调用
 * `AutoDrive_SetReturnPositionRaw()`、`AutoDrive_SetFishPositionRaw()`、
 * `AutoDrive_SetSwitchRaw()`；低电和链路超时也会通过 `AutoDrive_TriggerReturn()`
 * 进入返航入口。
 */
void ShipProtocol_RunScheduler(void);

/**
 * @brief   查询船端协议是否已经完成配对。
 * @return  1=已配对，0=未配对。
 */
u8 ShipProtocol_IsPaired(void);

/**
 * @brief      兼容入口：把 GPS 航向保持请求转交给 ShipControl。
 * @param[in]  target_heading_cd  目标航向，单位 0.01 度，范围按 0..35999 归一化。
 * @param[in]  base_speed         基础前进速度。
 * @return     1=请求已提交。
 *
 * @note
 * 新代码应直接调用 `ShipControl_RequestGpsNav()`；保留本接口只是避免旧引用
 * 立即失效。
 */
u8 ShipProtocol_ApplyYawHoldTarget(u16 target_heading_cd, int16 base_speed);

/**
 * @brief   兼容入口：复位 ShipControl yaw-hold 状态。
 */
void ShipProtocol_ResetYawHoldController(void);

#endif
