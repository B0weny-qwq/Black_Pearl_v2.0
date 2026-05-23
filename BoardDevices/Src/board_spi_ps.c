#include "board_spi_ps.h"
#include "ef_spi.h"
#include "STC32G_GPIO.h"
#include "STC32G_SPI.h"
#include "STC32G_Switch.h"

int8 board_spi_ps_init(void)
{
    ef_spi_config_t config;

    SPI_SW(SPI_P22_P23_P24_P25);
    P2_MODE_IO_PU(GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5);
    SPI_SS_2 = 1;

    config.enable = EF_SPI_ENABLE;
    config.ss_control = EF_SPI_SS_BY_PIN;
    config.first_bit = EF_SPI_FIRST_MSB;
    config.mode = EF_SPI_MODE_SLAVE;
    config.cpol = EF_SPI_CPOL_LOW;
    config.cpha = EF_SPI_CPHA_2EDGE;
    config.speed = EF_SPI_SPEED_FOSC_4;
    config.irq_enable = EF_SPI_ENABLE;

    if (ef_spi_init(&config) != EF_SPI_OK) {
        return BOARD_SPI_PS_ERR_DRIVER;
    }

    return BOARD_SPI_PS_OK;
}

u8 board_spi_ps_is_idle(void)
{
    return (SPI_SS_2 != 0) ? 1U : 0U;
}

int8 board_spi_ps_send(const u8 *data, u8 len)
{
    u8 i;

    if ((data == 0) || (len == 0U)) {
        return BOARD_SPI_PS_ERR_PARAM;
    }
    if (board_spi_ps_is_idle() == 0U) {
        return BOARD_SPI_PS_ERR_BUSY;
    }

    SPI_SS_2 = 0;
    ef_spi_set_mode(EF_SPI_MODE_MASTER);

    for (i = 0U; i < len; i++) {
        ef_spi_write_byte(data[i]);
    }

    SPI_SS_2 = 1;
    ef_spi_set_mode(EF_SPI_MODE_SLAVE);

    return BOARD_SPI_PS_OK;
}

u8 board_spi_ps_service(void)
{
    return ef_spi_slave_rx_tick();
}

int8 board_spi_ps_read(u8 *buffer, u8 max_len, u8 *out_len)
{
    int8 ret;

    ret = ef_spi_slave_rx_read(buffer, max_len, out_len);
    if (ret == EF_SPI_OK) {
        return BOARD_SPI_PS_OK;
    }
    if (ret == EF_SPI_ERR_OVERFLOW) {
        return BOARD_SPI_PS_ERR_OVERFLOW;
    }

    return BOARD_SPI_PS_ERR_PARAM;
}

void board_spi_ps_reset_rx(void)
{
    ef_spi_slave_rx_reset();
}
