#include "board_console.h"
#include "ef_board_resources.h"
#include "ef_uart.h"
#include "STC32G_GPIO.h"
#include "STC32G_Switch.h"

u8 board_console_init(void)
{
    ef_uart_config_t config;

    P3_MODE_IO_PU(EF_BOARD_CONSOLE_RX_PIN_MASK | EF_BOARD_CONSOLE_TX_PIN_MASK);
    UART1_SW(EF_BOARD_CONSOLE_UART_MUX);

    config.port = EF_BOARD_CONSOLE_UART_PORT;
    config.baudrate = EF_BOARD_CONSOLE_UART_BAUDRATE;
    config.rx_enable = ENABLE;

    if (ef_uart_init(&config) == SUCCESS) {
        return BOARD_CONSOLE_OK;
    }

    return BOARD_CONSOLE_ERR;
}

void board_console_write(const u8 *text)
{
    ef_uart_write(EF_BOARD_CONSOLE_UART_PORT, text);
}

void board_console_write_byte(u8 byte)
{
    ef_uart_write_byte(EF_BOARD_CONSOLE_UART_PORT, byte);
}
