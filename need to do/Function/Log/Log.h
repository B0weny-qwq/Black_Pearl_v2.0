/**
 * @file    Log.h
 * @brief   UART1 轻量级日志输出接口。
 * @author  boweny
 * @date    2026-05-05
 * @version v1.0
 *
 * @details
 * 基于 UART1 提供带标签、分级别的格式化日志输出。支持 INFO、WARN、
 * ERROR、DEBUG 四个级别，以及无级别的原始 printf 风格输出。
 *
 * @note    使用前必须先初始化 UART1，再调用 log_init() 使能日志输出。
 * @note    STC32G 工程中避免使用 %f，请将浮点量转换为整数后再输出。
 *
 * @see     Code_boweny/Function/Log/Log.c
 */

#ifndef __LOG_H__
#define __LOG_H__

#include "..\..\..\Driver\inc\STC32G_UART.h"
#include <stdarg.h>

#define LOGI(tag, ...)   log_info(tag, __VA_ARGS__)   /**< 输出 INFO 级别日志。 */
#define LOGW(tag, ...)   log_warn(tag, __VA_ARGS__)   /**< 输出 WARN 级别日志。 */
#define LOGE(tag, ...)   log_error(tag, __VA_ARGS__)  /**< 输出 ERROR 级别日志。 */
#define LOGD(tag, ...)   log_debug(tag, __VA_ARGS__)  /**< 输出 DEBUG 级别日志。 */

/**
 * @brief   初始化日志系统。
 * @return  none
 *
 * @details
 * 置位内部日志就绪标志，使 LOGI/LOGW/LOGE/LOGD 和 log_printf 输出生效。
 */
void log_init(void);

/**
 * @brief      输出带标签的 INFO 级别日志。
 * @param[in]  tag  日志标签字符串，例如 "IMU"。
 * @param[in]  fmt  printf 风格格式化字符串。
 * @param[in]  ...  可变参数。
 * @return     none
 */
void log_info(u8 *tag, u8 *fmt, ...);

/**
 * @brief      输出带标签的 WARN 级别日志。
 * @param[in]  tag  日志标签字符串。
 * @param[in]  fmt  printf 风格格式化字符串。
 * @param[in]  ...  可变参数。
 * @return     none
 */
void log_warn(u8 *tag, u8 *fmt, ...);

/**
 * @brief      输出带标签的 ERROR 级别日志。
 * @param[in]  tag  日志标签字符串。
 * @param[in]  fmt  printf 风格格式化字符串。
 * @param[in]  ...  可变参数。
 * @return     none
 */
void log_error(u8 *tag, u8 *fmt, ...);

/**
 * @brief      输出带标签的 DEBUG 级别日志。
 * @param[in]  tag  日志标签字符串。
 * @param[in]  fmt  printf 风格格式化字符串。
 * @param[in]  ...  可变参数。
 * @return     none
 */
void log_debug(u8 *tag, u8 *fmt, ...);

/**
 * @brief      输出无标签、无级别的原始日志。
 * @param[in]  fmt  printf 风格格式化字符串。
 * @param[in]  ...  可变参数。
 * @return     none
 */
void log_printf(u8 *fmt, ...);

#endif
