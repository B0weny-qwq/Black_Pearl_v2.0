#include "ef_uart.h"
#include "STC32G_NVIC.h"
#include "STC32G_UART.h"

static u8 ef_uart_to_stc_port(u8 port)
{
    if (port == EF_UART_PORT_1) {
        return UART1;
    }
    if (port == EF_UART_PORT_2) {
        return UART2;
    }
    if (port == EF_UART_PORT_3) {
        return UART3;
    }
    if (port == EF_UART_PORT_4) {
        return UART4;
    }

    return 0U;
}

u8 ef_uart_init(const ef_uart_config_t *config)
{
    COMx_InitDefine init;
    u8 stc_port;
    u8 result;

    if (config == 0) {
        return FAIL;
    }

    stc_port = ef_uart_to_stc_port(config->port);
    if (stc_port == 0U) {
        return FAIL;
    }

    init.UART_Mode = UART_8bit_BRTx;
    init.UART_BaudRate = config->baudrate;
    init.UART_RxEnable = config->rx_enable;
    init.BaudRateDouble = DISABLE;
    init.Morecommunicate = DISABLE;

    if (config->port == EF_UART_PORT_2) {
        init.UART_BRT_Use = BRT_Timer2;
    } else if (config->port == EF_UART_PORT_3) {
        init.UART_BRT_Use = BRT_Timer3;
    } else if (config->port == EF_UART_PORT_4) {
        init.UART_BRT_Use = BRT_Timer4;
    } else {
        init.UART_BRT_Use = BRT_Timer1;
    }

    result = UART_Configuration(stc_port, &init);
    if (result != SUCCESS) {
        return result;
    }

    if (config->port == EF_UART_PORT_1) {
        return NVIC_UART1_Init(ENABLE, Priority_1);
    }
    if (config->port == EF_UART_PORT_2) {
        return NVIC_UART2_Init(ENABLE, Priority_1);
    }
    if (config->port == EF_UART_PORT_3) {
        return NVIC_UART3_Init(ENABLE, Priority_1);
    }
    if (config->port == EF_UART_PORT_4) {
        return NVIC_UART4_Init(ENABLE, Priority_1);
    }

    return FAIL;
}

void ef_uart_write_byte(u8 port, u8 data)
{
    if (port == EF_UART_PORT_1) {
        TX1_write2buff(data);
    } else if (port == EF_UART_PORT_2) {
        TX2_write2buff(data);
    } else if (port == EF_UART_PORT_3) {
        TX3_write2buff(data);
    } else if (port == EF_UART_PORT_4) {
        TX4_write2buff(data);
    }
}

void ef_uart_write(u8 port, const u8 *data)
{
    if (data == 0) {
        return;
    }

    while (*data != '\0') {
        ef_uart_write_byte(port, *data);
        data++;
    }
}

u8 ef_uart_get_rx_view(u8 port, ef_uart_rx_view_t *view)
{
    if (view == 0) {
        return FAIL;
    }

    view->rx_buffer_size = 0U;
    view->write_index = 0U;

    if (port == EF_UART_PORT_1) {
        view->rx_buffer_size = COM_RX1_Lenth;
        view->write_index = COM1.RX_Cnt;
        return SUCCESS;
    }
    if (port == EF_UART_PORT_2) {
        view->rx_buffer_size = COM_RX2_Lenth;
        view->write_index = COM2.RX_Cnt;
        return SUCCESS;
    }
    if (port == EF_UART_PORT_3) {
        view->rx_buffer_size = COM_RX3_Lenth;
        view->write_index = COM3.RX_Cnt;
        return SUCCESS;
    }
    if (port == EF_UART_PORT_4) {
        view->rx_buffer_size = COM_RX4_Lenth;
        view->write_index = COM4.RX_Cnt;
        return SUCCESS;
    }

    return FAIL;
}

u8 ef_uart_read_rx_byte(u8 port, u8 index, u8 *data)
{
    if (data == 0) {
        return FAIL;
    }

    if (port == EF_UART_PORT_1) {
        if (index >= COM_RX1_Lenth) {
            return FAIL;
        }
        *data = RX1_Buffer[index];
        return SUCCESS;
    }
    if (port == EF_UART_PORT_2) {
        if (index >= COM_RX2_Lenth) {
            return FAIL;
        }
        *data = RX2_Buffer[index];
        return SUCCESS;
    }
    if (port == EF_UART_PORT_3) {
        if (index >= COM_RX3_Lenth) {
            return FAIL;
        }
        *data = RX3_Buffer[index];
        return SUCCESS;
    }
    if (port == EF_UART_PORT_4) {
        if (index >= COM_RX4_Lenth) {
            return FAIL;
        }
        *data = RX4_Buffer[index];
        return SUCCESS;
    }

    return FAIL;
}
