/**
 * @file    Log.c
 * @brief   轻量级 UART1 日志输出模块实现
 * @author  boweny
 * @date    2026-04-22
 * @version v1.0
 *
 * @details
 * - 基于 UART1 的轻量级日志输出，提供带标签、分级别的日志功能
 * - 内部使用 vsprintf() 格式化，最大支持 127 字符
 * - 输出格式: [tag] X: message\r\n  或  message\r\n
 * - log_init() 上电时自动打印启动横幅 (MCU / 作者 / 编译时间)
 *
 * @note    禁止使用浮点数 (%f) 格式化
 * @note    调用前必须先初始化 UART1，再调用 log_init()
 *
 * @see     Code_boweny/Function/Log/Log.h
 */

#include "..\..\..\User\config.h"
#include "Log.h"

#define LOG_BUF_SIZE  128

#ifndef AHRS_TEST_ONLY
#define AHRS_TEST_ONLY  0
#endif

/*---------------------------------- 本地变量 ----------------------------------*/

static u8  log_ready = 0;          /* 日志就绪标志: 0=禁用, 1=启用 */
static u8  log_buf[LOG_BUF_SIZE];  /* 格式化缓冲区 */

static bit log_tag_is(u8 *tag, u8 *name)
{
    u8 i;

    if ((tag == 0) || (name == 0)) {
        return 0;
    }

    for (i = 0; ; i++) {
        if (tag[i] != name[i]) {
            return 0;
        }
        if (tag[i] == '\0') {
            return 1;
        }
    }
}

static bit log_allow_in_ahrs_test(u8 level, u8 *tag)
{
    if (level == 'E') {
        return 1;
    }

    if (log_tag_is(tag, "AHRS") ||
        log_tag_is(tag, "HDG")) {
        return 1;
    }

#if QMI8658_DIAG_ENABLE
    if (log_tag_is(tag, "SYS") ||
        log_tag_is(tag, "IMU") ||
        log_tag_is(tag, "MAG")) {
        return 1;
    }
#endif

    return 0;
}

/*---------------------------------- 内部函数 ----------------------------------*/

/**
 * @brief      原始日志输出 (内部函数)
 * @param[in]  fmt   格式化字符串
 * @param[in]  args  可变参数列表
 * @return     none
 */
static void log_vprint(u8 *fmt, va_list args)
{
    u8  len;

    len = (u8)vsprintf((char *)log_buf, (const char *)fmt, args);

    /* 追加 \r\n */
    if (len < (LOG_BUF_SIZE - 2))
    {
        log_buf[len]     = '\r';
        log_buf[len + 1] = '\n';
        log_buf[len + 2] = '\0';
    }
    else
    {
        log_buf[LOG_BUF_SIZE - 3] = '\r';
        log_buf[LOG_BUF_SIZE - 2] = '\n';
        log_buf[LOG_BUF_SIZE - 1] = '\0';
    }

    PrintString1(log_buf);
}

/**
 * @brief      带标签日志输出 (内部函数)
 * @param[in]  level 日志级别字符 (I/W/E/D)
 * @param[in]  tag   日志标签
 * @param[in]  fmt   格式化字符串
 * @param[in]  args  可变参数列表
 * @return     none
 */
static void log_vtagged(u8 level, u8 *tag, u8 *fmt, va_list args)
{
    u8  i;
    u8  prefix_len;

#if AHRS_TEST_ONLY
    if (!log_allow_in_ahrs_test(level, tag)) {
        return;
    }
#endif

    /* 构建 [tag] X: 前缀 */
    log_buf[0] = '[';
    for (i = 0; tag[i] != '\0'; i++)
    {
        log_buf[i + 1] = tag[i];
    }
    log_buf[i + 1] = ']';
    log_buf[i + 2] = ' ';
    log_buf[i + 3] = level;
    log_buf[i + 4] = ':';
    log_buf[i + 5] = ' ';
    prefix_len = i + 6;

    /* 格式化消息到前缀之后 */
    vsprintf((char *)(log_buf + prefix_len), (const char *)fmt, args);

    /* 追加 \r\n 并确保不越界 */
    for (i = prefix_len; i < LOG_BUF_SIZE; i++)
    {
        if (log_buf[i] == '\0')
        {
            if (i < (LOG_BUF_SIZE - 2))
            {
                log_buf[i]     = '\r';
                log_buf[i + 1] = '\n';
                log_buf[i + 2] = '\0';
            }
            else
            {
                log_buf[LOG_BUF_SIZE - 3] = '\r';
                log_buf[LOG_BUF_SIZE - 2] = '\n';
                log_buf[LOG_BUF_SIZE - 1] = '\0';
            }
            break;
        }
    }

    PrintString1(log_buf);
}

/*---------------------------------- 初始化 ----------------------------------*/

/**
 * @brief   初始化日志系统
 * @return  none
 *
 * @details 将 log_ready 标志置 1，并打印启动横幅 (MCU版本/作者/编译时间)
 *          用于上电时验证日志系统是否正常工作
 *
 * @note    必须在 UART_config() 之后调用
 */
void log_init(void)
{
    log_ready = 1;

#if !AHRS_TEST_ONLY || QMI8658_DIAG_ENABLE
    PrintString1("\r\n");
    log_printf("[SYS] I: ============== Black Pearl v1.1 ==============");
    log_printf("[SYS] I:   MCU : STC32G  Fosc=%luHz", (u32)MAIN_Fosc);
    /* Keep the banner tied to the latest firmware build for field diagnosis. */
    log_printf("[SYS] I:   Author : boweny  Build : %s %s", __DATE__, __TIME__);
    log_printf("[SYS] I: LOG Ready. Use LOGI/LOGW/LOGE/LOGD/log_printf");
    PrintString1("\r\n");
#endif
}

/*---------------------------------- 公共函数 ----------------------------------*/

/**
 * @brief      带标签的 INFO 级别日志
 * @param[in]  tag   日志标签
 * @param[in]  fmt   格式化字符串
 * @param[in]  ...   可变参数
 * @return     none
 */
void log_info(u8 *tag, u8 *fmt, ...)
{
    va_list args;

    if (!log_ready) return;

    va_start(args, fmt);
    log_vtagged('I', tag, fmt, args);
    va_end(args);
}

/**
 * @brief      带标签的 WARN 级别日志
 * @param[in]  tag   日志标签
 * @param[in]  fmt   格式化字符串
 * @param[in]  ...   可变参数
 * @return     none
 */
void log_warn(u8 *tag, u8 *fmt, ...)
{
    va_list args;

    if (!log_ready) return;

    va_start(args, fmt);
    log_vtagged('W', tag, fmt, args);
    va_end(args);
}

/**
 * @brief      带标签的 ERROR 级别日志
 * @param[in]  tag   日志标签
 * @param[in]  fmt   格式化字符串
 * @param[in]  ...   可变参数
 * @return     none
 */
void log_error(u8 *tag, u8 *fmt, ...)
{
    va_list args;

    if (!log_ready) return;

    va_start(args, fmt);
    log_vtagged('E', tag, fmt, args);
    va_end(args);
}

/**
 * @brief      带标签的 DEBUG 级别日志
 * @param[in]  tag   日志标签
 * @param[in]  fmt   格式化字符串
 * @param[in]  ...   可变参数
 * @return     none
 */
void log_debug(u8 *tag, u8 *fmt, ...)
{
    va_list args;

    if (!log_ready) return;

    va_start(args, fmt);
    log_vtagged('D', tag, fmt, args);
    va_end(args);
}

/**
 * @brief      原始日志输出 (无标签、无级别)
 * @param[in]  fmt   格式化字符串
 * @param[in]  ...   可变参数
 * @return     none
 */
void log_printf(u8 *fmt, ...)
{
    va_list args;

    if (!log_ready) return;
#if AHRS_TEST_ONLY
#if !QMI8658_DIAG_ENABLE
    return;
#endif
#endif

    va_start(args, fmt);
    log_vprint(fmt, args);
    va_end(args);
}
