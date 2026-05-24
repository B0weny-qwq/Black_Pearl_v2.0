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

int8 board_lt8920_enter_idle(void);
int8 board_lt8920_open_rx(void);
int8 board_lt8920_open_rx_on_channel(u8 channel);
int8 board_lt8920_send_packet(const u8 *buf, u8 len);
int8 board_lt8920_send_packet_on_channel(u8 channel, const u8 *buf, u8 len);
int8 board_lt8920_read_packet(u8 *buf, u8 buf_len, u8 *out_len);
int8 board_lt8920_read_status(u16 *status);
int8 board_lt8920_read_raw_rssi(u8 *rssi);
int8 board_lt8920_set_channel(u8 channel);
int8 board_lt8920_set_sync_word(u32 sync_word);
int8 board_lt8920_set_sync_regs(u16 reg36, u16 reg39);
int8 board_lt8920_set_antenna(u8 antenna);
int8 board_lt8920_get_debug(board_lt8920_debug_t *debug);
u8 board_lt8920_get_channel(void);

#endif
