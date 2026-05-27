#include "board_wireless.h"
#include "board_lt8920.h"
#include "LT8920.h"

#define BOARD_WIRELESS_SCAN_SAMPLE_COUNT 16U
#define BOARD_WIRELESS_SCAN_SAMPLE_MS    2U
#define BOARD_WIRELESS_SIGNAL_RSSI_MIN   12U
#define BOARD_WIRELESS_SEARCH_POLL_DIV   32U
#define BOARD_WIRELESS_SEARCH_MAX_RETRY  5U

static board_wireless_state_t board_wireless_state;
static u8 board_wireless_rx_len[BOARD_WIRELESS_RX_QUEUE_DEPTH];
/*
 * RF payload 只在 BoardDevices 内部入队/出队，对 App 暴露的仍是拷贝接口。
 * 该队列按 4 x 60 字节占用较大，放入 XDATA 以给 C251 的 EDATA 栈留出空间。
 */
static u8 EF_LARGE_DATA board_wireless_rx_data[BOARD_WIRELESS_RX_QUEUE_DEPTH][BOARD_WIRELESS_MAX_PAYLOAD_LEN];
static u8 board_wireless_work_packet[BOARD_WIRELESS_MAX_PAYLOAD_LEN];
static u8 board_wireless_rx_head;
static u8 board_wireless_rx_tail;
static u8 board_wireless_rx_count;

static int8 board_wireless_map_error(int8 ret)
{
    if (ret == BOARD_LT8920_OK) {
        return BOARD_WIRELESS_OK;
    }
    if (ret == BOARD_LT8920_ERR_PARAM) {
        return BOARD_WIRELESS_ERR_PARAM;
    }
    if (ret == BOARD_LT8920_ERR_NOT_READY) {
        return BOARD_WIRELESS_ERR_STATE;
    }
    if (ret == BOARD_LT8920_ERR_EMPTY) {
        return BOARD_WIRELESS_ERR_EMPTY;
    }
    if (ret == BOARD_LT8920_ERR_OVERFLOW) {
        return BOARD_WIRELESS_ERR_OVERFLOW;
    }
    if (ret == BOARD_LT8920_ERR_VERIFY) {
        return BOARD_WIRELESS_ERR_VERIFY;
    }

    return BOARD_WIRELESS_ERR_IO;
}

static void board_wireless_delay_ms(u16 ms)
{
    u16 outer;
    u16 inner;

    for (outer = 0U; outer < ms; outer++) {
        for (inner = 0U; inner < 6000U; inner++) {
        }
    }
}

static void board_wireless_reset_state(void)
{
    u16 i;
    u8 *raw;

    raw = (u8 *)&board_wireless_state;
    for (i = 0U; i < (u16)sizeof(board_wireless_state_t); i++) {
        raw[i] = 0U;
    }

    board_wireless_state.antenna = BOARD_WIRELESS_ANT1;
    board_wireless_state.mode = BOARD_WIRELESS_MODE_IDLE;
    board_wireless_state.last_error = BOARD_WIRELESS_ERR_STATE;
}

static void board_wireless_reset_queue(void)
{
    u8 i;
    u8 j;

    board_wireless_rx_head = 0U;
    board_wireless_rx_tail = 0U;
    board_wireless_rx_count = 0U;

    for (i = 0U; i < BOARD_WIRELESS_RX_QUEUE_DEPTH; i++) {
        board_wireless_rx_len[i] = 0U;
        for (j = 0U; j < BOARD_WIRELESS_MAX_PAYLOAD_LEN; j++) {
            board_wireless_rx_data[i][j] = 0U;
        }
    }
}

static int8 board_wireless_queue_push(const u8 *buf, u8 len)
{
    u8 i;

    if ((buf == 0) || (len == 0U) || (len > BOARD_WIRELESS_MAX_PAYLOAD_LEN)) {
        return BOARD_WIRELESS_ERR_PARAM;
    }
    if (board_wireless_rx_count >= BOARD_WIRELESS_RX_QUEUE_DEPTH) {
        board_wireless_state.queue_overflow_count++;
        return BOARD_WIRELESS_ERR_OVERFLOW;
    }

    board_wireless_rx_len[board_wireless_rx_head] = len;
    for (i = 0U; i < len; i++) {
        board_wireless_rx_data[board_wireless_rx_head][i] = buf[i];
    }

    board_wireless_rx_head++;
    if (board_wireless_rx_head >= BOARD_WIRELESS_RX_QUEUE_DEPTH) {
        board_wireless_rx_head = 0U;
    }
    board_wireless_rx_count++;
    return BOARD_WIRELESS_OK;
}

static int8 board_wireless_queue_pop(u8 *buf, u8 buf_len, u8 *out_len)
{
    u8 i;
    u8 len;

    if ((buf == 0) || (out_len == 0)) {
        return BOARD_WIRELESS_ERR_PARAM;
    }
    if (board_wireless_rx_count == 0U) {
        return BOARD_WIRELESS_ERR_EMPTY;
    }

    len = board_wireless_rx_len[board_wireless_rx_tail];
    if (len > buf_len) {
        return BOARD_WIRELESS_ERR_OVERFLOW;
    }

    for (i = 0U; i < len; i++) {
        buf[i] = board_wireless_rx_data[board_wireless_rx_tail][i];
    }
    *out_len = len;

    board_wireless_rx_tail++;
    if (board_wireless_rx_tail >= BOARD_WIRELESS_RX_QUEUE_DEPTH) {
        board_wireless_rx_tail = 0U;
    }
    board_wireless_rx_count--;
    return BOARD_WIRELESS_OK;
}

static int8 board_wireless_enter_idle(void)
{
    int8 ret;

    ret = board_wireless_map_error(board_lt8920_enter_idle());
    if (ret == BOARD_WIRELESS_OK) {
        board_wireless_state.mode = BOARD_WIRELESS_MODE_IDLE;
    }
    return ret;
}

static int8 board_wireless_open_rx(void)
{
    int8 ret;

    ret = board_wireless_map_error(board_lt8920_open_rx());
    if (ret == BOARD_WIRELESS_OK) {
        board_wireless_state.mode = BOARD_WIRELESS_MODE_RX;
    }
    return ret;
}

static int8 board_wireless_open_rx_on_channel(u8 channel)
{
    int8 ret;

    ret = board_wireless_map_error(board_lt8920_open_rx_on_channel(channel));
    if (ret == BOARD_WIRELESS_OK) {
        board_wireless_state.mode = BOARD_WIRELESS_MODE_RX;
    }
    return ret;
}

static int8 board_wireless_probe_antenna(u8 antenna, u16 *avg_rssi, u16 *pkt_ok, u16 *crc_err)
{
    u8 sample_idx;
    u8 rssi;
    u16 sum;
    u16 status;
    u8 packet_len;
    int8 ret;

    if ((avg_rssi == 0) || (pkt_ok == 0) || (crc_err == 0)) {
        return BOARD_WIRELESS_ERR_PARAM;
    }

    *avg_rssi = 0U;
    *pkt_ok = 0U;
    *crc_err = 0U;
    sum = 0U;

    ret = board_wireless_map_error(board_lt8920_set_antenna(antenna));
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }
    ret = board_wireless_open_rx();
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }

    for (sample_idx = 0U; sample_idx < BOARD_WIRELESS_SCAN_SAMPLE_COUNT; sample_idx++) {
        if (board_lt8920_read_raw_rssi(&rssi) == BOARD_LT8920_OK) {
            sum = (u16)(sum + rssi);
        }

        if (board_lt8920_read_status(&status) == BOARD_LT8920_OK) {
            if ((status & LT8920_STATUS_PKT_FLAG) != 0U) {
                if ((status & LT8920_STATUS_CRC_ERROR) != 0U) {
                    (*crc_err)++;
                } else if (board_lt8920_read_packet(board_wireless_work_packet,
                                                    BOARD_WIRELESS_MAX_PAYLOAD_LEN,
                                                    &packet_len) == BOARD_LT8920_OK) {
                    (*pkt_ok)++;
                }
                (void)board_wireless_open_rx();
            }
        }

        board_wireless_delay_ms(BOARD_WIRELESS_SCAN_SAMPLE_MS);
    }

    *avg_rssi = (u16)(sum / BOARD_WIRELESS_SCAN_SAMPLE_COUNT);
    return BOARD_WIRELESS_OK;
}

int8 board_wireless_init(void)
{
    int8 ret;

    board_wireless_reset_state();
    board_wireless_reset_queue();

    ret = board_wireless_map_error(board_lt8920_init());
    if (ret != BOARD_WIRELESS_OK) {
        board_wireless_state.last_error = ret;
        return ret;
    }

    board_wireless_state.initialized = 1U;

    ret = board_wireless_rescan_antenna();
    if (ret != BOARD_WIRELESS_OK) {
        board_wireless_state.initialized = 0U;
        board_wireless_state.ready = 0U;
        board_wireless_state.last_error = ret;
        (void)board_wireless_enter_idle();
        return ret;
    }

    ret = board_wireless_open_rx();
    if (ret != BOARD_WIRELESS_OK) {
        board_wireless_state.initialized = 0U;
        board_wireless_state.ready = 0U;
        board_wireless_state.last_error = ret;
        (void)board_wireless_enter_idle();
        return ret;
    }

    board_wireless_state.ready = 1U;
    board_wireless_state.last_error = BOARD_WIRELESS_OK;
    return BOARD_WIRELESS_OK;
}

int8 board_wireless_deinit(void)
{
    if (board_wireless_state.initialized == 0U) {
        return BOARD_WIRELESS_OK;
    }

    (void)board_wireless_enter_idle();
    board_wireless_reset_queue();
    board_wireless_reset_state();
    return BOARD_WIRELESS_OK;
}

int8 board_wireless_poll(void)
{
    static u8 rx_crc_streak;
    u16 status;
    u8 packet_len;
    int8 ret;

    if (board_wireless_state.initialized == 0U) {
        return BOARD_WIRELESS_ERR_STATE;
    }
    if (board_wireless_state.ready == 0U) {
        return BOARD_WIRELESS_OK;
    }

    ret = board_wireless_map_error(board_lt8920_read_status(&status));
    if (ret != BOARD_WIRELESS_OK) {
        board_wireless_state.last_error = ret;
        return ret;
    }
    if ((status & LT8920_STATUS_PKT_FLAG) == 0U) {
        return BOARD_WIRELESS_OK;
    }

    if (board_wireless_state.mode == BOARD_WIRELESS_MODE_TX) {
        board_wireless_state.tx_ok_count++;
        ret = board_wireless_open_rx();
        board_wireless_state.last_error = ret;
        return ret;
    }
    if (board_wireless_state.mode != BOARD_WIRELESS_MODE_RX) {
        return BOARD_WIRELESS_OK;
    }

    if ((status & LT8920_STATUS_CRC_ERROR) != 0U) {
        board_wireless_state.crc_error_count++;
        if (rx_crc_streak < 0xFFU) {
            rx_crc_streak++;
        }
        if (rx_crc_streak >= 4U) {
            rx_crc_streak = 0U;
        }
        (void)board_wireless_open_rx();
        return BOARD_WIRELESS_OK;
    }

    rx_crc_streak = 0U;
    ret = board_wireless_map_error(board_lt8920_read_packet(board_wireless_work_packet,
                                                            BOARD_WIRELESS_MAX_PAYLOAD_LEN,
                                                            &packet_len));
    if (ret == BOARD_WIRELESS_OK) {
        board_wireless_state.rx_ok_count++;
        ret = board_wireless_queue_push(board_wireless_work_packet, packet_len);
        if (ret != BOARD_WIRELESS_OK) {
            board_wireless_state.rx_drop_count++;
        }
    } else {
        board_wireless_state.rx_drop_count++;
        board_wireless_state.last_error = ret;
    }

    (void)board_wireless_open_rx();
    return BOARD_WIRELESS_OK;
}

int8 board_wireless_send(const u8 *buf, u8 len)
{
    int8 ret;

    if ((buf == 0) || (len == 0U) || (len > BOARD_WIRELESS_MAX_PAYLOAD_LEN)) {
        return BOARD_WIRELESS_ERR_PARAM;
    }
    if ((board_wireless_state.initialized == 0U) || (board_wireless_state.ready == 0U)) {
        return BOARD_WIRELESS_ERR_STATE;
    }

    ret = board_wireless_map_error(board_lt8920_send_packet(buf, len));
    if (ret == BOARD_WIRELESS_OK) {
        board_wireless_state.mode = BOARD_WIRELESS_MODE_TX;
        board_wireless_state.tx_ok_count++;
        (void)board_wireless_open_rx();
    }
    board_wireless_state.last_error = ret;
    return ret;
}

int8 board_wireless_send_on_channel(u8 channel, const u8 *buf, u8 len)
{
    int8 ret;

    if ((buf == 0) || (len == 0U) || (len > BOARD_WIRELESS_MAX_PAYLOAD_LEN)) {
        return BOARD_WIRELESS_ERR_PARAM;
    }
    if ((board_wireless_state.initialized == 0U) || (board_wireless_state.ready == 0U)) {
        return BOARD_WIRELESS_ERR_STATE;
    }

    ret = board_wireless_map_error(board_lt8920_send_packet_on_channel(channel, buf, len));
    if (ret == BOARD_WIRELESS_OK) {
        board_wireless_state.mode = BOARD_WIRELESS_MODE_IDLE;
        board_wireless_state.tx_ok_count++;
    }
    board_wireless_state.last_error = ret;
    return ret;
}

int8 board_wireless_receive(u8 *buf, u8 buf_len, u8 *out_len)
{
    return board_wireless_queue_pop(buf, buf_len, out_len);
}

int8 board_wireless_set_channel(u8 channel)
{
    int8 ret;

    if (board_wireless_state.initialized == 0U) {
        return BOARD_WIRELESS_ERR_STATE;
    }

    ret = board_wireless_open_rx_on_channel(channel);
    board_wireless_state.last_error = ret;
    return ret;
}

int8 board_wireless_set_sync_regs(u16 reg36, u16 reg39)
{
    int8 ret;

    if (board_wireless_state.initialized == 0U) {
        return BOARD_WIRELESS_ERR_STATE;
    }

    ret = board_wireless_map_error(board_lt8920_set_sync_regs(reg36, reg39));
    if (ret == BOARD_WIRELESS_OK) {
        ret = board_wireless_open_rx();
    }
    board_wireless_state.last_error = ret;
    return ret;
}

int8 board_wireless_set_sync_regs_idle(u16 reg36, u16 reg39)
{
    int8 ret;

    if (board_wireless_state.initialized == 0U) {
        return BOARD_WIRELESS_ERR_STATE;
    }

    ret = board_wireless_map_error(board_lt8920_set_sync_regs(reg36, reg39));
    if (ret == BOARD_WIRELESS_OK) {
        ret = board_wireless_enter_idle();
    }
    board_wireless_state.last_error = ret;
    return ret;
}

int8 board_wireless_get_state(board_wireless_state_t *state)
{
    u16 i;
    const u8 *src;
    u8 *dst;

    if (state == 0) {
        return BOARD_WIRELESS_ERR_PARAM;
    }

    src = (const u8 *)&board_wireless_state;
    dst = (u8 *)state;
    for (i = 0U; i < (u16)sizeof(board_wireless_state_t); i++) {
        dst[i] = src[i];
    }
    return BOARD_WIRELESS_OK;
}

int8 board_wireless_get_rx_debug(board_wireless_rx_debug_t *debug)
{
    board_lt8920_debug_t radio_debug;
    int8 ret;

    if (debug == 0) {
        return BOARD_WIRELESS_ERR_PARAM;
    }

    ret = board_wireless_map_error(board_lt8920_get_debug(&radio_debug));
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }

    debug->reg7 = radio_debug.reg7;
    debug->reg8 = radio_debug.reg8;
    debug->reg36 = radio_debug.reg36;
    debug->reg37 = radio_debug.reg37;
    debug->reg38 = radio_debug.reg38;
    debug->reg39 = radio_debug.reg39;
    debug->reg48 = radio_debug.reg48;
    debug->reg52 = radio_debug.reg52;
    debug->rssi = radio_debug.rssi;
    debug->rx_en = radio_debug.rx_en;
    debug->tx_en = radio_debug.tx_en;
    debug->mode = (u8)board_wireless_state.mode;
    debug->rx_mode_bit = ((radio_debug.reg7 & LT8920_MODE_RX) != 0U) ? 1U : 0U;
    debug->channel = (u8)(radio_debug.reg7 & 0x007FU);
    return BOARD_WIRELESS_OK;
}

int8 board_wireless_rescan_antenna(void)
{
    u16 ant1_rssi;
    u16 ant2_rssi;
    u16 ant1_ok;
    u16 ant2_ok;
    u16 ant1_crc;
    u16 ant2_crc;
    u8 final_ant;
    int8 ret;

    if (board_wireless_state.initialized == 0U) {
        return BOARD_WIRELESS_ERR_STATE;
    }

    board_wireless_state.ready = 0U;
    ret = board_wireless_enter_idle();
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }

    ret = board_wireless_probe_antenna(BOARD_WIRELESS_ANT1, &ant1_rssi, &ant1_ok, &ant1_crc);
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }
    ret = board_wireless_probe_antenna(BOARD_WIRELESS_ANT2, &ant2_rssi, &ant2_ok, &ant2_crc);
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }

    board_wireless_state.antenna_rssi_ant1 = ant1_rssi;
    board_wireless_state.antenna_rssi_ant2 = ant2_rssi;
    final_ant = BOARD_WIRELESS_ANT1;
    if ((ant2_ok > ant1_ok) || ((ant2_ok == ant1_ok) && (ant2_rssi > ant1_rssi))) {
        final_ant = BOARD_WIRELESS_ANT2;
    }
    if ((ant1_rssi < BOARD_WIRELESS_SIGNAL_RSSI_MIN) &&
        (ant2_rssi < BOARD_WIRELESS_SIGNAL_RSSI_MIN)) {
        board_wireless_state.scan_has_signal = 0U;
        final_ant = BOARD_WIRELESS_ANT1;
    } else {
        board_wireless_state.scan_has_signal = 1U;
    }

    ret = board_wireless_map_error(board_lt8920_set_antenna(final_ant));
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }
    board_wireless_state.antenna = final_ant;
    ret = board_wireless_open_rx();
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }

    board_wireless_state.ready = 1U;
    return BOARD_WIRELESS_OK;
}

int8 board_wireless_search_signal_poll(void)
{
    static u16 poll_div;
    static u8 search_retry_count;
    int8 ret;

    if (board_wireless_state.initialized == 0U) {
        return BOARD_WIRELESS_ERR_STATE;
    }
    if (board_wireless_state.scan_has_signal != 0U) {
        search_retry_count = 0U;
        return BOARD_WIRELESS_OK;
    }

    poll_div++;
    if (poll_div < BOARD_WIRELESS_SEARCH_POLL_DIV) {
        return BOARD_WIRELESS_OK;
    }
    poll_div = 0U;

    ret = board_wireless_rescan_antenna();
    if (ret != BOARD_WIRELESS_OK) {
        board_wireless_state.last_error = ret;
        return ret;
    }

    if (board_wireless_state.scan_has_signal != 0U) {
        search_retry_count = 0U;
    } else {
        search_retry_count++;
        if (search_retry_count >= BOARD_WIRELESS_SEARCH_MAX_RETRY) {
            board_wireless_state.scan_has_signal = 1U;
            (void)board_lt8920_set_antenna(BOARD_WIRELESS_ANT1);
            board_wireless_state.antenna = BOARD_WIRELESS_ANT1;
            (void)board_wireless_open_rx();
        }
    }

    return BOARD_WIRELESS_OK;
}
