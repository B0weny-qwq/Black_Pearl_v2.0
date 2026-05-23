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

/** @brief 初始化板级 LT8920 与 KCT8206，并执行最小寄存器校验。 */
int8 board_lt8920_init(void);

/** @brief 返回板级 LT8920 是否已经完成 bring-up。 */
u8 board_lt8920_is_ready(void);

/** @brief 返回最近一次初始化或运行错误码。 */
int8 board_lt8920_get_last_error(void);

/** @brief 读取最近一次 LT8920 默认寄存器表校验失败信息。 */
void board_lt8920_get_verify_failure(u8 *reg, u16 *expected, u16 *actual);

#endif
