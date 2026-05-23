/**
 * @file    wireless.c
 * @brief   LT8920 wireless link management layer.
 * @author  boweny
 * @date    2026-05-06
 * @version v1.1
 *
 * @details
 * This file manages initialization, antenna selection, TX/RX state,
 * receive queues, transmit entry points, and debug hooks above the LT8920
 * chip layer. The legacy remote-control workflow, including fixed-channel
 * transmit, idle register writes, and opening the working RX path, is
 * wrapped here for the protocol layer.
 *
 * @note
 * `Wireless_Receive()` returns LT8920 RF payload bytes, not the full
 * `AA | len | cmd | payload | xor | BB` business frame.
 */
#include "wireless.h"
#include "lt8920.h"
#include "wireless_port.h"
#include "..\..\Function\Log\Log.h"
#include "..\..\..\User\Task.h"

static Wireless_State_t g_wireless_state;
static u8 g_wireless_rx_len[WIRELESS_RX_QUEUE_DEPTH];
static u8 g_wireless_rx_data[WIRELESS_RX_QUEUE_DEPTH][LT8920_MAX_PAYLOAD_LEN];
static u8 g_wireless_rx_head = 0U;
static u8 g_wireless_rx_tail = 0U;
static u8 g_wireless_rx_count = 0U;

static s8 Wireless_GetLatchedErrorOrState(void)
{
    if (g_wireless_state.last_error != SUCCESS) {
        return g_wireless_state.last_error;
    }
    return WIRELESS_ERR_STATE;
}

static void Wireless_ResetState(void)
{
    u16 i;
    u8 *raw;

    raw = (u8 *)&g_wireless_state;
    for (i = 0U; i < (u16)sizeof(Wireless_State_t); i++) {
        raw[i] = 0U;
    }

    g_wireless_state.antenna = WIRELESS_ANT1;
    g_wireless_state.mode = WIRELESS_MODE_IDLE;
}

static void Wireless_ResetQueue(void)
{
    u8 i;
    u8 j;

    g_wireless_rx_head = 0U;
    g_wireless_rx_tail = 0U;
    g_wireless_rx_count = 0U;

    for (i = 0U; i < WIRELESS_RX_QUEUE_DEPTH; i++) {
        g_wireless_rx_len[i] = 0U;
        for (j = 0U; j < LT8920_MAX_PAYLOAD_LEN; j++) {
            g_wireless_rx_data[i][j] = 0U;
        }
    }
}

static s8 Wireless_QueuePush(const u8 *buf, u8 len)
{
    u8 i;

    if ((buf == 0) || (len == 0U) || (len > LT8920_MAX_PAYLOAD_LEN)) {
        return WIRELESS_ERR_PARAM;
    }
    if (g_wireless_rx_count >= WIRELESS_RX_QUEUE_DEPTH) {
        g_wireless_state.queue_overflow_count++;
        return WIRELESS_ERR_OVERFLOW;
    }

    g_wireless_rx_len[g_wireless_rx_head] = len;
    for (i = 0U; i < len; i++) {
        g_wireless_rx_data[g_wireless_rx_head][i] = buf[i];
    }
    g_wireless_rx_head++;
    if (g_wireless_rx_head >= WIRELESS_RX_QUEUE_DEPTH) {
        g_wireless_rx_head = 0U;
    }
    g_wireless_rx_count++;
    return SUCCESS;
}

static s8 Wireless_QueuePop(u8 *buf, u8 buf_len, u8 *out_len)
{
    u8 i;
    u8 len;

    if ((buf == 0) || (out_len == 0)) {
        return WIRELESS_ERR_PARAM;
    }
    if (g_wireless_rx_count == 0U) {
        return WIRELESS_ERR_EMPTY;
    }

    len = g_wireless_rx_len[g_wireless_rx_tail];
    if (len > buf_len) {
        return WIRELESS_ERR_OVERFLOW;
    }

    for (i = 0U; i < len; i++) {
        buf[i] = g_wireless_rx_data[g_wireless_rx_tail][i];
    }
    *out_len = len;

    g_wireless_rx_tail++;
    if (g_wireless_rx_tail >= WIRELESS_RX_QUEUE_DEPTH) {
        g_wireless_rx_tail = 0U;
    }
    g_wireless_rx_count--;
    return SUCCESS;
}

static s8 Wireless_SetIdleMode(void)
{
    s8 rc;

#if !WIRELESS_FRONTEND_BYPASS_TEST
    WirelessPort_SetTxEn(0U);
    WirelessPort_SetRxEn(0U);
#endif
    rc = LT8920_EnterIdle();
    if (rc == SUCCESS) {
        g_wireless_state.mode = WIRELESS_MODE_IDLE;
    }
    return rc;
}

static s8 Wireless_SetRxMode(void)
{
    s8 rc;

#if !WIRELESS_FRONTEND_BYPASS_TEST
    /* Enable the TX frontend when the legacy transmitter path is used.
     * Keep RX_EN/TX_EN sequencing in sync with LT8920_TxData(). */
    WirelessPort_SetRxEn(1U);
    WirelessPort_DelayUs(5U);
#endif

    rc = LT8920_OpenRx();
    if (rc == SUCCESS) {
        g_wireless_state.mode = WIRELESS_MODE_RX;
    }
    return rc;
}

static s8 Wireless_SetRxModeOnChannel(u8 channel)
{
    s8 rc;

#if !WIRELESS_FRONTEND_BYPASS_TEST
    WirelessPort_SetTxEn(0U);
    WirelessPort_SetRxEn(1U);
    WirelessPort_DelayUs(5U);
#endif

    rc = LT8920_OpenRxOnChannel(channel);
    if (rc == SUCCESS) {
        g_wireless_state.mode = WIRELESS_MODE_RX;
    }
    return rc;
}

static void Wireless_EnableTxFrontend(void)
{
#if !WIRELESS_FRONTEND_BYPASS_TEST
    /* Enable the TX frontend when the legacy transmitter path is used. 
     * Keep RX_EN/TX_EN sequencing in sync with LT8920_TxData(). */
    WirelessPort_SetRxEn(1U);
    WirelessPort_SetTxEn(0U);
    WirelessPort_DelayUs(5U);
    WirelessPort_SetTxEn(1U);
    WirelessPort_DelayUs(5U);
#endif
}

static s8 Wireless_ProbeAntenna(u8 antenna, u16 *avg_rssi, u16 *pkt_ok, u16 *crc_err)
{
    u8 sample_idx;
    u8 rssi;
    u8 packet_len;
    u8 packet_buf[LT8920_MAX_PAYLOAD_LEN];
    u16 status;
    u32 sum;
    u16 ok_cnt;
    u16 crc_cnt;
    s8 rc;

    if ((avg_rssi == 0) || (pkt_ok == 0) || (crc_err == 0)) {
        return WIRELESS_ERR_PARAM;
    }

    rc = Wireless_SetAntenna(antenna);
    if (rc != SUCCESS) {
        return rc;
    }

    rc = Wireless_SetRxMode();
    if (rc != SUCCESS) {
        return rc;
    }

    WirelessPort_DelayMs(2U);

    sum = 0UL;
    ok_cnt = 0U;
    crc_cnt = 0U;

    for (sample_idx = 0U; sample_idx < WIRELESS_SCAN_SAMPLE_COUNT; sample_idx++) {
        rc = LT8920_ReadRawRssi(&rssi);
        if (rc == SUCCESS) {
            sum += rssi;
        }

        rc = LT8920_ReadStatus(&status);
        if ((rc == SUCCESS) && ((status & LT8920_STATUS_PKT_FLAG) != 0U)) {
            if ((status & LT8920_STATUS_CRC_ERROR) != 0U) {
                crc_cnt++;
            } else {
                if (LT8920_ReadPacket(packet_buf, LT8920_MAX_PAYLOAD_LEN, &packet_len) == SUCCESS) {
                    ok_cnt++;
                }
            }
            (void)Wireless_SetRxMode();
        }

        WirelessPort_DelayMs(WIRELESS_SCAN_SAMPLE_MS);
    }

    *avg_rssi = (u16)(sum / WIRELESS_SCAN_SAMPLE_COUNT);
    *pkt_ok = ok_cnt;
    *crc_err = crc_cnt;
    return SUCCESS;
}

s8 Wireless_Init(void)
{
    s8 rc;
    u8 fail_reg;
    u16 fail_expect;
    u16 fail_actual;
    u16 reg11;
    u16 reg41;
    u16 reg52;

    LOGI(WIRELESS_TAG, "init start");
    Wireless_ResetState();
    Wireless_ResetQueue();

    rc = WirelessPort_Init();
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        LOGE(WIRELESS_TAG, "port init fail rc=%d", rc);
        return rc;
    }

    WirelessPort_SetRst(1U);
    WirelessPort_DelayMs(10U);
    WirelessPort_SetRst(0U);
    WirelessPort_DelayMs(100U);
    WirelessPort_SetRst(1U);
    WirelessPort_DelayMs(100U);

    rc = LT8920_Init(LT8920_DEFAULT_CHANNEL, LT8920_DEFAULT_SYNC_WORD);
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        LOGE(WIRELESS_TAG, "lt8920 init fail rc=%d", rc);
        LT8920_GetVerifyFailure(&fail_reg, &fail_expect, &fail_actual);
        if (fail_reg != 0xFFU) {
            LOGE(WIRELESS_TAG, "verify reg%u actual=0x%04X expect=0x%04X",
                 (u16)fail_reg, fail_actual, fail_expect);
        }
        if (LT8920_ReadReg(11U, &reg11) == SUCCESS) {
            LOGE(WIRELESS_TAG, "verify reg11=0x%04X expect=0x0008", reg11);
        }
        if (LT8920_ReadReg(41U, &reg41) == SUCCESS) {
            LOGE(WIRELESS_TAG, "verify reg41=0x%04X expect=0xB000", reg41);
        }
        if (LT8920_ReadReg(52U, &reg52) == SUCCESS) {
            LOGE(WIRELESS_TAG, "debug reg52=0x%04X", reg52);
        }
        return rc;
    }

    g_wireless_state.initialized = 1U;
#if WIRELESS_PAIR_TX_ONLY_TEST
#if !WIRELESS_FRONTEND_BYPASS_TEST
    WirelessPort_SetRxEn(0U);
    WirelessPort_SetTxEn(1U);
    WirelessPort_SetAntSel(WIRELESS_PORT_ANT1);
    WirelessPort_DelayUs(5U);
#endif
    g_wireless_state.antenna = WIRELESS_ANT1;
    g_wireless_state.ready = 1U;
    g_wireless_state.mode = WIRELESS_MODE_IDLE;
    g_wireless_state.last_error = SUCCESS;
    LOGI(WIRELESS_TAG, "init ok txonly ch=%u ant=%u txen=%u rxen=%u",
         (u16)LT8920_DEFAULT_CHANNEL,
         (u16)g_wireless_state.antenna,
         (u16)WirelessPort_GetTxEn(),
         (u16)WirelessPort_GetRxEn());
    return SUCCESS;
#endif

#if WIRELESS_FRONTEND_BYPASS_TEST
    g_wireless_state.antenna = WIRELESS_ANT1;
    g_wireless_state.scan_has_signal = 1U;
    g_wireless_state.antenna_rssi_ant1 = 0U;
    g_wireless_state.antenna_rssi_ant2 = 0U;
#else
    rc = Wireless_RescanAntenna();
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        g_wireless_state.initialized = 0U;
        g_wireless_state.ready = 0U;
        (void)Wireless_SetIdleMode();
        LOGE(WIRELESS_TAG, "antenna scan fail rc=%d", rc);
        return rc;
    }
#endif

    rc = Wireless_SetRxMode();
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        g_wireless_state.initialized = 0U;
        g_wireless_state.ready = 0U;
        (void)Wireless_SetIdleMode();
        LOGE(WIRELESS_TAG, "enter rx fail rc=%d", rc);
        return rc;
    }

    g_wireless_state.ready = 1U;
    g_wireless_state.last_error = SUCCESS;
    LOGI(WIRELESS_TAG, "init ok ch=%u ant=%u rssi=%u/%u",
         (u16)LT8920_DEFAULT_CHANNEL,
         (u16)g_wireless_state.antenna,
         g_wireless_state.antenna_rssi_ant1,
         g_wireless_state.antenna_rssi_ant2);
    return SUCCESS;
}

s8 Wireless_Deinit(void)
{
    if (!g_wireless_state.initialized) {
        return SUCCESS;
    }

    (void)Wireless_SetIdleMode();
    (void)WirelessPort_Deinit();
    Wireless_ResetQueue();
    Wireless_ResetState();
    LOGI(WIRELESS_TAG, "deinit ok");
    return SUCCESS;
}

s8 Wireless_Poll(void)
{
    static u32 rx_brief_last_log_ms = 0UL;
    static u8 rx_crc_streak = 0U;
    u16 status;
    u8 packet_len;
    u8 packet_buf[LT8920_MAX_PAYLOAD_LEN];
    u32 now_ms;
    s8 rc;

    if (!g_wireless_state.initialized) {
        return WIRELESS_ERR_STATE;
    }
    if (!g_wireless_state.ready) {
        return SUCCESS;
    }

    rc = LT8920_ReadStatus(&status);
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        return rc;
    }
    if ((status & LT8920_STATUS_PKT_FLAG) == 0U) {
        return SUCCESS;
    }

#if WIRELESS_RX_TRACE_ENABLE
    LOGI(WIRELESS_TAG, "rx event st=0x%04X mode=%u", status, (u16)g_wireless_state.mode);
#endif

    if (g_wireless_state.mode == WIRELESS_MODE_TX) {
        g_wireless_state.tx_ok_count++;
        rc = Wireless_SetRxMode();
        if (rc != SUCCESS) {
            g_wireless_state.last_error = rc;
            LOGE(WIRELESS_TAG, "tx->rx fail rc=%d", rc);
            return rc;
        }
        return SUCCESS;
    }

    if (g_wireless_state.mode != WIRELESS_MODE_RX) {
        return SUCCESS;
    }

    if ((status & LT8920_STATUS_CRC_ERROR) != 0U) {
        g_wireless_state.crc_error_count++;
        if (rx_crc_streak < 0xFFU) {
            rx_crc_streak++;
        }
        if ((SHIP_RX_CRC_LOG_THRESHOLD == 0U) ||
            (rx_crc_streak >= SHIP_RX_CRC_LOG_THRESHOLD)) {
            LOGW(WIRELESS_TAG, "rx crc err streak=%u st=0x%04X mode=%u",
                 (u16)rx_crc_streak,
                 status,
                 (u16)g_wireless_state.mode);
            rx_crc_streak = 0U;
        }
        (void)Wireless_SetRxMode();
        return SUCCESS;
    }

    rx_crc_streak = 0U;

    rc = LT8920_ReadPacket(packet_buf, LT8920_MAX_PAYLOAD_LEN, &packet_len);
    if (rc == SUCCESS) {
        now_ms = Task_GetTickMs();
        g_wireless_state.rx_ok_count++;
        if ((rx_brief_last_log_ms == 0UL) ||
            (SHIP_RX_LOG_PERIOD_MS == 0U) ||
            ((now_ms - rx_brief_last_log_ms) >= SHIP_RX_LOG_PERIOD_MS)) {
            rx_brief_last_log_ms = now_ms;
            LOGI(WIRELESS_TAG,
                 "rx c=%u l=%u h=%02X t=%02X",
                 (u16)g_wireless_state.rx_ok_count,
                 (u16)packet_len,
                 (u16)((packet_len > 0U) ? packet_buf[0] : 0U),
                 (u16)((packet_len > 0U) ? packet_buf[packet_len - 1U] : 0U));
        }
#if WIRELESS_RX_TRACE_ENABLE
        LOGI(WIRELESS_TAG,
             "rx pkt len=%u data=%02X %02X %02X %02X %02X %02X %02X %02X",
             (u16)packet_len,
             (u16)((packet_len > 0U) ? packet_buf[0] : 0U),
             (u16)((packet_len > 1U) ? packet_buf[1] : 0U),
             (u16)((packet_len > 2U) ? packet_buf[2] : 0U),
             (u16)((packet_len > 3U) ? packet_buf[3] : 0U),
             (u16)((packet_len > 4U) ? packet_buf[4] : 0U),
             (u16)((packet_len > 5U) ? packet_buf[5] : 0U),
             (u16)((packet_len > 6U) ? packet_buf[6] : 0U),
             (u16)((packet_len > 7U) ? packet_buf[7] : 0U));
#endif
        rc = Wireless_QueuePush(packet_buf, packet_len);
        if (rc != SUCCESS) {
            g_wireless_state.rx_drop_count++;
            LOGW(WIRELESS_TAG, "rx queue drop rc=%d len=%u", rc, (u16)packet_len);
        }
    } else {
        g_wireless_state.rx_drop_count++;
        g_wireless_state.last_error = rc;
        LOGW(WIRELESS_TAG, "read pkt fail rc=%d st=0x%04X", rc, status);
    }

    (void)Wireless_SetRxMode();
    return SUCCESS;
}

static s8 Wireless_SendInternal(const u8 *buf, u8 len, u8 restore_rx)
{
    u16 status;
    u16 timeout_cnt;
    u16 fifo_dbg;
    u8 fifo_loaded;
    s8 rc;

    if ((buf == 0) || (len == 0U) || (len > LT8920_MAX_PAYLOAD_LEN)) {
        return WIRELESS_ERR_PARAM;
    }
    if ((!g_wireless_state.initialized) || (!g_wireless_state.ready)) {
        return WIRELESS_ERR_STATE;
    }

    Wireless_EnableTxFrontend();

    rc = LT8920_StartTxPacket(buf, len);
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        (void)LT8920_EnterIdle();
#if !WIRELESS_FRONTEND_BYPASS_TEST
        WirelessPort_SetTxEn(0U);
#endif
        fifo_loaded = LT8920_GetLastTxFifoCount();
        if (LT8920_ReadReg(52U, &fifo_dbg) != SUCCESS) {
            fifo_dbg = 0xFFFFU;
        }
        LOGE(WIRELESS_TAG, "load tx fifo fail rc=%d fifo_loaded=%u expect=%u reg52=0x%04X",
             rc,
             (u16)fifo_loaded,
             (u16)(len + 1U),
             fifo_dbg);
        return rc;
    }
    g_wireless_state.mode = WIRELESS_MODE_TX;

    fifo_loaded = LT8920_GetLastTxFifoCount();
    if (fifo_loaded != (u8)(len + 1U)) {
        g_wireless_state.last_error = WIRELESS_ERR_VERIFY;
        (void)LT8920_EnterIdle();
#if !WIRELESS_FRONTEND_BYPASS_TEST
        WirelessPort_SetTxEn(0U);
#endif
        if (LT8920_ReadReg(52U, &fifo_dbg) != SUCCESS) {
            fifo_dbg = 0xFFFFU;
        }
        LOGE(WIRELESS_TAG, "tx fifo count mismatch loaded=%u expect=%u reg52=0x%04X",
             (u16)fifo_loaded,
             (u16)(len + 1U),
             fifo_dbg);
        return WIRELESS_ERR_VERIFY;
    }
    WirelessPort_DelayUs(100U);

    for (timeout_cnt = 0U; timeout_cnt < WIRELESS_TX_TIMEOUT_LOOPS; timeout_cnt++) {
        rc = LT8920_ReadStatus(&status);
        if (rc != SUCCESS) {
            break;
        }
        if ((status & LT8920_STATUS_PKT_FLAG) != 0U) {
            g_wireless_state.tx_ok_count++;
            (void)LT8920_EnterIdle();
#if !WIRELESS_FRONTEND_BYPASS_TEST
            WirelessPort_SetTxEn(0U);
#endif
            g_wireless_state.mode = WIRELESS_MODE_IDLE;
#if !WIRELESS_TX_ONLY_TEST
            if (restore_rx != 0U) {
                (void)Wireless_SetRxMode();
            }
#endif
#if WIRELESS_TX_TRACE_ENABLE
            LOGI(WIRELESS_TAG, "tx ok len=%u st=0x%04X", (u16)len, status);
#endif
            return SUCCESS;
        }
        WirelessPort_DelayUs(WIRELESS_TX_PKT_POLL_US);
    }

    (void)Wireless_SetIdleMode();
    (void)LT8920_ClearTxFifo();
#if !WIRELESS_TX_ONLY_TEST
    if (restore_rx != 0U) {
        (void)Wireless_SetRxMode();
    }
#endif
    g_wireless_state.last_error = WIRELESS_ERR_TIMEOUT;
    if (LT8920_ReadStatus(&status) != SUCCESS) {
        status = 0xFFFFU;
    }
    if (LT8920_ReadReg(52U, &fifo_dbg) != SUCCESS) {
        fifo_dbg = 0xFFFFU;
    }
    LOGE(WIRELESS_TAG, "tx timeout len=%u st=0x%04X reg52=0x%04X",
         (u16)len, status, fifo_dbg);
    return WIRELESS_ERR_TIMEOUT;
}

s8 Wireless_Send(const u8 *buf, u8 len)
{
    return Wireless_SendInternal(buf, len, 1U);
}

s8 Wireless_SendOnChannel(u8 channel, const u8 *buf, u8 len)
{
    s8 rc;

    if ((!g_wireless_state.initialized) || (!g_wireless_state.ready)) {
        return WIRELESS_ERR_STATE;
    }

    rc = LT8920_SetChannel(channel);
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        return rc;
    }
    g_wireless_state.mode = WIRELESS_MODE_IDLE;

    return Wireless_SendInternal(buf, len, 0U);
}

s8 Wireless_Receive(u8 *buf, u8 buf_len, u8 *out_len)
{
    return Wireless_QueuePop(buf, buf_len, out_len);
}

s8 Wireless_RunTxDiagBurst(u8 log_detail)
{
#if WIRELESS_CARRIER_WAVE_TEST
    static u8 carrier_started = 0U;
    u16 reg3;
    u16 reg7;
    u16 reg11;
    u16 reg32;
    u16 reg34;
    u16 reg48;
    s8 rc;

    if ((!g_wireless_state.initialized) || (!g_wireless_state.ready)) {
        return Wireless_GetLatchedErrorOrState();
    }

    if (carrier_started != 0U) {
        return SUCCESS;
    }

    WirelessPort_SetRxEn(0U);
    WirelessPort_SetTxEn(1U);
    WirelessPort_DelayUs(10U);

    rc = LT8920_EnterCarrierWave();
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        LOGE(WIRELESS_TAG, "carrier start fail rc=%d", rc);
        return rc;
    }

    g_wireless_state.mode = WIRELESS_MODE_TX;
    carrier_started = 1U;

    (void)LT8920_ReadReg(3U, &reg3);
    (void)LT8920_ReadReg(7U, &reg7);
    (void)LT8920_ReadReg(11U, &reg11);
    (void)LT8920_ReadReg(32U, &reg32);
    (void)LT8920_ReadReg(34U, &reg34);
    (void)LT8920_ReadReg(48U, &reg48);
    LOGI(WIRELESS_TAG,
         "carrier on r3=0x%04X lock=%u r7=0x%04X r11=0x%04X r32=0x%04X r34=0x%04X r48=0x%04X txen=%u rxen=%u",
         reg3,
         (u16)((reg3 & 0x1000U) ? 1U : 0U),
         reg7,
         reg11,
         reg32,
         reg34,
         reg48,
         (u16)WirelessPort_GetTxEn(),
         (u16)WirelessPort_GetRxEn());
    return SUCCESS;
#else
    u8 i;
    u8 payload[LT8920_MAX_PAYLOAD_LEN];
    static u8 seq = 0U;
    u16 reg3_pre;
    u16 reg7_pre;
    u16 reg48_pre;
    u16 reg52_pre;
    u16 reg3_post;
    u16 reg7_post;
    u16 reg48_post;
    u16 reg52_post;
    s8 rc;

    if ((!g_wireless_state.initialized) || (!g_wireless_state.ready)) {
        return Wireless_GetLatchedErrorOrState();
    }

    payload[0] = 0xAAU;
    payload[1] = (u8)(LT8920_MAX_PAYLOAD_LEN - 2U);
    payload[2] = 0x10U;
    payload[3] = SHIP_PAIR_SEED0;
    payload[4] = SHIP_PAIR_SEED1;
    payload[5] = SHIP_PAIR_SEED2;
    payload[6] = SHIP_PAIR_SEED3;
    payload[7] = seq;
    for (i = 8U; i < (u8)(LT8920_MAX_PAYLOAD_LEN - 2U); i++) {
        payload[i] = (u8)(0x20U + seq + i);
    }
    payload[LT8920_MAX_PAYLOAD_LEN - 2U] =
        (u8)(payload[1] ^ payload[2] ^ payload[3] ^ payload[4] ^
             payload[5] ^ payload[6] ^ payload[7]);
    payload[LT8920_MAX_PAYLOAD_LEN - 1U] = 0xBBU;
    seq++;

    (void)LT8920_ReadReg(3U, &reg3_pre);
    (void)LT8920_ReadReg(7U, &reg7_pre);
    (void)LT8920_ReadReg(48U, &reg48_pre);
    (void)LT8920_ReadReg(52U, &reg52_pre);

#if LT8920_FORCE_TX_ORDER_TEST
    rc = LT8920_ForceTxPacket(payload, LT8920_MAX_PAYLOAD_LEN);
    if (rc == SUCCESS) {
        WirelessPort_DelayMs(20U);
    }
#else
    rc = Wireless_Send(payload, LT8920_MAX_PAYLOAD_LEN);
#endif

    (void)LT8920_ReadReg(3U, &reg3_post);
    (void)LT8920_ReadReg(7U, &reg7_post);
    (void)LT8920_ReadReg(48U, &reg48_post);
    (void)LT8920_ReadReg(52U, &reg52_post);

    if ((log_detail != 0U) || (rc != SUCCESS)) {
        LOGI(WIRELESS_TAG,
             "txdiag rc=%d pre r3=0x%04X lock=%u r7=0x%04X r48=0x%04X r52=0x%04X post r3=0x%04X lock=%u r7=0x%04X r48=0x%04X r52=0x%04X txen=%u rxen=%u",
             rc,
             reg3_pre,
             (u16)((reg3_pre & 0x1000U) ? 1U : 0U),
             reg7_pre,
             reg48_pre,
             reg52_pre,
             reg3_post,
             (u16)((reg3_post & 0x1000U) ? 1U : 0U),
             reg7_post,
             reg48_post,
             reg52_post,
             (u16)WirelessPort_GetTxEn(),
             (u16)WirelessPort_GetRxEn());
    }

    return rc;
#endif
}

s8 Wireless_RunPairTxOnlyTest(u8 log_detail)
{
    static u8 pair_tx_ready = 0U;
    static u16 pair_tx_count = 0U;
    u8 frame[9];
    u8 body_len;
    u16 status;
    u16 timeout_cnt;
    u16 reg52;
    s8 rc;

    if ((!g_wireless_state.initialized) || (!g_wireless_state.ready)) {
        return Wireless_GetLatchedErrorOrState();
    }

    body_len = 6U;
    frame[0] = 0xAAU;
    frame[1] = body_len;
    frame[2] = 0x10U;
    frame[3] = SHIP_PAIR_SEED0;
    frame[4] = SHIP_PAIR_SEED1;
    frame[5] = SHIP_PAIR_SEED2;
    frame[6] = SHIP_PAIR_SEED3;
    frame[7] = (u8)(frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5] ^ frame[6]);
    frame[8] = 0xBBU;

    if (pair_tx_ready == 0U) {
#if !WIRELESS_FRONTEND_BYPASS_TEST
        WirelessPort_SetRxEn(0U);
        WirelessPort_SetTxEn(1U);
        WirelessPort_DelayUs(5U);
#endif

        rc = Wireless_SetSyncWord(LT8920_DEFAULT_SYNC_WORD);
        if (rc != SUCCESS) {
            g_wireless_state.last_error = rc;
            LOGE(WIRELESS_TAG, "pair txonly set sync fail rc=%d", rc);
            return rc;
        }

        rc = Wireless_SetChannel((u8)PAIR_CHANNEL);
        if (rc != SUCCESS) {
            g_wireless_state.last_error = rc;
            LOGE(WIRELESS_TAG, "pair txonly set ch fail rc=%d", rc);
            return rc;
        }

        pair_tx_ready = 1U;
        LOGI(WIRELESS_TAG, "pair txonly start ch=0x%02X txen=%u rxen=%u",
             (u16)PAIR_CHANNEL,
             (u16)WirelessPort_GetTxEn(),
             (u16)WirelessPort_GetRxEn());
    }

    rc = LT8920_StartTxPacket(frame, (u8)sizeof(frame));
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        LOGE(WIRELESS_TAG, "pair txonly load fail rc=%d fifo_loaded=%u",
             rc,
             (u16)LT8920_GetLastTxFifoCount());
        return rc;
    }

    g_wireless_state.mode = WIRELESS_MODE_TX;
    WirelessPort_DelayUs(100U);

    for (timeout_cnt = 0U; timeout_cnt < WIRELESS_TX_TIMEOUT_LOOPS; timeout_cnt++) {
        rc = LT8920_ReadStatus(&status);
        if (rc != SUCCESS) {
            g_wireless_state.last_error = rc;
            LOGE(WIRELESS_TAG, "pair txonly status fail rc=%d", rc);
            return rc;
        }
        if ((status & LT8920_STATUS_PKT_FLAG) != 0U) {
            pair_tx_count++;
            (void)LT8920_EnterIdle();
            g_wireless_state.mode = WIRELESS_MODE_IDLE;
            g_wireless_state.tx_ok_count++;
            if ((log_detail != 0U) || (pair_tx_count == 1U)) {
                LOGI(WIRELESS_TAG,
                     "pair txonly ok cnt=%u ch=0x%02X txen=%u rxen=%u data=%02X %02X %02X %02X %02X %02X %02X %02X %02X",
                     pair_tx_count,
                     (u16)PAIR_CHANNEL,
                     (u16)WirelessPort_GetTxEn(),
                     (u16)WirelessPort_GetRxEn(),
                     (u16)frame[0], (u16)frame[1], (u16)frame[2],
                     (u16)frame[3], (u16)frame[4], (u16)frame[5],
                     (u16)frame[6], (u16)frame[7], (u16)frame[8]);
            }
            return SUCCESS;
        }
        WirelessPort_DelayUs(WIRELESS_TX_PKT_POLL_US);
    }

    (void)LT8920_EnterIdle();
    (void)LT8920_ClearTxFifo();
    if (LT8920_ReadReg(52U, &reg52) != SUCCESS) {
        reg52 = 0xFFFFU;
    }
    g_wireless_state.last_error = WIRELESS_ERR_TIMEOUT;
    LOGE(WIRELESS_TAG, "pair txonly timeout st=0x%04X reg52=0x%04X txen=%u rxen=%u",
         status,
         reg52,
         (u16)WirelessPort_GetTxEn(),
         (u16)WirelessPort_GetRxEn());

    return WIRELESS_ERR_TIMEOUT;
}

s8 Wireless_SetAntenna(u8 ant_sel)
{
    if ((ant_sel != WIRELESS_ANT1) && (ant_sel != WIRELESS_ANT2)) {
        return WIRELESS_ERR_PARAM;
    }
    if (!g_wireless_state.initialized) {
        return WIRELESS_ERR_STATE;
    }

#if !WIRELESS_FRONTEND_BYPASS_TEST
    WirelessPort_SetAntSel((ant_sel == WIRELESS_ANT2) ? WIRELESS_PORT_ANT2 : WIRELESS_PORT_ANT1);
#endif
    g_wireless_state.antenna = ant_sel;
    return SUCCESS;
}

s8 Wireless_GetState(Wireless_State_t *state)
{
    u16 i;
    u8 *dst;
    const u8 *src;

    if (state == 0) {
        return WIRELESS_ERR_PARAM;
    }

    dst = (u8 *)state;
    src = (const u8 *)&g_wireless_state;
    for (i = 0U; i < (u16)sizeof(Wireless_State_t); i++) {
        dst[i] = src[i];
    }
    return SUCCESS;
}

s8 Wireless_RescanAntenna(void)
{
    u16 ant1_rssi;
    u16 ant2_rssi;
    u16 ant1_ok;
    u16 ant2_ok;
    u16 ant1_crc;
    u16 ant2_crc;
    s8 rc;
    u8 final_ant;

    if (!g_wireless_state.initialized) {
        return WIRELESS_ERR_STATE;
    }

#if WIRELESS_FRONTEND_BYPASS_TEST
    g_wireless_state.antenna = WIRELESS_ANT1;
    g_wireless_state.scan_has_signal = 1U;
    g_wireless_state.antenna_rssi_ant1 = 0U;
    g_wireless_state.antenna_rssi_ant2 = 0U;
    g_wireless_state.ready = 0U;
    rc = Wireless_SetAntenna(WIRELESS_ANT1);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = Wireless_SetRxMode();
    if (rc != SUCCESS) {
        return rc;
    }
    g_wireless_state.ready = 1U;
    LOGI(WIRELESS_TAG, "frontend bypass: rescan disabled, force ANT1 rx");
    return SUCCESS;
#endif

    g_wireless_state.ready = 0U;
    rc = Wireless_SetIdleMode();
    if (rc != SUCCESS) {
        return rc;
    }

    rc = Wireless_ProbeAntenna(WIRELESS_ANT1, &ant1_rssi, &ant1_ok, &ant1_crc);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = Wireless_ProbeAntenna(WIRELESS_ANT2, &ant2_rssi, &ant2_ok, &ant2_crc);
    if (rc != SUCCESS) {
        return rc;
    }

    g_wireless_state.antenna_rssi_ant1 = ant1_rssi;
    g_wireless_state.antenna_rssi_ant2 = ant2_rssi;

    final_ant = WIRELESS_ANT1;
    if (ant2_rssi > ant1_rssi) {
        final_ant = WIRELESS_ANT2;
    } else if ((ant2_rssi == ant1_rssi) && (ant2_ok > ant1_ok)) {
        final_ant = WIRELESS_ANT2;
    } else if ((ant2_rssi == ant1_rssi) && (ant2_ok == ant1_ok) && (ant2_crc < ant1_crc)) {
        final_ant = WIRELESS_ANT2;
    }

    if ((ant1_ok == 0U) && (ant2_ok == 0U) &&
        (ant1_rssi < WIRELESS_SIGNAL_RSSI_MIN) &&
        (ant2_rssi < WIRELESS_SIGNAL_RSSI_MIN)) {
        g_wireless_state.scan_has_signal = 0U;
        final_ant = WIRELESS_ANT1;
        LOGW(WIRELESS_TAG, "scan no signal, fallback ANT1");
    } else {
        g_wireless_state.scan_has_signal = 1U;
    }

#if (WIRELESS_FORCE_ANT_MODE == WIRELESS_FORCE_ANT_1)
    final_ant = WIRELESS_ANT1;
#elif (WIRELESS_FORCE_ANT_MODE == WIRELESS_FORCE_ANT_2)
    final_ant = WIRELESS_ANT2;
#endif
    rc = Wireless_SetAntenna(final_ant);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = Wireless_SetRxMode();
    if (rc != SUCCESS) {
        return rc;
    }

    g_wireless_state.ready = 1U;
    LOGI(WIRELESS_TAG, "scan ant1 rssi=%u ok=%u crc=%u ant2 rssi=%u ok=%u crc=%u final=%u",
         ant1_rssi, ant1_ok, ant1_crc,
         ant2_rssi, ant2_ok, ant2_crc,
         (u16)final_ant);
    return SUCCESS;
}

s8 Wireless_SearchSignalPoll(void)
{
    static u16 poll_div = 0U;
    static u8 search_retry_count = 0U;
    static u8 initial_scan_counted = 0U;
    s8 rc;

    if (!g_wireless_state.initialized) {
        search_retry_count = 0U;
        initial_scan_counted = 0U;
        return WIRELESS_ERR_STATE;
    }
#if WIRELESS_FRONTEND_BYPASS_TEST
    g_wireless_state.scan_has_signal = 1U;
    search_retry_count = 0U;
    initial_scan_counted = 0U;
    return SUCCESS;
#endif
    if (g_wireless_state.scan_has_signal) {
        search_retry_count = 0U;
        initial_scan_counted = 0U;
        return SUCCESS;
    }

    if (initial_scan_counted == 0U) {
        initial_scan_counted = 1U;
        search_retry_count = 1U;
    }
    if (search_retry_count >= WIRELESS_SEARCH_MAX_RETRY) {
        g_wireless_state.scan_has_signal = 1U;
        (void)Wireless_SetAntenna(WIRELESS_ANT1);
        (void)Wireless_SetRxMode();
        LOGW(WIRELESS_TAG, "search retry limit %u, enter normal rx ant=%u rssi=%u/%u",
             (u16)WIRELESS_SEARCH_MAX_RETRY,
             (u16)g_wireless_state.antenna,
             g_wireless_state.antenna_rssi_ant1,
             g_wireless_state.antenna_rssi_ant2);
        return SUCCESS;
    }

    poll_div++;
    if (poll_div < WIRELESS_SEARCH_POLL_DIV) {
        return SUCCESS;
    }
    poll_div = 0U;

    rc = Wireless_RescanAntenna();
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        LOGE(WIRELESS_TAG, "search rescan fail rc=%d", rc);
        return rc;
    }
    if (g_wireless_state.scan_has_signal) {
        search_retry_count = 0U;
        initial_scan_counted = 0U;
        LOGI(WIRELESS_TAG, "signal detected ant=%u rssi=%u/%u",
             (u16)g_wireless_state.antenna,
             g_wireless_state.antenna_rssi_ant1,
             g_wireless_state.antenna_rssi_ant2);
    } else {
        search_retry_count++;
        if (search_retry_count >= WIRELESS_SEARCH_MAX_RETRY) {
            g_wireless_state.scan_has_signal = 1U;
            (void)Wireless_SetAntenna(WIRELESS_ANT1);
            (void)Wireless_SetRxMode();
            LOGW(WIRELESS_TAG, "search retry limit %u, enter normal rx ant=%u rssi=%u/%u",
                 (u16)WIRELESS_SEARCH_MAX_RETRY,
                 (u16)g_wireless_state.antenna,
                 g_wireless_state.antenna_rssi_ant1,
                 g_wireless_state.antenna_rssi_ant2);
        }
    }
    return SUCCESS;
}

s8 Wireless_RunMinimalTest(void)
{
    u16 reg3;
    u16 reg6;
    u16 reg11;
    u16 reg36;
    u16 reg37;
    u16 reg38;
    u16 reg39;
    u16 reg41;
    u16 st_after_tx;
    u16 fifo_after_tx;
    u8 tx_probe[4];
    s8 rc;

    if ((!g_wireless_state.initialized) || (!g_wireless_state.ready)) {
        return Wireless_GetLatchedErrorOrState();
    }

    rc = LT8920_ReadReg(3U, &reg3);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test read reg3 fail rc=%d", rc);
        return rc;
    }

    rc = LT8920_ReadReg(6U, &reg6);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test read reg6 fail rc=%d", rc);
        return rc;
    }

    rc = LT8920_ReadReg(11U, &reg11);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test read reg11 fail rc=%d", rc);
        return rc;
    }

    rc = LT8920_ReadReg(41U, &reg41);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test read reg41 fail rc=%d", rc);
        return rc;
    }

    if (reg11 != 0x0008U) {
        LOGE(WIRELESS_TAG, "test verify reg11 fail val=0x%04X", reg11);
        return WIRELESS_ERR_VERIFY;
    }
    if (reg41 != 0xB000U) {
        LOGE(WIRELESS_TAG, "test verify reg41 fail val=0x%04X", reg41);
        return WIRELESS_ERR_VERIFY;
    }
    /* Reg7 mixes mode-control bits with channel-related fields, so
     * do not treat it as the only source of truth for SPI writeback.
     */
    rc = LT8920_SetChannel(0x12U);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test set ch fail rc=%d", rc);
        return rc;
    }

    rc = LT8920_SetSyncRegs(0x1357U, 0x2468U);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test set sync regs fail rc=%d", rc);
        return rc;
    }
    rc = LT8920_ReadReg(36U, &reg36);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test read reg36 fail rc=%d", rc);
        return rc;
    }
    rc = LT8920_ReadReg(37U, &reg37);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test read reg37 fail rc=%d", rc);
        return rc;
    }
    rc = LT8920_ReadReg(38U, &reg38);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test read reg38 fail rc=%d", rc);
        return rc;
    }
    rc = LT8920_ReadReg(39U, &reg39);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test read reg39 fail rc=%d", rc);
        return rc;
    }
    if ((reg36 != 0x1357U) || (reg37 != 0x0380U) ||
        (reg38 != 0x5A5AU) || (reg39 != 0x2468U)) {
        LOGE(WIRELESS_TAG,
             "test verify sync fail r36=0x%04X r37=0x%04X r38=0x%04X r39=0x%04X",
             reg36, reg37, reg38, reg39);
        return WIRELESS_ERR_VERIFY;
    }

    rc = Wireless_SetSyncWord(LT8920_DEFAULT_SYNC_WORD);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test restore pair sync fail rc=%d", rc);
        return rc;
    }
    rc = Wireless_SetChannel(LT8920_DEFAULT_CHANNEL);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "test restore ch fail rc=%d", rc);
        return rc;
    }

#if WIRELESS_MINIMAL_TEST_ONLY
    LOGI(WIRELESS_TAG, "test ok, skip short tx probe in wireless minimal mode");
    return SUCCESS;
#endif

    tx_probe[0] = 0xA5U;
    tx_probe[1] = 0x5AU;
    tx_probe[2] = 0xC3U;
    tx_probe[3] = 0x3CU;
    LOGI(WIRELESS_TAG, "tx probe start ch=%u ant=%u txen=%u rxen=%u data=%02X %02X %02X %02X",
         (u16)LT8920_DEFAULT_CHANNEL,
         (u16)WirelessPort_GetAntSel(),
         (u16)WirelessPort_GetTxEn(),
         (u16)WirelessPort_GetRxEn(),
         (u16)tx_probe[0], (u16)tx_probe[1], (u16)tx_probe[2], (u16)tx_probe[3]);
    rc = Wireless_Send(tx_probe, 4U);
    if (rc != SUCCESS) {
        LOGE(WIRELESS_TAG, "tx probe fail rc=%d", rc);
        return rc;
    }
    if (LT8920_ReadStatus(&st_after_tx) != SUCCESS) {
        st_after_tx = 0xFFFFU;
    }
    if (LT8920_ReadReg(52U, &fifo_after_tx) != SUCCESS) {
        fifo_after_tx = 0xFFFFU;
    }
    LOGI(WIRELESS_TAG, "tx probe ok st=0x%04X reg52=0x%04X ant=%u txen=%u rxen=%u",
         st_after_tx,
         fifo_after_tx,
         (u16)WirelessPort_GetAntSel(),
         (u16)WirelessPort_GetTxEn(),
         (u16)WirelessPort_GetRxEn());

    LOGI(WIRELESS_TAG, "test ok");
    return SUCCESS;
}

s8 Wireless_SetChannel(u8 channel)
{
    s8 rc;
    u16 reg7;

    if (!g_wireless_state.initialized) {
        return WIRELESS_ERR_STATE;
    }

    rc = Wireless_SetRxModeOnChannel(channel);
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        return rc;
    }

    if ((SHIP_PROTO_DEBUG_ENABLE != 0U) &&
        (LT8920_ReadReg(7U, &reg7) == SUCCESS)) {
        LOGI(WIRELESS_TAG, "set ch target=%u reg7=0x%04X actual=%u",
             (u16)channel,
             reg7,
             (u16)(reg7 & 0x007FU));
    }

    return SUCCESS;
}

s8 Wireless_SetSyncWord(u32 sync_word)
{
    s8 rc;

    if (!g_wireless_state.initialized) {
        return WIRELESS_ERR_STATE;
    }

    rc = LT8920_SetSyncWord(sync_word);
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        return rc;
    }

    rc = Wireless_SetRxMode();
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        return rc;
    }

    return SUCCESS;
}

s8 Wireless_SetSyncRegs(u16 reg36, u16 reg39)
{
    s8 rc;

    if (!g_wireless_state.initialized) {
        return WIRELESS_ERR_STATE;
    }

    rc = LT8920_SetSyncRegs(reg36, reg39);
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        return rc;
    }

    rc = Wireless_SetRxMode();
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        return rc;
    }

    return SUCCESS;
}

s8 Wireless_SetSyncRegsIdle(u16 reg36, u16 reg39)
{
    s8 rc;

    if (!g_wireless_state.initialized) {
        return WIRELESS_ERR_STATE;
    }

    rc = LT8920_SetSyncRegs(reg36, reg39);
    if (rc != SUCCESS) {
        g_wireless_state.last_error = rc;
        return rc;
    }

    g_wireless_state.mode = WIRELESS_MODE_IDLE;
    return SUCCESS;
}

s8 Wireless_GetRxDebug(Wireless_RxDebug_t *dbg)
{
    s8 rc;
    u16 reg7;
    u16 reg8;
    u16 reg36;
    u16 reg37;
    u16 reg38;
    u16 reg39;
    u16 reg48;
    u16 reg52;
    u8 rssi;

    if (dbg == 0) {
        return WIRELESS_ERR_PARAM;
    }
    if (!g_wireless_state.initialized) {
        return WIRELESS_ERR_STATE;
    }

    rc = LT8920_ReadReg(7U, &reg7);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_ReadReg(8U, &reg8);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_ReadReg(36U, &reg36);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_ReadReg(37U, &reg37);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_ReadReg(38U, &reg38);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_ReadReg(39U, &reg39);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_ReadStatus(&reg48);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_ReadReg(52U, &reg52);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_ReadRawRssi(&rssi);
    if (rc != SUCCESS) {
        return rc;
    }

    dbg->reg7 = reg7;
    dbg->reg8 = reg8;
    dbg->reg36 = reg36;
    dbg->reg37 = reg37;
    dbg->reg38 = reg38;
    dbg->reg39 = reg39;
    dbg->reg48 = reg48;
    dbg->reg52 = reg52;
    dbg->rssi = rssi;
    dbg->rx_en = WirelessPort_GetRxEn();
    dbg->tx_en = WirelessPort_GetTxEn();
    dbg->mode = g_wireless_state.mode;
    dbg->rx_mode_bit = (u8)(((reg7 & 0x0080U) != 0U) ? 1U : 0U);
    dbg->channel = (u8)(reg7 & 0x007FU);
    return SUCCESS;
}
