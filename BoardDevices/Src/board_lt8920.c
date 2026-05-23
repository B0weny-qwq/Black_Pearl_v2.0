#include "board_lt8920.h"
#include "KCT8206.h"
#include "LT8920.h"
#include "ef_spi.h"
#include "STC32G_Delay.h"
#include "STC32G_GPIO.h"
#include "STC32G_Switch.h"
#include "stc32g.h"

typedef struct
{
    lt8920_t radio;
    kct8206_t frontend;
    int8 last_error;
    u8 ready;
} board_lt8920_context_t;

static board_lt8920_context_t board_lt8920_ctx = { {0}, {0}, BOARD_LT8920_ERR_NOT_READY, 0U };

static void board_lt8920_set_cs(void *ctx, u8 level)
{
    (void)ctx;
    P35 = (level != 0U) ? 1 : 0;
}

static u8 board_lt8920_spi_transfer(void *ctx, u8 value)
{
    (void)ctx;
    return ef_spi_transfer_byte(value);
}

static void board_lt8920_delay_us(void *ctx, u16 us)
{
    u8 dly;

    (void)ctx;

    while (us > 0U) {
        dly = (u8)(MAIN_Fosc / 2000000UL);
        while (--dly) {
        }
        us--;
    }
}

static void board_lt8920_delay_ms(void *ctx, u16 ms)
{
    (void)ctx;
    delay_ms(ms);
}

static void board_lt8920_set_rst(u8 level)
{
    P50 = (level != 0U) ? 1 : 0;
}

static void board_lt8920_set_ant(void *ctx, u8 level)
{
    (void)ctx;
    P51 = (level != 0U) ? 1 : 0;
}

static void board_lt8920_set_rxen(void *ctx, u8 level)
{
    (void)ctx;
    P13 = (level != 0U) ? 1 : 0;
}

static void board_lt8920_set_txen(void *ctx, u8 level)
{
    (void)ctx;
    P54 = (level != 0U) ? 1 : 0;
}

static u8 board_lt8920_get_ant(void *ctx)
{
    (void)ctx;
    return (P51 != 0U) ? 1U : 0U;
}

static u8 board_lt8920_get_rxen(void *ctx)
{
    (void)ctx;
    return (P13 != 0U) ? 1U : 0U;
}

static u8 board_lt8920_get_txen(void *ctx)
{
    (void)ctx;
    return (P54 != 0U) ? 1U : 0U;
}

static int8 board_lt8920_port_init(void)
{
    ef_spi_config_t spi_config;

    EAXSFR();

    P1_MODE_OUT_PP(GPIO_Pin_3);
    P1_PULL_UP_DISABLE(GPIO_Pin_3);
    P1_SPEED_HIGH(GPIO_Pin_3);

    P3_MODE_OUT_PP(GPIO_Pin_2 | GPIO_Pin_4 | GPIO_Pin_5);
    P3_MODE_IN_HIZ(GPIO_Pin_3);
    P3_PULL_UP_ENABLE(GPIO_Pin_3 | GPIO_Pin_5);
    P3_SPEED_HIGH(GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5);

    P5_MODE_OUT_PP(GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_4);
    P5_PULL_UP_DISABLE(GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_4);
    P5_SPEED_HIGH(GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_4);

    board_lt8920_set_cs(0, 1U);
    board_lt8920_set_rst(1U);
    board_lt8920_set_rxen(0, 0U);
    board_lt8920_set_txen(0, 0U);
    board_lt8920_set_ant(0, 0U);

    SPI_SW(SPI_P35_P34_P33_P32);

    spi_config.enable = EF_SPI_ENABLE;
    spi_config.ss_control = EF_SPI_SS_IGNORE;
    spi_config.first_bit = EF_SPI_FIRST_MSB;
    spi_config.mode = EF_SPI_MODE_MASTER;
    spi_config.cpol = EF_SPI_CPOL_LOW;
    spi_config.cpha = EF_SPI_CPHA_2EDGE;
    spi_config.speed = EF_SPI_SPEED_FOSC_16;
    spi_config.irq_enable = EF_SPI_DISABLE;

    return ef_spi_init(&spi_config);
}

int8 board_lt8920_init(void)
{
    lt8920_bus_t radio_bus;
    kct8206_bus_t fe_bus;
    int8 ret;

    board_lt8920_ctx.ready = 0U;
    board_lt8920_ctx.last_error = BOARD_LT8920_ERR_NOT_READY;

    ret = board_lt8920_port_init();
    if (ret != EF_SPI_OK) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_DRIVER;
        return BOARD_LT8920_ERR_DRIVER;
    }

    fe_bus.ctx = 0;
    fe_bus.set_rxen = board_lt8920_set_rxen;
    fe_bus.set_txen = board_lt8920_set_txen;
    fe_bus.set_ant_sel = board_lt8920_set_ant;
    fe_bus.get_rxen = board_lt8920_get_rxen;
    fe_bus.get_txen = board_lt8920_get_txen;
    fe_bus.get_ant_sel = board_lt8920_get_ant;
    fe_bus.delay_us = board_lt8920_delay_us;

    ret = KCT8206_Bind(&board_lt8920_ctx.frontend, &fe_bus);
    if (ret != KCT8206_OK) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_DRIVER;
        return BOARD_LT8920_ERR_DRIVER;
    }

    radio_bus.ctx = 0;
    radio_bus.spi_transfer = board_lt8920_spi_transfer;
    radio_bus.set_cs = board_lt8920_set_cs;
    radio_bus.delay_us = board_lt8920_delay_us;
    radio_bus.delay_ms = board_lt8920_delay_ms;

    ret = LT8920_Bind(&board_lt8920_ctx.radio, &radio_bus);
    if (ret != LT8920_OK) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_DRIVER;
        return BOARD_LT8920_ERR_DRIVER;
    }

    board_lt8920_set_rst(1U);
    delay_ms(10U);
    board_lt8920_set_rst(0U);
    delay_ms(100U);
    board_lt8920_set_rst(1U);
    delay_ms(100U);

    ret = LT8920_Init(&board_lt8920_ctx.radio,
                      LT8920_DEFAULT_CHANNEL,
                      LT8920_DEFAULT_SYNC_WORD);
    if (ret == LT8920_ERR_VERIFY) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_VERIFY;
        return BOARD_LT8920_ERR_VERIFY;
    }
    if (ret != LT8920_OK) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_DRIVER;
        return BOARD_LT8920_ERR_DRIVER;
    }

    (void)KCT8206_EnterIdle(&board_lt8920_ctx.frontend);
    board_lt8920_ctx.ready = 1U;
    board_lt8920_ctx.last_error = BOARD_LT8920_OK;
    return BOARD_LT8920_OK;
}

u8 board_lt8920_is_ready(void)
{
    return board_lt8920_ctx.ready;
}

int8 board_lt8920_get_last_error(void)
{
    return board_lt8920_ctx.last_error;
}

void board_lt8920_get_verify_failure(u8 *reg, u16 *expected, u16 *actual)
{
    LT8920_GetVerifyFailure(&board_lt8920_ctx.radio, reg, expected, actual);
}
