#include "board_lt8920.h"
#include "ef_board_resources.h"
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
static volatile u8 board_lt8920_ctx_seen = 0U;

static void board_lt8920_delay_us(void *ctx, u16 us);

static int8 board_lt8920_map_radio_error(int8 ret)
{
    if (ret == LT8920_OK) {
        return BOARD_LT8920_OK;
    }
    if (ret == LT8920_ERR_PARAM) {
        return BOARD_LT8920_ERR_PARAM;
    }
    if (ret == LT8920_ERR_EMPTY) {
        return BOARD_LT8920_ERR_EMPTY;
    }
    if (ret == LT8920_ERR_OVERFLOW) {
        return BOARD_LT8920_ERR_OVERFLOW;
    }
    if (ret == LT8920_ERR_CRC) {
        return BOARD_LT8920_ERR_CRC;
    }
    if (ret == LT8920_ERR_VERIFY) {
        return BOARD_LT8920_ERR_VERIFY;
    }

    return BOARD_LT8920_ERR_DRIVER;
}

static int8 board_lt8920_require_ready(void)
{
    if (board_lt8920_ctx.ready == 0U) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_NOT_READY;
        return BOARD_LT8920_ERR_NOT_READY;
    }

    return BOARD_LT8920_OK;
}

static int8 board_lt8920_wait_tx_done(void)
{
    u16 timeout_cnt;
    u16 status;
    int8 ret;

    for (timeout_cnt = 0U; timeout_cnt < 1000U; timeout_cnt++) {
        ret = LT8920_ReadStatus(&board_lt8920_ctx.radio, &status);
        if (ret != LT8920_OK) {
            return board_lt8920_map_radio_error(ret);
        }
        if ((status & LT8920_STATUS_PKT_FLAG) != 0U) {
            return BOARD_LT8920_OK;
        }
        board_lt8920_delay_us(0, 1000U);
    }

    (void)LT8920_ClearTxFifo(&board_lt8920_ctx.radio);
    return BOARD_LT8920_ERR_DRIVER;
}

static void board_lt8920_touch_ctx(void *ctx)
{
    board_lt8920_ctx_seen = (ctx != 0) ? 1U : 0U;
}

static void board_lt8920_set_cs(void *ctx, u8 level)
{
    board_lt8920_touch_ctx(ctx);
    EF_BOARD_LT8920_CS_BIT = (level != 0U) ? 1 : 0;
}

static u8 board_lt8920_spi_transfer(void *ctx, u8 value)
{
    board_lt8920_touch_ctx(ctx);
    return ef_spi_transfer_byte(value);
}

static void board_lt8920_delay_us(void *ctx, u16 us)
{
    u8 dly;

    board_lt8920_touch_ctx(ctx);

    while (us > 0U) {
        dly = (u8)(MAIN_Fosc / 2000000UL);
        while (--dly) {
        }
        us--;
    }
}

static void board_lt8920_delay_ms(void *ctx, u16 ms)
{
    board_lt8920_touch_ctx(ctx);
    delay_ms(ms);
}

static void board_lt8920_set_rst(u8 level)
{
    EF_BOARD_LT8920_RESET_BIT = (level != 0U) ? 1 : 0;
}

static void board_lt8920_set_ant(void *ctx, u8 level)
{
    board_lt8920_touch_ctx(ctx);
    EF_BOARD_KCT8206_ANT_SEL_BIT = (level != 0U) ? 1 : 0;
}

static void board_lt8920_set_rxen(void *ctx, u8 level)
{
    board_lt8920_touch_ctx(ctx);
    EF_BOARD_KCT8206_RXEN_BIT = (level != 0U) ? 1 : 0;
}

static void board_lt8920_set_txen(void *ctx, u8 level)
{
    board_lt8920_touch_ctx(ctx);
    EF_BOARD_KCT8206_TXEN_BIT = (level != 0U) ? 1 : 0;
}

static u8 board_lt8920_get_ant(void *ctx)
{
    board_lt8920_touch_ctx(ctx);
    return (EF_BOARD_KCT8206_ANT_SEL_BIT != 0U) ? 1U : 0U;
}

static u8 board_lt8920_get_rxen(void *ctx)
{
    board_lt8920_touch_ctx(ctx);
    return (EF_BOARD_KCT8206_RXEN_BIT != 0U) ? 1U : 0U;
}

static u8 board_lt8920_get_txen(void *ctx)
{
    board_lt8920_touch_ctx(ctx);
    return (EF_BOARD_KCT8206_TXEN_BIT != 0U) ? 1U : 0U;
}

static int8 board_lt8920_port_init(void)
{
    ef_spi_config_t spi_config;

    EAXSFR();

    P1_MODE_OUT_PP(EF_BOARD_KCT8206_RXEN_PIN_MASK);
    P1_PULL_UP_DISABLE(EF_BOARD_KCT8206_RXEN_PIN_MASK);
    P1_SPEED_HIGH(EF_BOARD_KCT8206_RXEN_PIN_MASK);

    P3_MODE_OUT_PP(EF_BOARD_LT8920_SPI_SCLK_PIN_MASK |
                   EF_BOARD_LT8920_SPI_MOSI_PIN_MASK |
                   EF_BOARD_LT8920_SPI_CS_PIN_MASK);
    P3_MODE_IN_HIZ(EF_BOARD_LT8920_SPI_MISO_PIN_MASK);
    P3_PULL_UP_ENABLE(EF_BOARD_LT8920_SPI_MISO_PIN_MASK |
                      EF_BOARD_LT8920_SPI_CS_PIN_MASK);
    P3_SPEED_HIGH(EF_BOARD_LT8920_SPI_SCLK_PIN_MASK |
                  EF_BOARD_LT8920_SPI_MOSI_PIN_MASK |
                  EF_BOARD_LT8920_SPI_MISO_PIN_MASK |
                  EF_BOARD_LT8920_SPI_CS_PIN_MASK);

    P5_MODE_OUT_PP(EF_BOARD_LT8920_RESET_PIN_MASK |
                   EF_BOARD_KCT8206_ANT_SEL_PIN_MASK |
                   EF_BOARD_KCT8206_TXEN_PIN_MASK);
    P5_PULL_UP_DISABLE(EF_BOARD_LT8920_RESET_PIN_MASK |
                       EF_BOARD_KCT8206_ANT_SEL_PIN_MASK |
                       EF_BOARD_KCT8206_TXEN_PIN_MASK);
    P5_SPEED_HIGH(EF_BOARD_LT8920_RESET_PIN_MASK |
                  EF_BOARD_KCT8206_ANT_SEL_PIN_MASK |
                  EF_BOARD_KCT8206_TXEN_PIN_MASK);

    board_lt8920_set_cs(0, 1U);
    board_lt8920_set_rst(1U);
    board_lt8920_set_rxen(0, 0U);
    board_lt8920_set_txen(0, 0U);
    board_lt8920_set_ant(0, 0U);

    SPI_SW(EF_BOARD_LT8920_SPI_MUX);

    spi_config.enable = EF_SPI_ENABLE;
    spi_config.ss_control = EF_SPI_SS_IGNORE;
    spi_config.first_bit = EF_SPI_FIRST_MSB;
    spi_config.mode = EF_SPI_MODE_MASTER;
    spi_config.cpol = EF_SPI_CPOL_LOW;
    spi_config.cpha = EF_SPI_CPHA_2EDGE;
    spi_config.speed = EF_BOARD_LT8920_SPI_SPEED;
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
                      EF_BOARD_LT8920_DEFAULT_CHANNEL,
                      EF_BOARD_LT8920_DEFAULT_SYNC_WORD);
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

int8 board_lt8920_enter_idle(void)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = KCT8206_EnterIdle(&board_lt8920_ctx.frontend);
    if (ret != KCT8206_OK) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_DRIVER;
        return BOARD_LT8920_ERR_DRIVER;
    }

    ret = board_lt8920_map_radio_error(LT8920_EnterIdle(&board_lt8920_ctx.radio));
    board_lt8920_ctx.last_error = ret;
    return ret;
}

int8 board_lt8920_open_rx(void)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = KCT8206_EnterRx(&board_lt8920_ctx.frontend);
    if (ret != KCT8206_OK) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_DRIVER;
        return BOARD_LT8920_ERR_DRIVER;
    }

    ret = board_lt8920_map_radio_error(LT8920_OpenRx(&board_lt8920_ctx.radio));
    board_lt8920_ctx.last_error = ret;
    return ret;
}

int8 board_lt8920_open_rx_on_channel(u8 channel)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = KCT8206_EnterRx(&board_lt8920_ctx.frontend);
    if (ret != KCT8206_OK) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_DRIVER;
        return BOARD_LT8920_ERR_DRIVER;
    }

    ret = board_lt8920_map_radio_error(LT8920_OpenRxOnChannel(&board_lt8920_ctx.radio, channel));
    board_lt8920_ctx.last_error = ret;
    return ret;
}

int8 board_lt8920_send_packet(const u8 *buf, u8 len)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = KCT8206_PrepareLegacyTx(&board_lt8920_ctx.frontend);
    if (ret != KCT8206_OK) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_DRIVER;
        return BOARD_LT8920_ERR_DRIVER;
    }

    ret = board_lt8920_map_radio_error(LT8920_StartTxPacket(&board_lt8920_ctx.radio, buf, len));
    if (ret != BOARD_LT8920_OK) {
        board_lt8920_ctx.last_error = ret;
        return ret;
    }

    ret = KCT8206_EnterTx(&board_lt8920_ctx.frontend);
    if (ret != KCT8206_OK) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_DRIVER;
        return BOARD_LT8920_ERR_DRIVER;
    }

    ret = board_lt8920_wait_tx_done();
    if (ret != BOARD_LT8920_OK) {
        (void)KCT8206_EnterIdle(&board_lt8920_ctx.frontend);
        (void)LT8920_EnterIdle(&board_lt8920_ctx.radio);
        board_lt8920_ctx.last_error = ret;
        return ret;
    }

    board_lt8920_ctx.last_error = BOARD_LT8920_OK;
    return BOARD_LT8920_OK;
}

int8 board_lt8920_send_packet_on_channel(u8 channel, const u8 *buf, u8 len)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = board_lt8920_map_radio_error(LT8920_SetChannel(&board_lt8920_ctx.radio, channel));
    if (ret != BOARD_LT8920_OK) {
        board_lt8920_ctx.last_error = ret;
        return ret;
    }

    ret = board_lt8920_send_packet(buf, len);
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = board_lt8920_map_radio_error(LT8920_EnterIdle(&board_lt8920_ctx.radio));
    if (ret != BOARD_LT8920_OK) {
        board_lt8920_ctx.last_error = ret;
        return ret;
    }

    board_lt8920_ctx.last_error = BOARD_LT8920_OK;
    return BOARD_LT8920_OK;
}

int8 board_lt8920_read_packet(u8 *buf, u8 buf_len, u8 *out_len)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = board_lt8920_map_radio_error(LT8920_ReadPacket(&board_lt8920_ctx.radio, buf, buf_len, out_len));
    board_lt8920_ctx.last_error = ret;
    return ret;
}

int8 board_lt8920_read_status(u16 *status)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = board_lt8920_map_radio_error(LT8920_ReadStatus(&board_lt8920_ctx.radio, status));
    board_lt8920_ctx.last_error = ret;
    return ret;
}

int8 board_lt8920_read_raw_rssi(u8 *rssi)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = board_lt8920_map_radio_error(LT8920_ReadRawRssi(&board_lt8920_ctx.radio, rssi));
    board_lt8920_ctx.last_error = ret;
    return ret;
}

int8 board_lt8920_set_channel(u8 channel)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = board_lt8920_map_radio_error(LT8920_SetChannel(&board_lt8920_ctx.radio, channel));
    board_lt8920_ctx.last_error = ret;
    return ret;
}

int8 board_lt8920_set_sync_word(u32 sync_word)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = board_lt8920_map_radio_error(LT8920_SetSyncWord(&board_lt8920_ctx.radio, sync_word));
    board_lt8920_ctx.last_error = ret;
    return ret;
}

int8 board_lt8920_set_sync_regs(u16 reg36, u16 reg39)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = board_lt8920_map_radio_error(LT8920_SetSyncRegs(&board_lt8920_ctx.radio, reg36, reg39));
    board_lt8920_ctx.last_error = ret;
    return ret;
}

int8 board_lt8920_set_antenna(u8 antenna)
{
    int8 ret;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }

    ret = KCT8206_SetAntenna(&board_lt8920_ctx.frontend,
                             (antenna == BOARD_LT8920_ANT2) ? KCT8206_ANT2 : KCT8206_ANT1);
    if (ret != KCT8206_OK) {
        board_lt8920_ctx.last_error = BOARD_LT8920_ERR_PARAM;
        return BOARD_LT8920_ERR_PARAM;
    }

    board_lt8920_ctx.last_error = BOARD_LT8920_OK;
    return BOARD_LT8920_OK;
}

int8 board_lt8920_get_debug(board_lt8920_debug_t *debug)
{
    int8 ret;
    kct8206_status_t frontend_status;

    ret = board_lt8920_require_ready();
    if (ret != BOARD_LT8920_OK) {
        return ret;
    }
    if (debug == 0) {
        return BOARD_LT8920_ERR_PARAM;
    }

    debug->reg7 = 0U;
    debug->reg8 = 0U;
    debug->reg36 = 0U;
    debug->reg37 = 0U;
    debug->reg38 = 0U;
    debug->reg39 = 0U;
    debug->reg48 = 0U;
    debug->reg52 = 0U;
    debug->rssi = 0U;
    debug->rx_en = 0U;
    debug->tx_en = 0U;
    debug->ant_sel = 0U;
    debug->frontend_state = 0U;
    debug->radio_channel = LT8920_GetChannel(&board_lt8920_ctx.radio);

    (void)LT8920_ReadReg(&board_lt8920_ctx.radio, 7U, &debug->reg7);
    (void)LT8920_ReadReg(&board_lt8920_ctx.radio, 8U, &debug->reg8);
    (void)LT8920_ReadReg(&board_lt8920_ctx.radio, 36U, &debug->reg36);
    (void)LT8920_ReadReg(&board_lt8920_ctx.radio, 37U, &debug->reg37);
    (void)LT8920_ReadReg(&board_lt8920_ctx.radio, 38U, &debug->reg38);
    (void)LT8920_ReadReg(&board_lt8920_ctx.radio, 39U, &debug->reg39);
    (void)LT8920_ReadStatus(&board_lt8920_ctx.radio, &debug->reg48);
    (void)LT8920_ReadReg(&board_lt8920_ctx.radio, 52U, &debug->reg52);
    (void)LT8920_ReadRawRssi(&board_lt8920_ctx.radio, &debug->rssi);

    if (KCT8206_ReadStatus(&board_lt8920_ctx.frontend, &frontend_status) == KCT8206_OK) {
        debug->rx_en = frontend_status.rxen;
        debug->tx_en = frontend_status.txen;
        debug->ant_sel = frontend_status.ant_sel;
        debug->frontend_state = frontend_status.state;
    }

    return BOARD_LT8920_OK;
}

u8 board_lt8920_get_channel(void)
{
    return LT8920_GetChannel(&board_lt8920_ctx.radio);
}
