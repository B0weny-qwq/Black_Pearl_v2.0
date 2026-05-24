/**
 * @file board_console.h
 * @brief Black Pearl 板级控制台接口。
 *
 * 本文件属于 BoardDevices 层，用于隐藏日志控制台的 UART 实例、引脚复用、
 * 波特率和 STC 驱动细节。Services/logger 通过该接口输出日志；App 层只负责
 * 调用初始化顺序，不直接接触 UART 官方驱动。
 */

#ifndef __BOARD_CONSOLE_H__
#define __BOARD_CONSOLE_H__

#include "type_def.h"

#define BOARD_CONSOLE_OK   0U
#define BOARD_CONSOLE_ERR  1U

/**
 * @brief 初始化板级控制台。
 *
 * 当前硬件绑定为 UART1，P3.1=TXD，P3.0=RXD，115200 8N1。该函数会配置
 * UART1 引脚复用、GPIO 模式和 UART/NVIC 底层驱动。
 *
 * @return BOARD_CONSOLE_OK 初始化成功。
 * @return BOARD_CONSOLE_ERR 底层 UART 初始化失败。
 */
u8 board_console_init(void);

/**
 * @brief 输出字符串到板级控制台。
 *
 * 当前底层为阻塞串口发送；调用者不需要关心 UART 端口和引脚。
 *
 * @param text 以 `\0` 结束的字符串；为 NULL 时直接返回。
 */
void board_console_write(const u8 *text);

/**
 * @brief 输出 1 字节到板级控制台。
 *
 * @param byte 待发送字节。
 */
void board_console_write_byte(u8 byte);

#endif
