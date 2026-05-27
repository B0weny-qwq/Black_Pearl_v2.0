/**
 * @file board_lt8920.h
 * @brief Black Pearl 板级 LT8920 无线芯片初始化接口。
 *
 * 本文件属于 BoardDevices 层，用于隐藏 LT8920 与 KCT8206 的板级引脚、
 * SPI 路由和默认上电时序。当前只提供最小 bring-up 初始化接口。
 */

#ifndef __BOARD_LT8920_H__
#define __BOARD_LT8920_H__

#include "type_def.h"

#define BOARD_LT8920_OK               0
#define BOARD_LT8920_ERR_PARAM       -1
#define BOARD_LT8920_ERR_DRIVER      -2
#define BOARD_LT8920_ERR_VERIFY      -3
#define BOARD_LT8920_ERR_NOT_READY   -4
#define BOARD_LT8920_ERR_EMPTY       -5
#define BOARD_LT8920_ERR_OVERFLOW    -6
#define BOARD_LT8920_ERR_CRC         -7

#define BOARD_LT8920_ANT1            0U
#define BOARD_LT8920_ANT2            1U

typedef struct
{
    u16 reg7;
    u16 reg8;
    u16 reg36;
    u16 reg37;
    u16 reg38;
    u16 reg39;
    u16 reg48;
    u16 reg52;
    u8 rssi;
    u8 rx_en;
    u8 tx_en;
    u8 ant_sel;
    u8 frontend_state;
    u8 radio_channel;
} board_lt8920_debug_t;

/** @brief 初始化板级 LT8920 与 KCT8206，并执行最小寄存器校验。 */
int8 board_lt8920_init(void);

/** @brief 返回板级 LT8920 是否已经完成 bring-up。 */
u8 board_lt8920_is_ready(void);

/** @brief 返回最近一次初始化或运行错误码。 */
int8 board_lt8920_get_last_error(void);

/** @brief 读取最近一次 LT8920 默认寄存器表校验失败信息。 */
void board_lt8920_get_verify_failure(u8 *reg, u16 *expected, u16 *actual);

/**
 * @brief 让 LT8920 和 KCT8206 前端进入 idle。
 * @return BOARD_LT8920_OK 成功；未初始化或底层失败时返回对应错误码。
 */
int8 board_lt8920_enter_idle(void);

/**
 * @brief 在当前信道打开 LT8920 接收模式。
 * @return BOARD_LT8920_OK 成功；未初始化或底层失败时返回对应错误码。
 */
int8 board_lt8920_open_rx(void);

/**
 * @brief 切换到指定信道并打开接收模式。
 * @param channel 7 位 LT8920 信道号。
 * @return BOARD_LT8920_OK 成功。
 */
int8 board_lt8920_open_rx_on_channel(u8 channel);

/**
 * @brief 在当前信道发送一帧 payload，并等待发送完成。
 * @param buf 待发送 payload，不能为 NULL。
 * @param len payload 长度，范围由 LT8920 芯片层校验。
 * @return BOARD_LT8920_OK 发送完成；失败时返回参数、超时或底层错误码。
 */
int8 board_lt8920_send_packet(const u8 *buf, u8 len);

/**
 * @brief 切换到指定信道发送一帧 payload，发送后保持 idle。
 * @param channel 7 位 LT8920 信道号。
 * @param buf 待发送 payload，不能为 NULL。
 * @param len payload 长度。
 * @return BOARD_LT8920_OK 发送完成。
 */
int8 board_lt8920_send_packet_on_channel(u8 channel, const u8 *buf, u8 len);

/**
 * @brief 从 LT8920 RX FIFO 读取一帧 payload。
 * @param buf 输出缓冲区，不能为 NULL。
 * @param buf_len 输出缓冲区容量。
 * @param out_len 实际读取长度，不能为 NULL。
 * @return BOARD_LT8920_OK 读取成功；BOARD_LT8920_ERR_EMPTY 表示无完整包。
 */
int8 board_lt8920_read_packet(u8 *buf, u8 buf_len, u8 *out_len);

/**
 * @brief 读取 LT8920 状态寄存器。
 * @param status 输出状态寄存器值，不能为 NULL。
 * @return BOARD_LT8920_OK 读取成功。
 */
int8 board_lt8920_read_status(u16 *status);

/**
 * @brief 读取 LT8920 原始 RSSI 字段。
 * @param rssi 输出 RSSI 原始值，不能为 NULL。
 * @return BOARD_LT8920_OK 读取成功。
 */
int8 board_lt8920_read_raw_rssi(u8 *rssi);

/**
 * @brief 设置 LT8920 当前信道。
 * @param channel 7 位信道号。
 * @return BOARD_LT8920_OK 设置成功。
 */
int8 board_lt8920_set_channel(u8 channel);

/**
 * @brief 设置 32 位同步字。
 * @param sync_word 同步字，按 LT8920 芯片层规则拆分到寄存器 36/39。
 * @return BOARD_LT8920_OK 设置成功。
 */
int8 board_lt8920_set_sync_word(u32 sync_word);

/**
 * @brief 直接设置 LT8920 同步字寄存器。
 * @param reg36 寄存器 36 值。
 * @param reg39 寄存器 39 值。
 * @return BOARD_LT8920_OK 设置成功。
 */
int8 board_lt8920_set_sync_regs(u16 reg36, u16 reg39);

/**
 * @brief 选择 KCT8206 天线通道。
 * @param antenna BOARD_LT8920_ANT1 或 BOARD_LT8920_ANT2。
 * @return BOARD_LT8920_OK 设置成功；非法值按芯片前端层返回参数错误。
 */
int8 board_lt8920_set_antenna(u8 antenna);

/**
 * @brief 读取 LT8920/KCT8206 板级调试快照。
 * @param debug 输出调试结构体，不能为 NULL。
 * @return BOARD_LT8920_OK 读取完成。
 */
int8 board_lt8920_get_debug(board_lt8920_debug_t *debug);

/**
 * @brief 获取当前 LT8920 本地信道缓存。
 * @return 7 位信道号；未初始化时仍返回芯片层缓存值。
 */
u8 board_lt8920_get_channel(void);

#endif
