#include "LT8920.h"

/*
 * LT8920 默认寄存器表，迁移自旧无线初始化流程。
 * verify_mask=0 表示该项不做回读校验。
 */
static const lt8920_reg_setting_t EF_CODE_CONST lt8920_default_regs[] =
{
    {0U,  0x6FE0U, 0xFFFFU},
    {1U,  0x5681U, 0xFFFFU},
    {2U,  0x6617U, 0xFFFFU},
    {4U,  0x9CC9U, 0xFFFFU},
    {5U,  0x6637U, 0xFFFFU},
    {7U,  0x0030U, 0xFFFFU},
    {8U,  0x6C90U, 0xFFFFU},
    {9U,  0x4800U, 0xFFFFU},
    {10U, 0x7FFDU, 0xFFFFU},
    {11U, 0x0008U, 0xFFFFU},
    {12U, 0x0000U, 0xFFFFU},
    {13U, 0x48BDU, 0xFFFFU},
    {22U, 0x00FFU, 0xFFFFU},
    {23U, 0x8005U, 0xFFFFU},
    {24U, 0x0067U, 0xFFFFU},
    {25U, 0x1659U, 0xFFFFU},
    {26U, 0x19E0U, 0xFFFFU},
    {27U, 0x1300U, 0xFFFFU},
    {28U, 0x1800U, 0xFFFFU},
    {32U, 0x4800U, 0xFFFFU},
    {33U, 0x3FC7U, 0xFFFFU},
    {34U, 0x2000U, 0xFFFFU},
    {35U, 0x0000U, 0xFFFFU},
    {36U, 0x0380U, 0xFFFFU},
    {37U, 0x0380U, 0xFFFFU},
    {38U, 0x5A5AU, 0xFFFFU},
    {39U, 0x0380U, 0xFFFFU},
    {40U, 0x4402U, 0xFFFFU},
    {41U, 0xB000U, 0xFFFFU},
    {42U, 0xFDB0U, 0xFFFFU},
    {43U, 0x000FU, 0xFFFFU},
    {44U, 0x1000U, 0xFF00U},
    {45U, 0x0552U, 0xFFFFU},
    {50U, 0x0000U, 0x0000U}
};

static int8 LT8920_CheckDevice(lt8920_t *dev)
{
    if ((dev == 0) ||
        (dev->bus.spi_transfer == 0) ||
        (dev->bus.set_cs == 0)) {
        return LT8920_ERR_PARAM;
    }

    return LT8920_OK;
}

static u8 LT8920_BuildCommand(u8 reg, u8 read)
{
    /* 命令格式：bit7=读标志，bit6..0=寄存器地址。 */
    return (u8)((read != 0U ? 0x80U : 0x00U) | (reg & 0x7FU));
}

static void LT8920_SetCs(lt8920_t *dev, u8 level)
{
    dev->bus.set_cs(dev->bus.ctx, level);
}

static u8 LT8920_SpiTransfer(lt8920_t *dev, u8 value)
{
    return dev->bus.spi_transfer(dev->bus.ctx, value);
}

static void LT8920_DelayUs(lt8920_t *dev, u16 us)
{
    if ((dev != 0) && (dev->bus.delay_us != 0)) {
        dev->bus.delay_us(dev->bus.ctx, us);
    }
}

static int8 LT8920_UpdateModeRegister(lt8920_t *dev, u16 mode_flags)
{
    /* 寄存器 7 同时包含模式位和当前 7 位射频信道。 */
    return LT8920_WriteReg(dev,
                           LT8920_REG_CHANNEL_MODE,
                           (u16)(mode_flags | (u16)(dev->channel & 0x7FU)));
}

static void LT8920_ClearVerifyFailure(lt8920_t *dev)
{
    dev->verify_fail_reg = 0xFFU;
    dev->verify_fail_expected = 0U;
    dev->verify_fail_actual = 0U;
}

static void LT8920_SetVerifyFailure(lt8920_t *dev, u8 reg, u16 expected, u16 actual)
{
    dev->verify_fail_reg = reg;
    dev->verify_fail_expected = expected;
    dev->verify_fail_actual = actual;
}

static int8 LT8920_ResetRxPath(lt8920_t *dev)
{
    int8 ret;

    ret = LT8920_EnterIdle(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    return LT8920_ClearRxFifo(dev);
}

int8 LT8920_Bind(lt8920_t *dev, const lt8920_bus_t *bus)
{
    if ((dev == 0) ||
        (bus == 0) ||
        (bus->spi_transfer == 0) ||
        (bus->set_cs == 0)) {
        return LT8920_ERR_PARAM;
    }

    dev->bus = *bus;
    dev->initialized = 0U;
    dev->channel = LT8920_DEFAULT_CHANNEL;
    dev->sync_word = LT8920_DEFAULT_SYNC_WORD;
    dev->last_tx_fifo_count = 0U;
    LT8920_ClearVerifyFailure(dev);
    LT8920_SetCs(dev, 1U);
    return LT8920_OK;
}

int8 LT8920_WriteReg(lt8920_t *dev, u8 reg, u16 value)
{
    int8 ret;

    ret = LT8920_CheckDevice(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    /* LT8920 寄存器访问顺序：命令字节，随后高字节、低字节。 */
    LT8920_SetCs(dev, 0U);
    (void)LT8920_SpiTransfer(dev, LT8920_BuildCommand(reg, 0U));
    (void)LT8920_SpiTransfer(dev, (u8)(value >> 8));
    (void)LT8920_SpiTransfer(dev, (u8)(value & 0x00FFU));
    LT8920_SetCs(dev, 1U);

    return LT8920_OK;
}

int8 LT8920_ReadReg(lt8920_t *dev, u8 reg, u16 *value)
{
    int8 ret;
    u8 msb;
    u8 lsb;

    ret = LT8920_CheckDevice(dev);
    if (ret != LT8920_OK) {
        return ret;
    }
    if (value == 0) {
        return LT8920_ERR_PARAM;
    }

    LT8920_SetCs(dev, 0U);
    (void)LT8920_SpiTransfer(dev, LT8920_BuildCommand(reg, 1U));
    msb = LT8920_SpiTransfer(dev, 0x00U);
    lsb = LT8920_SpiTransfer(dev, 0x00U);
    LT8920_SetCs(dev, 1U);

    *value = (u16)(((u16)msb << 8) | lsb);
    return LT8920_OK;
}

int8 LT8920_WriteFifo(lt8920_t *dev, const u8 *buf, u8 len)
{
    int8 ret;
    u8 i;

    ret = LT8920_CheckDevice(dev);
    if (ret != LT8920_OK) {
        return ret;
    }
    if ((buf == 0) || (len == 0U)) {
        return LT8920_ERR_PARAM;
    }

    LT8920_SetCs(dev, 0U);
    (void)LT8920_SpiTransfer(dev, LT8920_BuildCommand(LT8920_REG_FIFO, 0U));
    for (i = 0U; i < len; i++) {
        (void)LT8920_SpiTransfer(dev, buf[i]);
    }
    LT8920_SetCs(dev, 1U);

    return LT8920_OK;
}

int8 LT8920_ReadFifo(lt8920_t *dev, u8 *buf, u8 len)
{
    int8 ret;
    u8 i;

    ret = LT8920_CheckDevice(dev);
    if (ret != LT8920_OK) {
        return ret;
    }
    if ((buf == 0) || (len == 0U)) {
        return LT8920_ERR_PARAM;
    }

    LT8920_SetCs(dev, 0U);
    (void)LT8920_SpiTransfer(dev, LT8920_BuildCommand(LT8920_REG_FIFO, 1U));
    for (i = 0U; i < len; i++) {
        buf[i] = LT8920_SpiTransfer(dev, 0x00U);
    }
    LT8920_SetCs(dev, 1U);

    return LT8920_OK;
}

int8 LT8920_LoadDefaultProfile(lt8920_t *dev)
{
    int8 ret;
    u8 i;
    u8 count;

    ret = LT8920_CheckDevice(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    /* 保持旧表写入顺序，部分射频配置对顺序敏感。 */
    count = (u8)(sizeof(lt8920_default_regs) / sizeof(lt8920_default_regs[0]));
    for (i = 0U; i < count; i++) {
        ret = LT8920_WriteReg(dev, lt8920_default_regs[i].reg, lt8920_default_regs[i].value);
        if (ret != LT8920_OK) {
            return ret;
        }
    }

    return LT8920_OK;
}

int8 LT8920_VerifyDefaultProfile(lt8920_t *dev)
{
    int8 ret;
    u8 i;
    u8 count;
    u16 actual;
    u16 expected;
    u16 mask;

    ret = LT8920_CheckDevice(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    LT8920_ClearVerifyFailure(dev);

    count = (u8)(sizeof(lt8920_default_regs) / sizeof(lt8920_default_regs[0]));
    for (i = 0U; i < count; i++) {
        expected = lt8920_default_regs[i].value;
        mask = lt8920_default_regs[i].verify_mask;
        if (mask == 0U) {
            continue;
        }

        ret = LT8920_ReadReg(dev, lt8920_default_regs[i].reg, &actual);
        if (ret != LT8920_OK) {
            LT8920_SetVerifyFailure(dev, lt8920_default_regs[i].reg, expected, 0xFFFFU);
            return ret;
        }
        if ((actual & mask) != (expected & mask)) {
            LT8920_SetVerifyFailure(dev, lt8920_default_regs[i].reg, expected, actual);
            return LT8920_ERR_VERIFY;
        }
    }

    return LT8920_OK;
}

int8 LT8920_Init(lt8920_t *dev, u8 channel, u32 sync_word)
{
    int8 ret;

    if (channel > 0x7FU) {
        return LT8920_ERR_PARAM;
    }

    ret = LT8920_LoadDefaultProfile(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    ret = LT8920_VerifyDefaultProfile(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    dev->initialized = 0U;
    dev->channel = (u8)(channel & 0x7FU);
    dev->sync_word = sync_word;
    dev->last_tx_fifo_count = 0U;

    ret = LT8920_SetSyncWord(dev, sync_word);
    if (ret != LT8920_OK) {
        return ret;
    }

    ret = LT8920_SetChannel(dev, channel);
    if (ret != LT8920_OK) {
        return ret;
    }

    ret = LT8920_WriteReg(dev, LT8920_REG_MODE_CONFIG, 0x6C90U);
    if (ret != LT8920_OK) {
        return ret;
    }

    ret = LT8920_ClearTxFifo(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    ret = LT8920_ClearRxFifo(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    ret = LT8920_EnterIdle(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    dev->initialized = 1U;
    return LT8920_OK;
}

int8 LT8920_SetChannel(lt8920_t *dev, u8 channel)
{
    int8 ret;

    ret = LT8920_CheckDevice(dev);
    if (ret != LT8920_OK) {
        return ret;
    }
    if (channel > 0x7FU) {
        return LT8920_ERR_PARAM;
    }

    dev->channel = (u8)(channel & 0x7FU);
    return LT8920_EnterIdle(dev);
}

int8 LT8920_SetSyncWord(lt8920_t *dev, u32 sync_word)
{
    int8 ret;

    ret = LT8920_EnterIdle(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    dev->sync_word = sync_word;

    /*
     * 旧工程同步字布局：
     * reg36=低 16 位，reg37=0x0380，reg38=0x5A5A，reg39=高 16 位。
     */
    ret = LT8920_WriteReg(dev, LT8920_REG_SYNCWORD_0, (u16)(sync_word & 0xFFFFUL));
    if (ret != LT8920_OK) {
        return ret;
    }
    ret = LT8920_WriteReg(dev, LT8920_REG_SYNCWORD_1, 0x0380U);
    if (ret != LT8920_OK) {
        return ret;
    }
    ret = LT8920_WriteReg(dev, LT8920_REG_SYNCWORD_2, 0x5A5AU);
    if (ret != LT8920_OK) {
        return ret;
    }
    ret = LT8920_WriteReg(dev, LT8920_REG_SYNCWORD_3, (u16)(sync_word >> 16));
    if (ret != LT8920_OK) {
        return ret;
    }

    return LT8920_ClearRxFifo(dev);
}

int8 LT8920_SetSyncRegs(lt8920_t *dev, u16 reg36, u16 reg39)
{
    int8 ret;

    ret = LT8920_EnterIdle(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    ret = LT8920_WriteReg(dev, LT8920_REG_SYNCWORD_0, reg36);
    if (ret != LT8920_OK) {
        return ret;
    }

    return LT8920_WriteReg(dev, LT8920_REG_SYNCWORD_3, reg39);
}

int8 LT8920_EnterIdle(lt8920_t *dev)
{
    int8 ret;

    ret = LT8920_CheckDevice(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    return LT8920_UpdateModeRegister(dev, LT8920_MODE_IDLE);
}

int8 LT8920_EnterRx(lt8920_t *dev)
{
    int8 ret;

    ret = LT8920_WriteReg(dev, LT8920_REG_MODE_CONFIG, 0x6C90U);
    if (ret != LT8920_OK) {
        return ret;
    }

    return LT8920_UpdateModeRegister(dev, LT8920_MODE_RX);
}

int8 LT8920_EnterTx(lt8920_t *dev)
{
    int8 ret;

    ret = LT8920_WriteReg(dev, LT8920_REG_MODE_CONFIG, 0x6C90U);
    if (ret != LT8920_OK) {
        return ret;
    }

    return LT8920_UpdateModeRegister(dev, LT8920_MODE_TX);
}

int8 LT8920_OpenRx(lt8920_t *dev)
{
    int8 ret;

    ret = LT8920_EnterIdle(dev);
    if (ret != LT8920_OK) {
        return ret;
    }
    ret = LT8920_ClearRxFifo(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    return LT8920_EnterRx(dev);
}

int8 LT8920_OpenRxOnChannel(lt8920_t *dev, u8 channel)
{
    int8 ret;

    if (channel > 0x7FU) {
        return LT8920_ERR_PARAM;
    }

    ret = LT8920_CheckDevice(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    dev->channel = (u8)(channel & 0x7FU);
    return LT8920_OpenRx(dev);
}

int8 LT8920_EnterCarrierWave(lt8920_t *dev)
{
    int8 ret;

    /* 连续载波测试模式寄存器值来自旧 LT8920 驱动。 */
    ret = LT8920_EnterIdle(dev);
    if (ret != LT8920_OK) {
        return ret;
    }
    ret = LT8920_WriteReg(dev, 32U, 0x1807U);
    if (ret != LT8920_OK) {
        return ret;
    }
    ret = LT8920_WriteReg(dev, 34U, 0x830BU);
    if (ret != LT8920_OK) {
        return ret;
    }
    ret = LT8920_WriteReg(dev, 11U, 0x8008U);
    if (ret != LT8920_OK) {
        return ret;
    }

    return LT8920_UpdateModeRegister(dev, LT8920_MODE_TX);
}

int8 LT8920_ClearTxFifo(lt8920_t *dev)
{
    return LT8920_WriteReg(dev, LT8920_REG_FIFO_CTRL, LT8920_FIFO_CLEAR_VALUE);
}

int8 LT8920_ClearRxFifo(lt8920_t *dev)
{
    return LT8920_WriteReg(dev, LT8920_REG_FIFO_CTRL, LT8920_FIFO_CLEAR_VALUE);
}

int8 LT8920_ReadTxFifoCount(lt8920_t *dev, u8 *count)
{
    int8 ret;
    u16 reg52;

    if (count == 0) {
        return LT8920_ERR_PARAM;
    }

    ret = LT8920_ReadReg(dev, LT8920_REG_FIFO_CTRL, &reg52);
    if (ret != LT8920_OK) {
        return ret;
    }

    /* FIFO 写入后，寄存器 52 的 [13:8] 为 FIFO 字节计数。 */
    *count = (u8)((reg52 >> 8) & 0x003FU);
    return LT8920_OK;
}

int8 LT8920_WritePacket(lt8920_t *dev, const u8 *buf, u8 len)
{
    int8 ret;
    u8 packet_buf[LT8920_MAX_PAYLOAD_LEN + 1U];
    u8 i;
    u8 fifo_count;

    if ((dev == 0) || (buf == 0) || (len == 0U) || (len > LT8920_MAX_PAYLOAD_LEN)) {
        return LT8920_ERR_PARAM;
    }
    if (dev->initialized == 0U) {
        return LT8920_ERR_STATE;
    }

    ret = LT8920_EnterIdle(dev);
    if (ret != LT8920_OK) {
        return ret;
    }
    ret = LT8920_ClearTxFifo(dev);
    if (ret != LT8920_OK) {
        return ret;
    }

    /* LT8920 FIFO 包格式：第 1 字节为载荷长度。 */
    packet_buf[0] = len;
    for (i = 0U; i < len; i++) {
        packet_buf[i + 1U] = buf[i];
    }

    ret = LT8920_WriteFifo(dev, packet_buf, (u8)(len + 1U));
    if (ret != LT8920_OK) {
        return ret;
    }

    ret = LT8920_ReadTxFifoCount(dev, &fifo_count);
    if (ret != LT8920_OK) {
        return ret;
    }

    dev->last_tx_fifo_count = fifo_count;
    if (fifo_count != (u8)(len + 1U)) {
        return LT8920_ERR_VERIFY;
    }

    return LT8920_OK;
}

int8 LT8920_StartTxPacket(lt8920_t *dev, const u8 *buf, u8 len)
{
    int8 ret;

    ret = LT8920_WritePacket(dev, buf, len);
    if (ret != LT8920_OK) {
        return ret;
    }

    return LT8920_EnterTx(dev);
}

int8 LT8920_ForceTxPacket(lt8920_t *dev, const u8 *buf, u8 len)
{
    int8 ret;

    ret = LT8920_StartTxPacket(dev, buf, len);
    if (ret != LT8920_OK) {
        return ret;
    }

    LT8920_DelayUs(dev, 20U);
    return LT8920_OK;
}

int8 LT8920_ReadPacket(lt8920_t *dev, u8 *buf, u8 buf_len, u8 *out_len)
{
    int8 ret;
    u16 status;
    u8 packet_len;
    u8 read_len;
    u8 value;
    u8 i;

    if ((dev == 0) || (buf == 0) || (out_len == 0) || (buf_len == 0U)) {
        return LT8920_ERR_PARAM;
    }
    if (dev->initialized == 0U) {
        return LT8920_ERR_STATE;
    }

    ret = LT8920_ReadStatus(dev, &status);
    if (ret != LT8920_OK) {
        return ret;
    }
    if (LT8920_IsPacketDone(status) == 0U) {
        return LT8920_ERR_EMPTY;
    }
    if (LT8920_IsCrcError(status) != 0U) {
        (void)LT8920_ResetRxPath(dev);
        return LT8920_ERR_CRC;
    }

    LT8920_SetCs(dev, 0U);
    (void)LT8920_SpiTransfer(dev, LT8920_BuildCommand(LT8920_REG_FIFO, 1U));
    /* FIFO 第 1 字节为长度，后面紧跟载荷。 */
    packet_len = LT8920_SpiTransfer(dev, 0x00U);
    read_len = packet_len;
    if (read_len > 64U) {
        read_len = 64U;
    }

    if (packet_len == 0U) {
        LT8920_SetCs(dev, 1U);
        (void)LT8920_ResetRxPath(dev);
        return LT8920_ERR_FAIL;
    }

    ret = LT8920_OK;
    for (i = 0U; i < read_len; i++) {
        value = LT8920_SpiTransfer(dev, 0x00U);
        if ((packet_len <= LT8920_MAX_PAYLOAD_LEN) && (packet_len <= buf_len)) {
            buf[i] = value;
        } else {
            ret = LT8920_ERR_OVERFLOW;
        }
    }
    LT8920_SetCs(dev, 1U);

    if (ret != LT8920_OK) {
        (void)LT8920_ResetRxPath(dev);
        return ret;
    }

    *out_len = packet_len;
    return LT8920_OK;
}

int8 LT8920_ReadStatus(lt8920_t *dev, u16 *status)
{
    return LT8920_ReadReg(dev, LT8920_REG_STATUS, status);
}

int8 LT8920_ReadRawRssi(lt8920_t *dev, u8 *rssi)
{
    int8 ret;
    u16 reg6;

    if (rssi == 0) {
        return LT8920_ERR_PARAM;
    }

    ret = LT8920_ReadReg(dev, LT8920_REG_RSSI, &reg6);
    if (ret != LT8920_OK) {
        return ret;
    }

    *rssi = (u8)((reg6 >> 10) & 0x003FU);
    return LT8920_OK;
}

u8 LT8920_IsPacketDone(u16 status)
{
    return ((status & LT8920_STATUS_PKT_FLAG) != 0U) ? 1U : 0U;
}

u8 LT8920_IsCrcError(u16 status)
{
    return ((status & LT8920_STATUS_CRC_ERROR) != 0U) ? 1U : 0U;
}

void LT8920_GetVerifyFailure(lt8920_t *dev, u8 *reg, u16 *expected, u16 *actual)
{
    if (dev == 0) {
        return;
    }
    if (reg != 0) {
        *reg = dev->verify_fail_reg;
    }
    if (expected != 0) {
        *expected = dev->verify_fail_expected;
    }
    if (actual != 0) {
        *actual = dev->verify_fail_actual;
    }
}

u8 LT8920_GetLastTxFifoCount(const lt8920_t *dev)
{
    if (dev == 0) {
        return 0U;
    }

    return dev->last_tx_fifo_count;
}

u8 LT8920_GetChannel(const lt8920_t *dev)
{
    if (dev == 0) {
        return 0U;
    }

    return dev->channel;
}
