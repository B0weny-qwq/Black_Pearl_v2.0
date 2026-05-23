#include "ef_spi.h"
#include "STC32G_NVIC.h"
#include "STC32G_SPI.h"

static u8 ef_spi_to_stc_enable(u8 enable)
{
    return (enable != 0U) ? ENABLE : DISABLE;
}

int8 ef_spi_init(const ef_spi_config_t *config)
{
    SPI_InitTypeDef init;

    if (config == 0) {
        return EF_SPI_ERR_PARAM;
    }

    init.SPI_Enable = ef_spi_to_stc_enable(config->enable);
    init.SPI_SSIG = (config->ss_control == EF_SPI_SS_IGNORE) ? ENABLE : DISABLE;
    init.SPI_FirstBit = (config->first_bit == EF_SPI_FIRST_LSB) ? SPI_LSB : SPI_MSB;
    init.SPI_Mode = (config->mode == EF_SPI_MODE_MASTER) ? SPI_Mode_Master : SPI_Mode_Slave;
    init.SPI_CPOL = (config->cpol == EF_SPI_CPOL_HIGH) ? SPI_CPOL_High : SPI_CPOL_Low;
    init.SPI_CPHA = (config->cpha == EF_SPI_CPHA_1EDGE) ? SPI_CPHA_1Edge : SPI_CPHA_2Edge;
    init.SPI_Speed = config->speed;

    SPI_Init(&init);
    ef_spi_slave_rx_reset();
    NVIC_SPI_Init(ef_spi_to_stc_enable(config->irq_enable), Priority_3);

    return EF_SPI_OK;
}

void ef_spi_set_mode(u8 mode)
{
    if (mode == EF_SPI_MODE_MASTER) {
        SPI_SetMode(SPI_Mode_Master);
    } else {
        SPI_SetMode(SPI_Mode_Slave);
    }
}

void ef_spi_write_byte(u8 data)
{
    SPI_WriteByte(data);
}

u8 ef_spi_transfer_byte(u8 data)
{
    if (ESPI) {
        B_SPI_Busy = 1;
        SPDAT = data;
        while (B_SPI_Busy) {
        }
        return SPDAT;
    }

    SPDAT = data;
    while (SPIF == 0) {
    }
    SPI_ClearFlag();
    return SPDAT;
}

u8 ef_spi_read_byte(void)
{
    return SPI_ReadByte();
}

u8 ef_spi_slave_rx_tick(void)
{
    if (SPI_RxTimerOut > 0U) {
        SPI_RxTimerOut--;
        if ((SPI_RxTimerOut == 0U) && (SPI_RxCnt > 0U)) {
            return 1U;
        }
    }

    return 0U;
}

u8 ef_spi_slave_rx_count(void)
{
    return SPI_RxCnt;
}

int8 ef_spi_slave_rx_read(u8 *buffer, u8 max_len, u8 *out_len)
{
    u8 i;
    u8 count;
    u8 copy_len;
    u8 old_espi;

    if ((buffer == 0) || (out_len == 0) || (max_len == 0U)) {
        return EF_SPI_ERR_PARAM;
    }

    old_espi = ESPI;
    ESPI = 0;

    count = SPI_RxCnt;
    copy_len = (count > max_len) ? max_len : count;
    for (i = 0U; i < copy_len; i++) {
        buffer[i] = SPI_RxBuffer[i];
    }

    SPI_RxCnt = 0U;
    SPI_RxTimerOut = 0U;
    *out_len = copy_len;

    ESPI = old_espi;

    return (count > max_len) ? EF_SPI_ERR_OVERFLOW : EF_SPI_OK;
}

void ef_spi_slave_rx_reset(void)
{
    u8 old_espi;

    old_espi = ESPI;
    ESPI = 0;

    SPI_RxCnt = 0U;
    SPI_RxTimerOut = 0U;

    ESPI = old_espi;
}
