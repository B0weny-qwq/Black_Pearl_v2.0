#include "board_console.h"
#include "ef_uart.h"
#include "STC32G_GPIO.h"
#include "STC32G_Switch.h"

#define BOARD_CONSOLE_UART_PORT      EF_UART_PORT_1
#define BOARD_CONSOLE_UART_BAUDRATE  115200UL

u8 board_console_init(void)
{
    ef_uart_config_t config;

    P3_MODE_IO_PU(GPIO_Pin_0 | GPIO_Pin_1);
    UART1_SW(UART1_SW_P30_P31);

    config.port = BOARD_CONSOLE_UART_PORT;
    config.baudrate = BOARD_CONSOLE_UART_BAUDRATE;
    config.rx_enable = ENABLE;

    if (ef_uart_init(&config) == SUCCESS) {
        return BOARD_CONSOLE_OK;
    }

    return BOARD_CONSOLE_ERR;
}

void board_console_write(const u8 *text)
{
    ef_uart_write(BOARD_CONSOLE_UART_PORT, text);
}

void board_console_write_byte(u8 data)
{
    ef_uart_write_byte(BOARD_CONSOLE_UART_PORT, data);
}
