#include "board_spi_ps.h"
#include "ef_board_resources.h"
#include "ef_spi.h"
#include "STC32G_GPIO.h"
#include "STC32G_SPI.h"
#include "STC32G_Switch.h"

int8 board_spi_ps_init(void)
{
#if EF_BOARD_SPI_PS_ENABLED
#if EF_BOARD_SPI_PS_SHARES_LT8920_SPI
    return BOARD_SPI_PS_ERR_RESOURCE;
#else
    ef_spi_config_t config;

    SPI_SW(EF_BOARD_SPI_PS_MUX);
    P2_MODE_IO_PU(EF_BOARD_SPI_PS_SS_PIN_MASK |
                  EF_BOARD_SPI_PS_MOSI_PIN_MASK |
                  EF_BOARD_SPI_PS_MISO_PIN_MASK |
                  EF_BOARD_SPI_PS_SCLK_PIN_MASK);
    EF_BOARD_SPI_PS_SS_BIT = 1;

    config.enable = EF_SPI_ENABLE;
    config.ss_control = EF_SPI_SS_BY_PIN;
    config.first_bit = EF_SPI_FIRST_MSB;
    config.mode = EF_SPI_MODE_SLAVE;
    config.cpol = EF_SPI_CPOL_LOW;
    config.cpha = EF_SPI_CPHA_2EDGE;
    config.speed = EF_BOARD_SPI_PS_SPEED;
    config.irq_enable = EF_SPI_ENABLE;

    if (ef_spi_init(&config) != EF_SPI_OK) {
        return BOARD_SPI_PS_ERR_DRIVER;
    }

    return BOARD_SPI_PS_OK;
#endif
#else
    return BOARD_SPI_PS_ERR_DRIVER;
#endif
}

u8 board_spi_ps_is_idle(void)
{
#if EF_BOARD_SPI_PS_ENABLED
    return (EF_BOARD_SPI_PS_SS_BIT != 0) ? 1U : 0U;
#else
    return 0U;
#endif
}

int8 board_spi_ps_send(const u8 *buffer, u8 len)
{
#if EF_BOARD_SPI_PS_ENABLED
    u8 i;

    if ((buffer == 0) || (len == 0U)) {
        return BOARD_SPI_PS_ERR_PARAM;
    }
    if (board_spi_ps_is_idle() == 0U) {
        return BOARD_SPI_PS_ERR_BUSY;
    }

    EF_BOARD_SPI_PS_SS_BIT = 0;
    ef_spi_set_mode(EF_SPI_MODE_MASTER);

    for (i = 0U; i < len; i++) {
        ef_spi_write_byte(buffer[i]);
    }

    EF_BOARD_SPI_PS_SS_BIT = 1;
    ef_spi_set_mode(EF_SPI_MODE_SLAVE);

    return BOARD_SPI_PS_OK;
#else
    if ((buffer == 0) || (len == 0U)) {
        return BOARD_SPI_PS_ERR_PARAM;
    }
    return BOARD_SPI_PS_ERR_DRIVER;
#endif
}

u8 board_spi_ps_service(void)
{
#if EF_BOARD_SPI_PS_ENABLED
    return ef_spi_slave_rx_tick();
#else
    return 0U;
#endif
}

int8 board_spi_ps_read(u8 *buffer, u8 max_len, u8 *out_len)
{
#if EF_BOARD_SPI_PS_ENABLED
    int8 ret;

    ret = ef_spi_slave_rx_read(buffer, max_len, out_len);
    if (ret == EF_SPI_OK) {
        return BOARD_SPI_PS_OK;
    }
    if (ret == EF_SPI_ERR_OVERFLOW) {
        return BOARD_SPI_PS_ERR_OVERFLOW;
    }

    return BOARD_SPI_PS_ERR_PARAM;
#else
    if ((buffer == 0) || (out_len == 0) || (max_len == 0U)) {
        return BOARD_SPI_PS_ERR_PARAM;
    }
    *out_len = 0U;
    return BOARD_SPI_PS_ERR_DRIVER;
#endif
}

void board_spi_ps_reset_rx(void)
{
#if EF_BOARD_SPI_PS_ENABLED
    ef_spi_slave_rx_reset();
#endif
}
