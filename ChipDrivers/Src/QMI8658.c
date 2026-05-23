#include "QMI8658.h"

static int8 QMI8658_CheckDevice(qmi8658_t *dev)
{
    if ((dev == 0) ||
        (dev->bus.write_reg == 0) ||
        (dev->bus.read_regs == 0)) {
        return QMI8658_ERR_PARAM;
    }

    return QMI8658_OK;
}

static void QMI8658_Delay(qmi8658_t *dev, u16 ms)
{
    if ((dev != 0) && (dev->bus.delay_ms != 0)) {
        dev->bus.delay_ms(dev->bus.ctx, ms);
    }
}

static u8 QMI8658_IsStatusReady(u8 status0)
{
    return (((status0 & (QMI8658_STATUS0_A_DA | QMI8658_STATUS0_G_DA)) ==
             (QMI8658_STATUS0_A_DA | QMI8658_STATUS0_G_DA)) ? 1U : 0U);
}

static int8 QMI8658_SelectAddress(qmi8658_t *dev)
{
    int8 ret;
    u8 id;
    u8 old_addr;

    if (dev == 0) {
        return QMI8658_ERR_PARAM;
    }

    old_addr = dev->addr;

    dev->addr = QMI8658_I2C_ADDR_PRIMARY;
    ret = QMI8658_ReadID(dev, &id);
    if ((ret == QMI8658_OK) && (id == QMI8658_CHIP_ID_VALUE)) {
        return QMI8658_OK;
    }

    dev->addr = QMI8658_I2C_ADDR_ALT;
    ret = QMI8658_ReadID(dev, &id);
    if ((ret == QMI8658_OK) && (id == QMI8658_CHIP_ID_VALUE)) {
        return QMI8658_OK;
    }

    dev->addr = old_addr;
    return QMI8658_ERR_ID;
}

static int8 QMI8658_VerifyConfig(qmi8658_t *dev)
{
    int8 ret;
    u8 ctrl1;
    u8 ctrl2;
    u8 ctrl3;
    u8 ctrl5;
    u8 ctrl7;

    ret = QMI8658_ReadReg(dev, QMI8658_REG_CTRL1, &ctrl1);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_ReadReg(dev, QMI8658_REG_CTRL2, &ctrl2);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_ReadReg(dev, QMI8658_REG_CTRL3, &ctrl3);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_ReadReg(dev, QMI8658_REG_CTRL5, &ctrl5);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_ReadReg(dev, QMI8658_REG_CTRL7, &ctrl7);
    if (ret != QMI8658_OK) {
        return ret;
    }

    if ((ctrl1 != QMI8658_CTRL1_INIT) ||
        (ctrl2 != QMI8658_CTRL2_INIT) ||
        (ctrl3 != QMI8658_CTRL3_INIT) ||
        (ctrl5 != QMI8658_CTRL5_INIT) ||
        (ctrl7 != QMI8658_CTRL7_INIT)) {
        return QMI8658_ERR_VERIFY;
    }

    return QMI8658_OK;
}

int8 QMI8658_Bind(qmi8658_t *dev, const qmi8658_bus_t *bus)
{
    if ((dev == 0) ||
        (bus == 0) ||
        (bus->write_reg == 0) ||
        (bus->read_regs == 0)) {
        return QMI8658_ERR_PARAM;
    }

    dev->bus = *bus;
    dev->addr = QMI8658_I2C_ADDR_PRIMARY;
    dev->initialized = 0U;
    dev->data_ready = 0U;
    dev->last_status0 = 0U;
    return QMI8658_OK;
}

void QMI8658_SetAddress(qmi8658_t *dev, u8 addr)
{
    if (dev != 0) {
        dev->addr = addr;
    }
}

u8 QMI8658_GetAddress(const qmi8658_t *dev)
{
    if (dev == 0) {
        return 0U;
    }

    return dev->addr;
}

int8 QMI8658_WriteReg(qmi8658_t *dev, u8 reg, u8 value)
{
    int8 ret;

    ret = QMI8658_CheckDevice(dev);
    if (ret != QMI8658_OK) {
        return ret;
    }

    return (dev->bus.write_reg(dev->bus.ctx, dev->addr, reg, value) == 0) ? QMI8658_OK : QMI8658_ERR_BUS;
}

int8 QMI8658_ReadRegs(qmi8658_t *dev, u8 start_reg, u8 *buf, u8 len)
{
    int8 ret;

    ret = QMI8658_CheckDevice(dev);
    if (ret != QMI8658_OK) {
        return ret;
    }
    if ((buf == 0) || (len == 0U)) {
        return QMI8658_ERR_PARAM;
    }

    return (dev->bus.read_regs(dev->bus.ctx, dev->addr, start_reg, buf, len) == 0) ? QMI8658_OK : QMI8658_ERR_BUS;
}

int8 QMI8658_ReadReg(qmi8658_t *dev, u8 reg, u8 *value)
{
    if (value == 0) {
        return QMI8658_ERR_PARAM;
    }

    return QMI8658_ReadRegs(dev, reg, value, 1U);
}

int8 QMI8658_ReadID(qmi8658_t *dev, u8 *chip_id)
{
    return QMI8658_ReadReg(dev, QMI8658_REG_WHO_AM_I, chip_id);
}

int8 QMI8658_ReadStatus0(qmi8658_t *dev, u8 *status0)
{
    int8 ret;

    if (status0 == 0) {
        return QMI8658_ERR_PARAM;
    }

    ret = QMI8658_ReadReg(dev, QMI8658_REG_STATUS0, status0);
    if (ret != QMI8658_OK) {
        return ret;
    }

    dev->last_status0 = *status0;
    dev->data_ready = QMI8658_IsStatusReady(*status0);
    return QMI8658_OK;
}

int8 QMI8658_Init(qmi8658_t *dev)
{
    int8 ret;
    u8 retry;
    u8 status0;
    u16 waited_ms;

    ret = QMI8658_CheckDevice(dev);
    if (ret != QMI8658_OK) {
        return ret;
    }

    dev->initialized = 0U;
    dev->data_ready = 0U;
    dev->last_status0 = 0U;

    ret = QMI8658_ERR_ID;
    for (retry = 0U; retry < QMI8658_INIT_RETRY_MAX; retry++) {
        QMI8658_Delay(dev, QMI8658_POWER_UP_DELAY_MS);
        ret = QMI8658_SelectAddress(dev);
        if (ret == QMI8658_OK) {
            break;
        }
        QMI8658_Delay(dev, QMI8658_INIT_RETRY_DELAY_MS);
    }
    if (ret != QMI8658_OK) {
        return ret;
    }

    ret = QMI8658_WriteReg(dev, QMI8658_REG_CTRL7, 0x00U);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_WriteReg(dev, QMI8658_REG_CTRL1, QMI8658_CTRL1_INIT);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_WriteReg(dev, QMI8658_REG_CTRL2, QMI8658_CTRL2_INIT);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_WriteReg(dev, QMI8658_REG_CTRL3, QMI8658_CTRL3_INIT);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_WriteReg(dev, QMI8658_REG_CTRL5, QMI8658_CTRL5_INIT);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_WriteReg(dev, QMI8658_REG_CTRL7, QMI8658_CTRL7_INIT);
    if (ret != QMI8658_OK) {
        return ret;
    }

    QMI8658_Delay(dev, QMI8658_ENABLE_DELAY_MS);

    ret = QMI8658_VerifyConfig(dev);
    if (ret != QMI8658_OK) {
        return ret;
    }

    waited_ms = 0U;
    while (waited_ms < QMI8658_READY_TIMEOUT_MS) {
        ret = QMI8658_ReadStatus0(dev, &status0);
        if (ret != QMI8658_OK) {
            return ret;
        }
        if (QMI8658_IsStatusReady(status0) != 0U) {
            dev->initialized = 1U;
            dev->data_ready = 1U;
            return QMI8658_OK;
        }

        QMI8658_Delay(dev, 5U);
        waited_ms = (u16)(waited_ms + 5U);
    }

    return QMI8658_ERR_NOT_READY;
}

int8 QMI8658_ReadRawSample(qmi8658_t *dev, qmi8658_sample_t *sample)
{
    int8 ret;
    u8 raw[14];
    u8 status0;

    if (sample == 0) {
        return QMI8658_ERR_PARAM;
    }
    if ((dev == 0) || (dev->initialized == 0U)) {
        return QMI8658_ERR_NOT_READY;
    }

    sample->acc_x_raw = 0;
    sample->acc_y_raw = 0;
    sample->acc_z_raw = 0;
    sample->gyro_x_raw = 0;
    sample->gyro_y_raw = 0;
    sample->gyro_z_raw = 0;
    sample->temp_raw = 0;
    sample->has_temp = 0U;

    ret = QMI8658_ReadRegs(dev, QMI8658_REG_TEMP_L, raw, 14U);
    if (ret != QMI8658_OK) {
        return ret;
    }

    sample->temp_raw = (int16)(((u16)raw[1] << 8) | raw[0]);
    sample->acc_x_raw = (int16)(((u16)raw[3] << 8) | raw[2]);
    sample->acc_y_raw = (int16)(((u16)raw[5] << 8) | raw[4]);
    sample->acc_z_raw = (int16)(((u16)raw[7] << 8) | raw[6]);
    sample->gyro_x_raw = (int16)(((u16)raw[9] << 8) | raw[8]);
    sample->gyro_y_raw = (int16)(((u16)raw[11] << 8) | raw[10]);
    sample->gyro_z_raw = (int16)(((u16)raw[13] << 8) | raw[12]);
    sample->has_temp = 1U;

    if ((raw[0] == 0xFFU) && (raw[1] == 0xFFU) &&
        (raw[2] == 0xFFU) && (raw[3] == 0xFFU) &&
        (raw[4] == 0xFFU) && (raw[5] == 0xFFU) &&
        (raw[6] == 0xFFU) && (raw[7] == 0xFFU) &&
        (raw[8] == 0xFFU) && (raw[9] == 0xFFU) &&
        (raw[10] == 0xFFU) && (raw[11] == 0xFFU) &&
        (raw[12] == 0xFFU) && (raw[13] == 0xFFU)) {
        return QMI8658_ERR_DATA;
    }

    if ((sample->acc_x_raw == 0) &&
        (sample->acc_y_raw == 0) &&
        (sample->acc_z_raw == 0) &&
        (sample->gyro_x_raw == 0) &&
        (sample->gyro_y_raw == 0) &&
        (sample->gyro_z_raw == 0)) {
        return QMI8658_ERR_DATA;
    }

    ret = QMI8658_ReadStatus0(dev, &status0);
    if (ret != QMI8658_OK) {
        return ret;
    }

    return QMI8658_OK;
}

u8 QMI8658_IsReady(const qmi8658_t *dev)
{
    return ((dev != 0) && (dev->initialized != 0U)) ? 1U : 0U;
}

u8 QMI8658_HasDataReady(const qmi8658_t *dev)
{
    return (dev != 0) ? dev->data_ready : 0U;
}

void QMI8658_ClearDataReady(qmi8658_t *dev)
{
    if (dev != 0) {
        dev->data_ready = 0U;
    }
}
