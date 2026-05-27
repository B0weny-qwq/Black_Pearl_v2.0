#include "logger.h"
#include "board_console.h"
#include <stdarg.h>

#define LOG_BUF_SIZE  128U

static u8 log_ready = 0U;
static u8 log_buf[LOG_BUF_SIZE];
static u8 log_pos = 0U;

static void log_reset(void)
{
    log_pos = 0U;
    log_buf[0] = '\0';
}

static void log_putc(u8 ch)
{
    if (log_pos < (LOG_BUF_SIZE - 1U)) {
        log_buf[log_pos] = ch;
        log_pos++;
        log_buf[log_pos] = '\0';
    }
}

static void log_puts(u8 *text)
{
    if (text == 0) {
        text = (u8 *)"(null)";
    }

    while (*text != '\0') {
        log_putc(*text);
        text++;
    }
}

static void log_put_unsigned(u32 value, u8 base, u8 upper, u8 width, u8 pad_zero)
{
    u8 tmp[16];
    u8 count;
    u8 digit;
    u8 pad;

    count = 0U;
    do {
        digit = (u8)(value % base);
        if (digit < 10U) {
            tmp[count] = (u8)('0' + digit);
        } else if (upper != 0U) {
            tmp[count] = (u8)('A' + digit - 10U);
        } else {
            tmp[count] = (u8)('a' + digit - 10U);
        }
        count++;
        value /= base;
    } while ((value != 0UL) && (count < sizeof(tmp)));

    pad = (pad_zero != 0U) ? '0' : ' ';
    while (count < width) {
        log_putc(pad);
        width--;
    }

    while (count > 0U) {
        count--;
        log_putc(tmp[count]);
    }
}

static void log_put_signed(int32 value, u8 width, u8 pad_zero)
{
    u32 abs_value;

    if (value < 0L) {
        log_putc('-');
        abs_value = (u32)(0L - value);
        if (width > 0U) {
            width--;
        }
    } else {
        abs_value = (u32)value;
    }

    log_put_unsigned(abs_value, 10U, 0U, width, pad_zero);
}

static void log_vformat(u8 *fmt, va_list args)
{
    u8 ch;
    u8 width;
    u8 pad_zero;
    u8 is_long;

    if (fmt == 0) {
        return;
    }

    while (*fmt != '\0') {
        ch = *fmt;
        fmt++;

        if (ch != '%') {
            log_putc(ch);
            continue;
        }

        pad_zero = 0U;
        width = 0U;
        is_long = 0U;

        if (*fmt == '0') {
            pad_zero = 1U;
            fmt++;
        }

        while ((*fmt >= '0') && (*fmt <= '9')) {
            width = (u8)((width * 10U) + (u8)(*fmt - '0'));
            fmt++;
        }

        if (*fmt == 'l') {
            is_long = 1U;
            fmt++;
        }

        ch = *fmt;
        if (ch == '\0') {
            break;
        }
        fmt++;

        if (ch == '%') {
            log_putc('%');
        } else if ((ch == 'd') || (ch == 'i')) {
            if (is_long != 0U) {
                log_put_signed(va_arg(args, int32), width, pad_zero);
            } else {
                log_put_signed((int32)va_arg(args, int), width, pad_zero);
            }
        } else if (ch == 'u') {
            if (is_long != 0U) {
                log_put_unsigned(va_arg(args, u32), 10U, 0U, width, pad_zero);
            } else {
                log_put_unsigned((u32)va_arg(args, unsigned int), 10U, 0U, width, pad_zero);
            }
        } else if (ch == 'x') {
            if (is_long != 0U) {
                log_put_unsigned(va_arg(args, u32), 16U, 0U, width, pad_zero);
            } else {
                log_put_unsigned((u32)va_arg(args, unsigned int), 16U, 0U, width, pad_zero);
            }
        } else if (ch == 'X') {
            if (is_long != 0U) {
                log_put_unsigned(va_arg(args, u32), 16U, 1U, width, pad_zero);
            } else {
                log_put_unsigned((u32)va_arg(args, unsigned int), 16U, 1U, width, pad_zero);
            }
        } else if (ch == 'c') {
            log_putc((u8)va_arg(args, int));
        } else if (ch == 's') {
            log_puts(va_arg(args, u8 *));
        } else {
            log_putc('%');
            if (is_long != 0U) {
                log_putc('l');
            }
            log_putc(ch);
        }
    }
}

static void log_append_crlf(void)
{
    if (log_pos > (LOG_BUF_SIZE - 3U)) {
        log_pos = LOG_BUF_SIZE - 3U;
    }

    log_buf[log_pos] = '\r';
    log_pos++;
    log_buf[log_pos] = '\n';
    log_pos++;
    log_buf[log_pos] = '\0';
}

static void log_vprint(u8 *fmt, va_list args)
{
    log_reset();
    log_vformat(fmt, args);
    log_append_crlf();
    board_console_write(log_buf);
}

static void log_vtagged(u8 level, u8 *tag, u8 *fmt, va_list args)
{
    if (tag == 0) {
        tag = (u8 *)"LOG";
    }

    log_reset();
    log_putc('[');
    log_puts(tag);
    log_putc(']');
    log_putc(' ');
    log_putc(level);
    log_putc(':');
    log_putc(' ');
    log_vformat(fmt, args);
    log_append_crlf();
    board_console_write(log_buf);
}

void log_init(void)
{
    log_ready = 1U;
    board_console_write((const u8 *)"\r\n");
    log_printf((u8 *)"[SYS] I: Black Pearl v2.0");
    log_printf((u8 *)"[SYS] I: UART1 P3.1/P3.0 115200");
    log_printf((u8 *)"[SYS] I: Build %s %s", __DATE__, __TIME__);
    board_console_write((const u8 *)"\r\n");
}

void log_info(u8 *tag, u8 *fmt, ...)
{
    va_list args;

    if (!log_ready) {
        return;
    }

    va_start(args, fmt);
    log_vtagged('I', tag, fmt, args);
    va_end(args);
}

void log_warn(u8 *tag, u8 *fmt, ...)
{
    va_list args;

    if (!log_ready) {
        return;
    }

    va_start(args, fmt);
    log_vtagged('W', tag, fmt, args);
    va_end(args);
}

void log_error(u8 *tag, u8 *fmt, ...)
{
    va_list args;

    if (!log_ready) {
        return;
    }

    va_start(args, fmt);
    log_vtagged('E', tag, fmt, args);
    va_end(args);
}

void log_debug(u8 *tag, u8 *fmt, ...)
{
    va_list args;

    if (!log_ready) {
        return;
    }

    va_start(args, fmt);
    log_vtagged('D', tag, fmt, args);
    va_end(args);
}

void log_printf(u8 *fmt, ...)
{
    va_list args;

    if (!log_ready) {
        return;
    }

    va_start(args, fmt);
    log_vprint(fmt, args);
    va_end(args);
}
