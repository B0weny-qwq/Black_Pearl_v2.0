/**
 * @file logger.h
 * @brief 轻量级串口日志服务接口。
 *
 * 本文件属于 Services 层，提供带标签、分级别的日志 API。日志服务只依赖
 * BoardDevices/board_console 输出，不直接包含 STC 官方驱动头文件，也不直接
 * 操作寄存器或引脚。
 */

#ifndef __LOGGER_H__
#define __LOGGER_H__

#include "type_def.h"

#define LOGI(tag, ...)   log_info((u8 *)(tag), __VA_ARGS__)
#define LOGW(tag, ...)   log_warn((u8 *)(tag), __VA_ARGS__)
#define LOGE(tag, ...)   log_error((u8 *)(tag), __VA_ARGS__)
#define LOGD(tag, ...)   log_debug((u8 *)(tag), __VA_ARGS__)

/**
 * @brief 打开日志输出。
 *
 * 必须在 `board_console_init()` 成功之后调用。该函数只置位日志就绪状态并输出
 * 启动横幅，不负责初始化 UART。
 */
void log_init(void);

/**
 * @brief 输出 INFO 级别日志。
 *
 * 输出格式为 `[tag] I: message\r\n`。日志未初始化时直接丢弃。
 *
 * @param tag 日志标签字符串；为 NULL 时使用 `LOG`。
 * @param fmt 格式化字符串；支持 `%d/%i/%u/%ld/%lu/%x/%X/%s/%c/%%` 和常用宽度。
 */
void log_info(u8 *tag, u8 *fmt, ...);

/**
 * @brief 输出 WARN 级别日志。
 *
 * 输出格式为 `[tag] W: message\r\n`。日志未初始化时直接丢弃。
 *
 * @param tag 日志标签字符串；为 NULL 时使用 `LOG`。
 * @param fmt 格式化字符串；不支持 `%f`。
 */
void log_warn(u8 *tag, u8 *fmt, ...);

/**
 * @brief 输出 ERROR 级别日志。
 *
 * 输出格式为 `[tag] E: message\r\n`。日志未初始化时直接丢弃。
 *
 * @param tag 日志标签字符串；为 NULL 时使用 `LOG`。
 * @param fmt 格式化字符串；不支持 `%f`。
 */
void log_error(u8 *tag, u8 *fmt, ...);

/**
 * @brief 输出 DEBUG 级别日志。
 *
 * 输出格式为 `[tag] D: message\r\n`。日志未初始化时直接丢弃。
 *
 * @param tag 日志标签字符串；为 NULL 时使用 `LOG`。
 * @param fmt 格式化字符串；不支持 `%f`。
 */
void log_debug(u8 *tag, u8 *fmt, ...);

/**
 * @brief 输出原始日志行。
 *
 * 输出格式为 `message\r\n`。单条日志使用 128 字节内部缓冲，超长内容会被截断。
 *
 * @param fmt 格式化字符串；不支持 `%f`。
 */
void log_printf(u8 *fmt, ...);

#endif
