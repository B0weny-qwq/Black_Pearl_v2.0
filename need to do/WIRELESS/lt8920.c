/**
 * @file    lt8920.c
 * @brief   LT8920 2.4GHz 无线收发芯片寄存器与 FIFO 驱动实现。
 * @author  boweny
 * @date    2026-05-06
 * @version v1.1
 *
 * @details
 * 本文件负责 LT8920 默认寄存器表加载、寄存器读写、FIFO 读写、
 * TX/RX 模式切换和调试状态读取。当前寄存器配置和收发入口顺序按
 * `Wireless_other/LT8920/LT8920_SPI.c` 对齐，用于旧遥控器业务移植。
 *
 * @note
 * 本层不解析业务协议，不决定配对状态；业务调度由 `ship_protocol.c`
 * 完成。修改寄存器顺序前必须先对照 `Wireless_other`。
 */
#include "lt8920.h"

#include "wireless.h"
#include "wireless_port.h"

typedef struct
{
    u8 reg;
    u8 high;
    u8 low;
    u16 verify_mask;
} LT8920_RegInit_t;

static const LT8920_RegInit_t g_lt8920_default_regs[] =
{
    {0U,  0x6FU, 0xE0U, 0xFFFFU},
    {1U,  0x56U, 0x81U, 0xFFFFU},
    {2U,  0x66U, 0x17U, 0xFFFFU},
    {4U,  0x9CU, 0xC9U, 0xFFFFU},
    {5U,  0x66U, 0x37U, 0xFFFFU},
    {7U,  0x00U, 0x30U, 0xFFFFU},
    {8U,  0x6CU, 0x90U, 0xFFFFU},
    {9U,  0x48U, 0x00U, 0xFFFFU},
    {10U, 0x7FU, 0xFDU, 0xFFFFU},
    {11U, 0x00U, 0x08U, 0xFFFFU},
    {12U, 0x00U, 0x00U, 0xFFFFU},
    {13U, 0x48U, 0xBDU, 0xFFFFU},
    {22U, 0x00U, 0xFFU, 0xFFFFU},
    {23U, 0x80U, 0x05U, 0xFFFFU},
    {24U, 0x00U, 0x67U, 0xFFFFU},
    {25U, 0x16U, 0x59U, 0xFFFFU},
    {26U, 0x19U, 0xE0U, 0xFFFFU},
    {27U, 0x13U, 0x00U, 0xFFFFU},
    {28U, 0x18U, 0x00U, 0xFFFFU},
    {32U, 0x48U, 0x00U, 0xFFFFU},
    {33U, 0x3FU, 0xC7U, 0xFFFFU},
    {34U, 0x20U, 0x00U, 0xFFFFU},
    {35U, 0x00U, 0x00U, 0xFFFFU},
    {36U, 0x03U, 0x80U, 0xFFFFU},
    {37U, 0x03U, 0x80U, 0xFFFFU},
    {38U, 0x5AU, 0x5AU, 0xFFFFU},
    {39U, 0x03U, 0x80U, 0xFFFFU},
    {40U, 0x44U, 0x02U, 0xFFFFU},
    {41U, 0xB0U, 0x00U, 0xFFFFU},
    {42U, 0xFDU, 0xB0U, 0xFFFFU},
    {43U, 0x00U, 0x0FU, 0xFFFFU},
    {44U, 0x10U, 0x00U, 0xFF00U},
    {45U, 0x05U, 0x52U, 0xFFFFU},
    {50U, 0x00U, 0x00U, 0x0000U}
};

static u8 g_lt8920_initialized = 0U;
static u8 g_lt8920_channel = LT8920_DEFAULT_CHANNEL;
static u32 g_lt8920_sync_word = LT8920_DEFAULT_SYNC_WORD;
static u8 g_lt8920_verify_fail_reg = 0xFFU;
static u16 g_lt8920_verify_fail_expected = 0U;
static u16 g_lt8920_verify_fail_actual = 0U;
static u8 g_lt8920_last_tx_fifo_count = 0U;

static void LT8920_ClearVerifyFailure(void)
{
    g_lt8920_verify_fail_reg = 0xFFU;
    g_lt8920_verify_fail_expected = 0U;
    g_lt8920_verify_fail_actual = 0U;
}

static void LT8920_SetVerifyFailure(u8 reg, u16 expected, u16 actual)
{
    g_lt8920_verify_fail_reg = reg;
    g_lt8920_verify_fail_expected = expected;
    g_lt8920_verify_fail_actual = actual;
}

static u8 LT8920_BuildCommand(u8 reg, u8 read)
{
    return (u8)((read ? 0x80U : 0x00U) | (reg & 0x7FU));
}

static s8 LT8920_WriteRegBytes(u8 reg, u8 high, u8 low)
{
    WirelessPort_SetCs(0U);
    (void)WirelessPort_SpiTransfer(LT8920_BuildCommand(reg, 0U));
    (void)WirelessPort_SpiTransfer(high);
    (void)WirelessPort_SpiTransfer(low);
    WirelessPort_SetCs(1U);
    return SUCCESS;
}

static s8 LT8920_WriteReg(u8 reg, u16 value)
{
    return LT8920_WriteRegBytes(reg, (u8)(value >> 8), (u8)(value & 0x00FFU));
}

static s8 LT8920_WriteFifoBytes(const u8 *buf, u8 len)
{
    u8 i;

    if ((buf == 0) || (len == 0U)) {
        return WIRELESS_ERR_PARAM;
    }

    WirelessPort_SetCs(0U);
    (void)WirelessPort_SpiTransfer(LT8920_BuildCommand(50U, 0U));
#if LT8920_FIFO_DELAY_TEST
    WirelessPort_DelayUs(1U);
#endif
    for (i = 0U; i < len; i++) {
        (void)WirelessPort_SpiTransfer(buf[i]);
#if LT8920_FIFO_DELAY_TEST
        WirelessPort_DelayUs(1U);
#endif
    }
    WirelessPort_SetCs(1U);
#if LT8920_FIFO_DELAY_TEST
    WirelessPort_DelayUs(1U);
#endif
    return SUCCESS;
}

static s8 LT8920_ReadTxFifoCount(u8 *count)
{
    u16 reg52;
    s8 rc;

    if (count == 0) {
        return WIRELESS_ERR_PARAM;
    }

    rc = LT8920_ReadReg(52U, &reg52);
    if (rc != SUCCESS) {
        return rc;
    }

    *count = (u8)((reg52 >> 8) & 0x003FU);
    return SUCCESS;
}

static s8 LT8920_UpdateModeRegister(u16 mode_flags)
{
    return LT8920_WriteReg(7U, (u16)(mode_flags | (u16)(g_lt8920_channel & 0x7FU)));
}

static s8 LT8920_ResetRxPath(void)
{
    s8 rc;

    rc = LT8920_EnterIdle();
    if (rc != SUCCESS) {
        return rc;
    }
    return LT8920_ClearRxFifo();
}

static s8 LT8920_LoadDefaultProfile(void)
{
    u8 i;
    s8 rc;

    for (i = 0U; i < (u8)(sizeof(g_lt8920_default_regs) / sizeof(g_lt8920_default_regs[0])); i++) {
        rc = LT8920_WriteRegBytes(g_lt8920_default_regs[i].reg,
                                  g_lt8920_default_regs[i].high,
                                  g_lt8920_default_regs[i].low);
        if (rc != SUCCESS) {
            return rc;
        }
    }

    return SUCCESS;
}

static s8 LT8920_VerifyDefaultProfile(void)
{
    u8 i;
    u16 value;
    u16 expected;
    u16 mask;
    s8 rc;

    LT8920_ClearVerifyFailure();

    for (i = 0U; i < (u8)(sizeof(g_lt8920_default_regs) / sizeof(g_lt8920_default_regs[0])); i++) {
        expected = (u16)(((u16)g_lt8920_default_regs[i].high << 8) |
                         g_lt8920_default_regs[i].low);
        mask = g_lt8920_default_regs[i].verify_mask;
        if (mask == 0U) {
            continue;
        }
        rc = LT8920_ReadReg(g_lt8920_default_regs[i].reg, &value);
        if (rc != SUCCESS) {
            LT8920_SetVerifyFailure(g_lt8920_default_regs[i].reg, expected, 0xFFFFU);
            return rc;
        }
        if ((value & mask) != (expected & mask)) {
            LT8920_SetVerifyFailure(g_lt8920_default_regs[i].reg, expected, value);
            return WIRELESS_ERR_VERIFY;
        }
    }

    return SUCCESS;
}

s8 LT8920_ReadReg(u8 reg, u16 *value)
{
    u8 msb;
    u8 lsb;

    if (value == 0) {
        return WIRELESS_ERR_PARAM;
    }

    WirelessPort_SetCs(0U);
    (void)WirelessPort_SpiTransfer(LT8920_BuildCommand(reg, 1U));
    msb = WirelessPort_SpiTransfer(0x00U);
    lsb = WirelessPort_SpiTransfer(0x00U);
    WirelessPort_SetCs(1U);

    *value = (u16)(((u16)msb << 8) | lsb);
    return SUCCESS;
}

s8 LT8920_Init(u8 channel, u32 sync_word)
{
    s8 rc;

    if (channel > 0x7FU) {
        return WIRELESS_ERR_PARAM;
    }

    LT8920_ClearVerifyFailure();

    rc = LT8920_LoadDefaultProfile();
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_VerifyDefaultProfile();
    if (rc != SUCCESS) {
        return rc;
    }

    g_lt8920_channel = channel;
    g_lt8920_sync_word = sync_word;

    rc = LT8920_SetSyncWord(sync_word);
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_SetChannel(channel);
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_WriteReg(8U, 0x6C90U);
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_ClearTxFifo();
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_ClearRxFifo();
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_EnterIdle();
    if (rc != SUCCESS) {
        return rc;
    }

    g_lt8920_initialized = 1U;
    return SUCCESS;
}

s8 LT8920_SetChannel(u8 channel)
{
    g_lt8920_channel = (u8)(channel & 0x7FU);
    return LT8920_EnterIdle();
}

s8 LT8920_SetSyncWord(u32 sync_word)
{
    s8 rc;

    g_lt8920_sync_word = sync_word;

    rc = LT8920_EnterIdle();
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_WriteReg(36U, (u16)(sync_word & 0xFFFFUL));
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_WriteReg(37U, 0x0380U);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_WriteReg(38U, 0x5A5AU);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = LT8920_WriteReg(39U, (u16)(sync_word >> 16));
    if (rc != SUCCESS) {
        return rc;
    }

    return LT8920_ClearRxFifo();
}

s8 LT8920_SetSyncRegs(u16 reg36, u16 reg39)
{
    s8 rc;

    rc = LT8920_EnterIdle();
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_WriteReg(36U, reg36);
    if (rc != SUCCESS) {
        return rc;
    }

    /* 旧版 RF_Encrypt_Config() 只重写 reg36/reg39。
     * reg37/reg38 必须保持配对默认值，才能兼容不可修改的遥控器。
     */
    rc = LT8920_WriteReg(39U, reg39);
    if (rc != SUCCESS) {
        return rc;
    }

    return SUCCESS;
}

s8 LT8920_EnterIdle(void)
{
    return LT8920_UpdateModeRegister(0x0000U);
}

s8 LT8920_EnterRx(void)
{
    s8 rc;

    rc = LT8920_WriteReg(8U, 0x6C90U);
    if (rc != SUCCESS) {
        return rc;
    }
    return LT8920_UpdateModeRegister(0x0080U);
}

s8 LT8920_EnterTx(void)
{
    s8 rc;

    rc = LT8920_WriteReg(8U, 0x6C90U);
    if (rc != SUCCESS) {
        return rc;
    }
    return LT8920_UpdateModeRegister(0x0100U);
}

s8 LT8920_OpenRx(void)
{
    s8 rc;

    rc = LT8920_EnterIdle();
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_ClearRxFifo();
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_WriteReg(8U, 0x6C90U);
    if (rc != SUCCESS) {
        return rc;
    }

    return LT8920_UpdateModeRegister(0x0080U);
}

s8 LT8920_OpenRxOnChannel(u8 channel)
{
    if (channel > 0x7FU) {
        return WIRELESS_ERR_PARAM;
    }

    g_lt8920_channel = (u8)(channel & 0x7FU);
    return LT8920_OpenRx();
}

s8 LT8920_EnterCarrierWave(void)
{
    s8 rc;

    rc = LT8920_EnterIdle();
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_WriteReg(32U, 0x1807U);
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_WriteReg(34U, 0x830BU);
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_WriteReg(11U, 0x8008U);
    if (rc != SUCCESS) {
        return rc;
    }

    return LT8920_UpdateModeRegister(0x0100U);
}

s8 LT8920_ReadStatus(u16 *status)
{
    return LT8920_ReadReg(48U, status);
}

s8 LT8920_ReadRawRssi(u8 *rssi)
{
    u16 reg6;
    s8 rc;

    if (rssi == 0) {
        return WIRELESS_ERR_PARAM;
    }

    rc = LT8920_ReadReg(6U, &reg6);
    if (rc != SUCCESS) {
        return rc;
    }

    *rssi = (u8)((reg6 >> 10) & 0x003FU);
    return SUCCESS;
}

s8 LT8920_ClearTxFifo(void)
{
    return LT8920_WriteReg(52U, 0x8080U);
}

s8 LT8920_ClearRxFifo(void)
{
    return LT8920_WriteReg(52U, 0x8080U);
}

s8 LT8920_WritePacket(const u8 *buf, u8 len)
{
    u8 packet_buf[LT8920_MAX_PAYLOAD_LEN + 1U];
    u8 i;
    u8 fifo_count;
    s8 rc;

    if ((buf == 0) || (len == 0U) || (len > LT8920_MAX_PAYLOAD_LEN)) {
        return WIRELESS_ERR_PARAM;
    }
    if (!g_lt8920_initialized) {
        return WIRELESS_ERR_STATE;
    }

    rc = LT8920_EnterIdle();
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_ClearTxFifo();
    if (rc != SUCCESS) {
        return rc;
    }

    packet_buf[0] = len;
    for (i = 0U; i < len; i++) {
        packet_buf[i + 1U] = buf[i];
    }

    rc = LT8920_WriteFifoBytes(packet_buf, (u8)(len + 1U));
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_ReadTxFifoCount(&fifo_count);
    if (rc != SUCCESS) {
        return rc;
    }
    g_lt8920_last_tx_fifo_count = fifo_count;
    if (fifo_count != (u8)(len + 1U)) {
        return WIRELESS_ERR_VERIFY;
    }

    return SUCCESS;
}

s8 LT8920_StartTxPacket(const u8 *buf, u8 len)
{
    s8 rc;

    rc = LT8920_WritePacket(buf, len);
    if (rc != SUCCESS) {
        return rc;
    }

    return LT8920_EnterTx();
}

s8 LT8920_ForceTxPacket(const u8 *buf, u8 len)
{
    u8 packet_buf[LT8920_MAX_PAYLOAD_LEN + 1U];
    u8 i;
    u8 fifo_count;
    s8 rc;

    if ((buf == 0) || (len == 0U) || (len > LT8920_MAX_PAYLOAD_LEN)) {
        return WIRELESS_ERR_PARAM;
    }
    if (!g_lt8920_initialized) {
        return WIRELESS_ERR_STATE;
    }

    rc = LT8920_EnterIdle();
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_ClearTxFifo();
    if (rc != SUCCESS) {
        return rc;
    }

    packet_buf[0] = len;
    for (i = 0U; i < len; i++) {
        packet_buf[i + 1U] = buf[i];
    }
    rc = LT8920_WriteFifoBytes(packet_buf, (u8)(len + 1U));
    if (rc != SUCCESS) {
        return rc;
    }

    rc = LT8920_ReadTxFifoCount(&fifo_count);
    if (rc != SUCCESS) {
        return rc;
    }
    g_lt8920_last_tx_fifo_count = fifo_count;
    if (fifo_count != (u8)(len + 1U)) {
        return WIRELESS_ERR_VERIFY;
    }

    rc = LT8920_EnterTx();
    if (rc != SUCCESS) {
        return rc;
    }
    WirelessPort_DelayUs(20U);

    return SUCCESS;
}

s8 LT8920_ReadPacket(u8 *buf, u8 buf_len, u8 *out_len)
{
    u16 status;
    u8 packet_len;
    u8 read_len;
    u8 value;
    u8 i;
    s8 rc;

    if ((buf == 0) || (out_len == 0) || (buf_len == 0U)) {
        return WIRELESS_ERR_PARAM;
    }
    if (!g_lt8920_initialized) {
        return WIRELESS_ERR_STATE;
    }

    rc = LT8920_ReadStatus(&status);
    if (rc != SUCCESS) {
        return rc;
    }
    if ((status & LT8920_STATUS_PKT_FLAG) == 0U) {
        return WIRELESS_ERR_EMPTY;
    }
    if ((status & LT8920_STATUS_CRC_ERROR) != 0U) {
        (void)LT8920_ResetRxPath();
        return WIRELESS_ERR_VERIFY;
    }

    WirelessPort_SetCs(0U);
    (void)WirelessPort_SpiTransfer(LT8920_BuildCommand(50U, 1U));
    packet_len = WirelessPort_SpiTransfer(0x00U);
    read_len = packet_len;
    if (read_len > 64U) {
        read_len = 64U;
    }

    if (packet_len == 0U) {
        WirelessPort_SetCs(1U);
        (void)LT8920_ResetRxPath();
        return WIRELESS_ERR_IO;
    }

    rc = SUCCESS;
    for (i = 0U; i < read_len; i++) {
        value = WirelessPort_SpiTransfer(0x00U);
        if ((packet_len <= LT8920_MAX_PAYLOAD_LEN) && (packet_len <= buf_len)) {
            buf[i] = value;
        } else {
            rc = WIRELESS_ERR_OVERFLOW;
        }
    }
    WirelessPort_SetCs(1U);

    if (rc != SUCCESS) {
        (void)LT8920_ResetRxPath();
        return rc;
    }

    *out_len = packet_len;
    return SUCCESS;
}

void LT8920_GetVerifyFailure(u8 *reg, u16 *expected, u16 *actual)
{
    if (reg != 0) {
        *reg = g_lt8920_verify_fail_reg;
    }
    if (expected != 0) {
        *expected = g_lt8920_verify_fail_expected;
    }
    if (actual != 0) {
        *actual = g_lt8920_verify_fail_actual;
    }
}

u8 LT8920_GetLastTxFifoCount(void)
{
    return g_lt8920_last_tx_fifo_count;
}
