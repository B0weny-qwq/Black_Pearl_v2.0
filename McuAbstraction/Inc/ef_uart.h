/**
 * @file ef_uart.h
 * @brief MCU UART 统一封装接口。
 *
 * 本文件属于 McuAbstraction 层，对上隐藏 STC32G_UART/STC32G_NVIC 的具体
 * 初始化结构体、中断使能函数和发送函数命名。BoardDevices 层通过该接口选择
 * UART 端口、波特率和接收开关；App 层不应直接包含本头文件。
 */

#ifndef __EF_UART_H__
#define __EF_UART_H__

#include "type_def.h"

#define EF_UART_PORT_1  1U
#define EF_UART_PORT_2  2U
#define EF_UART_PORT_3  3U
#define EF_UART_PORT_4  4U

typedef struct
{
    u8 port;       /**< UART 端口号，取值为 EF_UART_PORT_x。 */
    u32 baudrate;  /**< 波特率，例如 115200UL。 */
    u8 rx_enable;  /**< 接收使能，取值沿用 ENABLE/DISABLE。 */
} ef_uart_config_t;

typedef struct
{
    const u8 *rx_buffer;   /**< Driver 层环形接收缓冲首地址。 */
    u16 rx_buffer_size;    /**< 接收缓冲总长度。 */
    u8 write_index;        /**< ISR 当前写指针，对应 Driver 层 RX_Cnt。 */
} ef_uart_rx_view_t;

/**
 * @brief 初始化指定 UART 端口。
 *
 * 内部会配置 STC 官方 UART 驱动，并开启对应 UART 中断。该函数会占用对应
 * UART 的波特率发生器资源；具体引脚复用仍由 BoardDevices 或 Platform 层决定。
 *
 * @param config UART 初始化参数，不能为 NULL。
 * @return SUCCESS 初始化成功。
 * @return FAIL 参数非法或 STC 官方驱动初始化失败。
 */
u8 ef_uart_init(const ef_uart_config_t *config);

/**
 * @brief 阻塞发送 1 字节数据。
 *
 * 当前底层沿用 STC 官方 `TXx_write2buff()` 行为；当官方 UART 配置为阻塞发送时，
 * 本函数会等待该字节发送完成。
 *
 * @param port UART 端口号，取值为 EF_UART_PORT_x。
 * @param data 待发送字节。
 */
void ef_uart_write_byte(u8 port, u8 data);

/**
 * @brief 发送以 `\0` 结束的字符串。
 *
 * @param port UART 端口号，取值为 EF_UART_PORT_x。
 * @param data 待发送字符串；为 NULL 时直接返回。
 */
void ef_uart_write(u8 port, const u8 *data);

/**
 * @brief 获取 UART 接收缓冲的只读视图。
 *
 * 该接口只暴露 Driver 层已经维护好的接收缓冲与写指针，便于 BoardDevices 做
 * 增量消费；不会修改底层缓冲状态，也不会重置 RX_Cnt。
 *
 * @param port UART 端口号，取值为 EF_UART_PORT_x。
 * @param view 输出视图，不能为 NULL。
 * @return SUCCESS 获取成功。
 * @return FAIL 端口号非法或参数为空。
 */
u8 ef_uart_get_rx_view(u8 port, ef_uart_rx_view_t *view);

#endif
