#include "QMC6309.h"

static int8 QMC6309_Fail(qmc6309_t *dev, int8 err);

/* 只检查必需的芯片访问回调，延时回调允许为空。 */
static int8 QMC6309_CheckDevice(qmc6309_t *dev)
{
    if ((dev == 0) ||
        (dev->bus.write_reg == 0) ||
        (dev->bus.read_regs == 0)) {
        QMC6309_Fail(dev, QMC6309_ERR_PARAM);
        return QMC6309_ERR_PARAM;
    }

    return QMC6309_OK;
}

static void QMC6309_Delay(qmc6309_t *dev, u16 ms)
{
    if ((dev != 0) && (dev->bus.delay_ms != 0)) {
        dev->bus.delay_ms(ms);
    }
}

static int8 QMC6309_Fail(qmc6309_t *dev, int8 err)
{
    if (dev != 0) {
        dev->last_error = err;
    }
    return err;
}

static u8 QMC6309_StatusIsReady(u8 status)
{
    return ((status & QMC6309_STATUS_DRDY) != 0U) ? 1U : 0U;
}

static u8 QMC6309_TryAddress(qmc6309_t *dev, u8 addr)
{
    u8 id;

    if (dev == 0) {
        return 0U;
    }

    dev->addr = addr;
    id = 0xFFU;
    if ((QMC6309_ReadID(dev, &id) == QMC6309_OK) && (id == QMC6309_CHIP_ID_VALUE)) {
        return 1U;
    }

    dev->last_id = id;
    return 0U;
}

int8 QMC6309_Bind(qmc6309_t *dev, const qmc6309_bus_t *bus)
{
    if ((dev == 0) ||
        (bus == 0) ||
        (bus->write_reg == 0) ||
        (bus->read_regs == 0)) {
        return QMC6309_ERR_PARAM;
    }

    dev->bus = *bus;
    dev->addr = QMC6309_I2C_ADDR_PRIMARY;
    dev->initialized = 0U;
    dev->data_ready = 0U;
    dev->last_error = QMC6309_OK;
    dev->last_id = 0U;
    dev->last_status = 0U;
    dev->last_control_1 = 0U;
    dev->last_control_2 = 0U;
    return QMC6309_OK;
}

void QMC6309_SetAddress(qmc6309_t *dev, u8 addr)
{
    if (dev != 0) {
        dev->addr = addr;
    }
}

u8 QMC6309_GetAddress(const qmc6309_t *dev)
{
    if (dev == 0) {
        return 0U;
    }

    return dev->addr;
}

int8 QMC6309_WriteReg(qmc6309_t *dev, u8 reg, u8 value)
{
    int8 ret;

    ret = QMC6309_CheckDevice(dev);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }

    if (dev->bus.write_reg(dev->addr, reg, value) == 0) {
        dev->last_error = QMC6309_OK;
        if (reg == QMC6309_REG_CONTROL_1) {
            dev->last_control_1 = value;
        } else if (reg == QMC6309_REG_CONTROL_2) {
            dev->last_control_2 = value;
        }
        return QMC6309_OK;
    }

    return QMC6309_Fail(dev, QMC6309_ERR_BUS);
}

int8 QMC6309_ReadRegs(qmc6309_t *dev, u8 start_reg, u8 *buf, u8 len)
{
    int8 ret;

    ret = QMC6309_CheckDevice(dev);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }
    if ((buf == 0) || (len == 0U)) {
        return QMC6309_Fail(dev, QMC6309_ERR_PARAM);
    }

    if (dev->bus.read_regs(dev->addr, start_reg, buf, len) == 0) {
        dev->last_error = QMC6309_OK;
        return QMC6309_OK;
    }

    return QMC6309_Fail(dev, QMC6309_ERR_BUS);
}

int8 QMC6309_ReadReg(qmc6309_t *dev, u8 reg, u8 *value)
{
    if (value == 0) {
        return QMC6309_Fail(dev, QMC6309_ERR_PARAM);
    }

    return QMC6309_ReadRegs(dev, reg, value, 1U);
}

int8 QMC6309_ReadID(qmc6309_t *dev, u8 *chip_id)
{
    int8 ret;

    ret = QMC6309_ReadReg(dev, QMC6309_REG_CHIP_ID, chip_id);
    if ((ret == QMC6309_OK) && (dev != 0) && (chip_id != 0)) {
        dev->last_id = *chip_id;
    }
    return ret;
}

int8 QMC6309_ReadStatus(qmc6309_t *dev, u8 *status)
{
    int8 ret;

    if (status == 0) {
        return QMC6309_ERR_PARAM;
    }

    ret = QMC6309_ReadReg(dev, QMC6309_REG_STATUS, status);
    if (ret != QMC6309_OK) {
        return ret;
    }

    dev->last_status = *status;
    dev->data_ready = QMC6309_StatusIsReady(*status);
    return QMC6309_OK;
}

int8 QMC6309_SelectAddress(qmc6309_t *dev)
{
    u8 old_addr;

    if (dev == 0) {
        return QMC6309_ERR_PARAM;
    }

    old_addr = dev->addr;

    /* 保留第一个能读到正确芯片 ID 的地址。 */
    if (QMC6309_TryAddress(dev, QMC6309_I2C_ADDR_PRIMARY_7BIT) != 0U) {
        return QMC6309_OK;
    }
    if (QMC6309_TryAddress(dev, QMC6309_I2C_ADDR_PRIMARY) != 0U) {
        return QMC6309_OK;
    }
    if (QMC6309_TryAddress(dev, QMC6309_I2C_ADDR_ALT_7BIT) != 0U) {
        return QMC6309_OK;
    }
    if (QMC6309_TryAddress(dev, QMC6309_I2C_ADDR_ALT) != 0U) {
        return QMC6309_OK;
    }

    dev->addr = old_addr;
    return QMC6309_Fail(dev, QMC6309_ERR_ID);
}

int8 QMC6309_Reset(qmc6309_t *dev)
{
    int8 ret;

    /* 软复位按旧驱动时序执行：写 reset，等待，再清零。 */
    ret = QMC6309_WriteReg(dev, QMC6309_REG_CONTROL_2, QMC6309_CTRL2_SOFT_RESET);
    if (ret != QMC6309_OK) {
        return ret;
    }
    QMC6309_Delay(dev, QMC6309_RESET_ENTER_DELAY_MS);

    ret = QMC6309_WriteReg(dev, QMC6309_REG_CONTROL_2, 0x00U);
    if (ret != QMC6309_OK) {
        return ret;
    }
    QMC6309_Delay(dev, QMC6309_RESET_EXIT_DELAY_MS);

    return QMC6309_OK;
}

int8 QMC6309_WaitReady(qmc6309_t *dev, u16 timeout_ms)
{
    u16 elapsed_ms;
    u8 id;
    int8 ret;

    for (elapsed_ms = 0U; elapsed_ms < timeout_ms; elapsed_ms = (u16)(elapsed_ms + 10U)) {
        ret = QMC6309_ReadID(dev, &id);
        if ((ret == QMC6309_OK) && (id == QMC6309_CHIP_ID_VALUE)) {
            return QMC6309_OK;
        }
        QMC6309_Delay(dev, 10U);
    }

    return QMC6309_Fail(dev, QMC6309_ERR_NOT_READY);
}

static int8 QMC6309_WaitDataReady(qmc6309_t *dev, u16 timeout_ms)
{
    u16 elapsed_ms;
    u8 status;
    int8 ret;

    ret = QMC6309_ERR_NOT_READY;
    for (elapsed_ms = 0U;
         elapsed_ms < timeout_ms;
         elapsed_ms = (u16)(elapsed_ms + QMC6309_DRDY_POLL_DELAY_MS)) {
        ret = QMC6309_ReadStatus(dev, &status);
        if (ret != QMC6309_OK) {
            return QMC6309_Fail(dev, ret);
        }
        if (QMC6309_StatusIsReady(status) != 0U) {
            return QMC6309_OK;
        }
        QMC6309_Delay(dev, QMC6309_DRDY_POLL_DELAY_MS);
    }

    if (ret == QMC6309_OK) {
        ret = QMC6309_ERR_NOT_READY;
    }
    return QMC6309_Fail(dev, ret);
}

int8 QMC6309_Init(qmc6309_t *dev)
{
    int8 ret;
    u8 id;
    u8 status;

    ret = QMC6309_CheckDevice(dev);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }

    dev->initialized = 0U;
    dev->data_ready = 0U;
    dev->last_error = QMC6309_OK;
    dev->last_id = 0U;
    dev->last_status = 0U;
    dev->last_control_1 = 0U;
    dev->last_control_2 = 0U;

    ret = QMC6309_SelectAddress(dev);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }

    ret = QMC6309_WaitReady(dev, QMC6309_POWER_UP_DELAY_MS);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }

    ret = QMC6309_ReadID(dev, &id);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }
    if (id != QMC6309_CHIP_ID_VALUE) {
        return QMC6309_Fail(dev, QMC6309_ERR_ID);
    }

    ret = QMC6309_Reset(dev);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }

    /* 最小芯片初始化流程：standby -> 写 CONTROL_2 初始化值 -> active。 */
    ret = QMC6309_WriteReg(dev, QMC6309_REG_CONTROL_1, QMC6309_CTRL1_STANDBY);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }

    ret = QMC6309_WriteReg(dev, QMC6309_REG_CONTROL_2, QMC6309_CTRL2_INIT);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }

    ret = QMC6309_WriteReg(dev, QMC6309_REG_CONTROL_1, QMC6309_CTRL1_ACTIVE);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }
    QMC6309_Delay(dev, QMC6309_ENABLE_DELAY_MS);

    ret = QMC6309_ReadStatus(dev, &status);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }

    dev->initialized = 1U;
    dev->data_ready = QMC6309_StatusIsReady(status);
    dev->last_error = QMC6309_OK;
    return QMC6309_OK;
}

int8 QMC6309_ReadXYZ(qmc6309_t *dev, int16 *x, int16 *y, int16 *z)
{
    int8 ret;
    u8 raw[6];

    if ((x == 0) || (y == 0) || (z == 0)) {
        return QMC6309_Fail(dev, QMC6309_ERR_PARAM);
    }

    if ((dev == 0) || (dev->initialized == 0U)) {
        return QMC6309_Fail(dev, QMC6309_ERR_NOT_READY);
    }

    *x = 0;
    *y = 0;
    *z = 0;

    ret = QMC6309_WaitDataReady(dev, QMC6309_DRDY_TIMEOUT_MS);
    if (ret != QMC6309_OK) {
        return ret;
    }

    ret = QMC6309_ReadRegs(dev, QMC6309_REG_DATA_X_L, raw, 6U);
    if (ret != QMC6309_OK) {
        return QMC6309_Fail(dev, ret);
    }

    /* 数据寄存器为小端有符号 16 位原始值。 */
    *x = (int16)(((u16)raw[1] << 8) | raw[0]);
    *y = (int16)(((u16)raw[3] << 8) | raw[2]);
    *z = (int16)(((u16)raw[5] << 8) | raw[4]);

    /* 过滤 bring-up 阶段常见的浮空总线或未就绪读数。 */
    if ((raw[0] == 0xFFU) && (raw[1] == 0xFFU) &&
        (raw[2] == 0xFFU) && (raw[3] == 0xFFU) &&
        (raw[4] == 0xFFU) && (raw[5] == 0xFFU)) {
        return QMC6309_Fail(dev, QMC6309_ERR_DATA);
    }

    if ((*x == 0) && (*y == 0) && (*z == 0)) {
        return QMC6309_Fail(dev, QMC6309_ERR_DATA);
    }

    dev->data_ready = 0U;
    dev->last_error = QMC6309_OK;
    return QMC6309_OK;
}

int8 QMC6309_SetODR(qmc6309_t *dev, u8 odr)
{
    int8 ret;

    ret = QMC6309_WriteReg(dev, QMC6309_REG_CONTROL_2, odr);
    return (ret == QMC6309_OK) ? QMC6309_OK : QMC6309_Fail(dev, ret);
}

int8 QMC6309_ReadDiagRegs(qmc6309_t *dev, qmc6309_regs_t *regs)
{
    int8 ret;
    int8 saved_error;

    if (regs == 0) {
        return QMC6309_ERR_PARAM;
    }

    saved_error = (dev != 0) ? dev->last_error : QMC6309_ERR_PARAM;
    ret = QMC6309_ReadReg(dev, QMC6309_REG_CHIP_ID, &regs->chip_id);
    if (ret != QMC6309_OK) {
        if (dev != 0) {
            dev->last_error = saved_error;
        }
        return ret;
    }
    if (dev != 0) {
        dev->last_id = regs->chip_id;
    }
    ret = QMC6309_ReadReg(dev, QMC6309_REG_STATUS, &regs->status);
    if (ret != QMC6309_OK) {
        if (dev != 0) {
            dev->last_error = saved_error;
        }
        return ret;
    }
    if (dev != 0) {
        dev->last_status = regs->status;
        dev->data_ready = QMC6309_StatusIsReady(regs->status);
    }
    ret = QMC6309_ReadReg(dev, QMC6309_REG_CONTROL_1, &regs->control_1);
    if (ret != QMC6309_OK) {
        if (dev != 0) {
            dev->last_error = saved_error;
        }
        return ret;
    }
    if (dev != 0) {
        dev->last_control_1 = regs->control_1;
    }
    ret = QMC6309_ReadReg(dev, QMC6309_REG_CONTROL_2, &regs->control_2);
    if (dev != 0) {
        if (ret == QMC6309_OK) {
            dev->last_control_2 = regs->control_2;
        }
        dev->last_error = saved_error;
    }
    return ret;
}

u8 QMC6309_IsReady(const qmc6309_t *dev)
{
    return ((dev != 0) && (dev->initialized != 0U)) ? 1U : 0U;
}

u8 QMC6309_HasDataReady(const qmc6309_t *dev)
{
    return (dev != 0) ? dev->data_ready : 0U;
}

int8 QMC6309_GetLastError(const qmc6309_t *dev)
{
    return (dev != 0) ? dev->last_error : QMC6309_ERR_PARAM;
}
