/**
 * @file board_wireless.h
 * @brief 板级 LT8920 无线链路状态机接口。
 *
 * 本接口属于 BoardDevices 层，负责管理 LT8920 射频 payload 收发、天线选择、
 * RX 软件队列和链路诊断。App 层只能通过本文件访问无线链路，不关心 SPI、
 * GPIO、复位脚、前端 RXEN/TXEN 或 LT8920 寄存器细节。
 */

#ifndef __BOARD_WIRELESS_H__
#define __BOARD_WIRELESS_H__

#include "type_def.h"

#define BOARD_WIRELESS_OK               0
#define BOARD_WIRELESS_ERR_PARAM       -1
#define BOARD_WIRELESS_ERR_STATE       -2
#define BOARD_WIRELESS_ERR_IO          -3
#define BOARD_WIRELESS_ERR_TIMEOUT     -4
#define BOARD_WIRELESS_ERR_EMPTY       -5
#define BOARD_WIRELESS_ERR_OVERFLOW    -6
#define BOARD_WIRELESS_ERR_VERIFY      -7

#define BOARD_WIRELESS_ANT1             0U
#define BOARD_WIRELESS_ANT2             1U

#define BOARD_WIRELESS_MAX_PAYLOAD_LEN  60U
#define BOARD_WIRELESS_RX_QUEUE_DEPTH   4U

typedef enum
{
    BOARD_WIRELESS_MODE_IDLE = 0,
    BOARD_WIRELESS_MODE_RX,
    BOARD_WIRELESS_MODE_TX
} board_wireless_mode_t;

typedef struct
{
    u8 initialized;
    u8 ready;
    board_wireless_mode_t mode;
    u8 antenna;
    u8 scan_has_signal;
    int8 last_error;
    u16 tx_ok_count;
    u16 rx_ok_count;
    u16 crc_error_count;
    u16 queue_overflow_count;
    u16 rx_drop_count;
    u16 antenna_rssi_ant1;
    u16 antenna_rssi_ant2;
} board_wireless_state_t;

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
    u8 mode;
    u8 rx_mode_bit;
    u8 channel;
} board_wireless_rx_debug_t;

/**
 * @brief 初始化板级无线链路并进入 RX。
 *
 * 函数会初始化 LT8920/KCT8206 板级绑定、执行默认寄存器加载与校验、
 * 扫描天线并打开默认接收模式。该函数可能阻塞等待芯片复位和短延时。
 *
 * @return BOARD_WIRELESS_OK 初始化成功。
 * @return BOARD_WIRELESS_ERR_IO 底层 SPI/GPIO/芯片驱动失败。
 * @return BOARD_WIRELESS_ERR_VERIFY 默认寄存器回读校验失败。
 */
int8 board_wireless_init(void);

/**
 * @brief 关闭无线链路到空闲状态并清空软件接收队列。
 * @return BOARD_WIRELESS_OK 关闭成功或原本未初始化。
 */
int8 board_wireless_deinit(void);

/**
 * @brief 轮询 LT8920 接收状态并把 RF payload 推入软件队列。
 * @return BOARD_WIRELESS_OK 本次轮询完成。
 */
int8 board_wireless_poll(void);

/**
 * @brief 发送一帧 RF payload，发送完成后恢复 RX。
 * @param buf 待发送 payload，不能为 NULL。
 * @param len payload 长度，范围 1..BOARD_WIRELESS_MAX_PAYLOAD_LEN。
 * @return BOARD_WIRELESS_OK 发送已启动。
 */
int8 board_wireless_send(const u8 *buf, u8 len);

/**
 * @brief 在指定 LT8920 信道发送一帧 RF payload，发送后保持 idle。
 * @param channel 7 位 LT8920 信道号。
 * @param buf 待发送 payload，不能为 NULL。
 * @param len payload 长度。
 * @return BOARD_WIRELESS_OK 发送已启动。
 */
int8 board_wireless_send_on_channel(u8 channel, const u8 *buf, u8 len);

/**
 * @brief 从软件接收队列读取一帧 RF payload。
 * @param buf 输出缓冲区，不能为 NULL。
 * @param buf_len 输出缓冲区容量。
 * @param out_len 实际读取长度，不能为 NULL。
 * @return BOARD_WIRELESS_OK 读取成功。
 * @return BOARD_WIRELESS_ERR_EMPTY 当前无 payload。
 */
int8 board_wireless_receive(u8 *buf, u8 buf_len, u8 *out_len);

int8 board_wireless_set_channel(u8 channel);
int8 board_wireless_set_sync_regs(u16 reg36, u16 reg39);
int8 board_wireless_set_sync_regs_idle(u16 reg36, u16 reg39);
int8 board_wireless_get_state(board_wireless_state_t *state);
int8 board_wireless_get_rx_debug(board_wireless_rx_debug_t *debug);
int8 board_wireless_rescan_antenna(void);
int8 board_wireless_search_signal_poll(void);

#endif
