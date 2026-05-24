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
        dev->bus.delay_ms(ms);
    }
}

static int8 QMI8658_Fail(qmi8658_t *dev, int8 err)
{
    if (dev != 0) {
        dev->last_error = err;
    }
    return err;
}

static void QMI8658_ResetConfigDiag(qmi8658_t *dev)
{
    if (dev != 0) {
        dev->last_cfg_retry = 0xFFU;
        dev->last_cfg_reg = 0xFFU;
        dev->last_cfg_write = 0xFFU;
        dev->last_cfg_read = 0xFFU;
        dev->last_cfg_ret = QMI8658_OK;
    }
}

static void QMI8658_RecordConfigDiag(qmi8658_t *dev,
                                     u8 retry,
                                     u8 reg,
                                     u8 write_value,
                                     u8 read_value,
                                     int8 ret)
{
    if (dev != 0) {
        dev->last_cfg_retry = retry;
        dev->last_cfg_reg = reg;
        dev->last_cfg_write = write_value;
        dev->last_cfg_read = read_value;
        dev->last_cfg_ret = ret;
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
    u8 retry;

    if (dev == 0) {
        return QMI8658_ERR_PARAM;
    }

    old_addr = dev->addr;

    for (retry = 0U; retry < QMI8658_ID_RETRY_MAX; retry++) {
        dev->addr = QMI8658_I2C_ADDR_PRIMARY;
        ret = QMI8658_ReadID(dev, &id);
        dev->last_id = id;
        if ((ret == QMI8658_OK) && (id == QMI8658_CHIP_ID_VALUE)) {
            return QMI8658_OK;
        }

        dev->addr = QMI8658_I2C_ADDR_ALT;
        ret = QMI8658_ReadID(dev, &id);
        dev->last_id = id;
        if ((ret == QMI8658_OK) && (id == QMI8658_CHIP_ID_VALUE)) {
            return QMI8658_OK;
        }

        if ((u8)(retry + 1U) < QMI8658_ID_RETRY_MAX) {
            QMI8658_Delay(dev, QMI8658_ID_RETRY_DELAY_MS);
        }
    }

    dev->addr = old_addr;
    return QMI8658_Fail(dev, QMI8658_ERR_ID);
}

static int8 QMI8658_CheckConfigReg(qmi8658_t *dev, u8 retry, u8 reg, u8 expected, u8 *value)
{
    int8 ret;
    u8 readback;

    readback = 0xFFU;
    ret = QMI8658_ReadReg(dev, reg, &readback);
    if (value != 0) {
        *value = readback;
    }
    if (ret != QMI8658_OK) {
        QMI8658_RecordConfigDiag(dev, retry, reg, expected, readback, ret);
        return ret;
    }
    if (readback != expected) {
        QMI8658_RecordConfigDiag(dev, retry, reg, expected, readback, QMI8658_ERR_VERIFY);
        return QMI8658_ERR_VERIFY;
    }

    return QMI8658_OK;
}

static int8 QMI8658_WriteAndCheckReg(qmi8658_t *dev, u8 retry, u8 reg, u8 value)
{
    int8 ret;
    u8 readback;

    readback = 0xFFU;
    ret = QMI8658_WriteReg(dev, reg, value);
    if (ret != QMI8658_OK) {
        QMI8658_RecordConfigDiag(dev, retry, reg, value, readback, ret);
        return ret;
    }

    QMI8658_Delay(dev, QMI8658_VERIFY_RETRY_DELAY_MS);
    ret = QMI8658_ReadReg(dev, reg, &readback);
    if (ret != QMI8658_OK) {
        QMI8658_RecordConfigDiag(dev, retry, reg, value, readback, ret);
        return ret;
    }
    if (readback != value) {
        QMI8658_RecordConfigDiag(dev, retry, reg, value, readback, QMI8658_ERR_VERIFY);
        return QMI8658_ERR_VERIFY;
    }

    QMI8658_RecordConfigDiag(dev, retry, reg, value, readback, QMI8658_OK);
    return QMI8658_OK;
}

static int8 QMI8658_VerifyConfig(qmi8658_t *dev, u8 cfg_retry)
{
    int8 ret;
    u8 retry;
    u8 ctrl1;
    u8 ctrl2;
    u8 ctrl3;
    u8 ctrl5;
    u8 ctrl7;

    ret = QMI8658_ERR_VERIFY;
    for (retry = 0U; retry < QMI8658_VERIFY_RETRY_MAX; retry++) {
        ctrl1 = 0xFFU;
        ctrl2 = 0xFFU;
        ctrl3 = 0xFFU;
        ctrl5 = 0xFFU;
        ctrl7 = 0xFFU;

        ret = QMI8658_CheckConfigReg(dev, cfg_retry, QMI8658_REG_CTRL1, QMI8658_CTRL1_INIT, &ctrl1);
        if (ret == QMI8658_OK) {
            ret = QMI8658_CheckConfigReg(dev, cfg_retry, QMI8658_REG_CTRL2, QMI8658_CTRL2_INIT, &ctrl2);
        }
        if (ret == QMI8658_OK) {
            ret = QMI8658_CheckConfigReg(dev, cfg_retry, QMI8658_REG_CTRL3, QMI8658_CTRL3_INIT, &ctrl3);
        }
        if (ret == QMI8658_OK) {
            ret = QMI8658_CheckConfigReg(dev, cfg_retry, QMI8658_REG_CTRL5, QMI8658_CTRL5_INIT, &ctrl5);
        }
        if (ret == QMI8658_OK) {
            ret = QMI8658_CheckConfigReg(dev, cfg_retry, QMI8658_REG_CTRL7, QMI8658_CTRL7_INIT, &ctrl7);
        }

        if (dev != 0) {
            dev->last_ctrl1 = ctrl1;
            dev->last_ctrl2 = ctrl2;
            dev->last_ctrl3 = ctrl3;
            dev->last_ctrl5 = ctrl5;
            dev->last_ctrl7 = ctrl7;
        }

        if ((ret == QMI8658_OK) &&
            (ctrl1 == QMI8658_CTRL1_INIT) &&
            (ctrl2 == QMI8658_CTRL2_INIT) &&
            (ctrl3 == QMI8658_CTRL3_INIT) &&
            (ctrl5 == QMI8658_CTRL5_INIT) &&
            (ctrl7 == QMI8658_CTRL7_INIT)) {
            return QMI8658_OK;
        }

        if ((u8)(retry + 1U) < QMI8658_VERIFY_RETRY_MAX) {
            QMI8658_Delay(dev, QMI8658_VERIFY_RETRY_DELAY_MS);
        }
    }

    if (ret != QMI8658_OK) {
        return ret;
    }
    return QMI8658_ERR_VERIFY;
}

static int8 QMI8658_WriteConfig(qmi8658_t *dev, u8 retry)
{
    int8 ret;

    ret = QMI8658_WriteAndCheckReg(dev, retry, QMI8658_REG_CTRL7, 0x00U);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_WriteAndCheckReg(dev, retry, QMI8658_REG_CTRL1, QMI8658_CTRL1_INIT);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_WriteAndCheckReg(dev, retry, QMI8658_REG_CTRL2, QMI8658_CTRL2_INIT);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_WriteAndCheckReg(dev, retry, QMI8658_REG_CTRL3, QMI8658_CTRL3_INIT);
    if (ret != QMI8658_OK) {
        return ret;
    }
    ret = QMI8658_WriteAndCheckReg(dev, retry, QMI8658_REG_CTRL5, QMI8658_CTRL5_INIT);
    if (ret != QMI8658_OK) {
        return ret;
    }
    return QMI8658_WriteAndCheckReg(dev, retry, QMI8658_REG_CTRL7, QMI8658_CTRL7_INIT);
}

static int8 QMI8658_ConfigureWithRetry(qmi8658_t *dev)
{
    int8 ret;
    u8 retry;

    ret = QMI8658_ERR_VERIFY;
    for (retry = 0U; retry < QMI8658_CONFIG_RETRY_MAX; retry++) {
        ret = QMI8658_WriteConfig(dev, retry);
        if (ret == QMI8658_OK) {
            QMI8658_Delay(dev, QMI8658_ENABLE_DELAY_MS);
            ret = QMI8658_VerifyConfig(dev, retry);
            if (ret == QMI8658_OK) {
                return QMI8658_OK;
            }
        }

        if ((u8)(retry + 1U) < QMI8658_CONFIG_RETRY_MAX) {
            QMI8658_Delay(dev, QMI8658_CONFIG_RETRY_DELAY_MS);
        }
    }

    return ret;
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
    dev->last_error = QMI8658_OK;
    dev->last_id = 0U;
    dev->last_ctrl1 = 0U;
    dev->last_ctrl2 = 0U;
    dev->last_ctrl3 = 0U;
    dev->last_ctrl5 = 0U;
    dev->last_ctrl7 = 0U;
    dev->last_status0 = 0U;
    QMI8658_ResetConfigDiag(dev);
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

    if (dev->bus.write_reg(dev->addr, reg, value) == 0) {
        dev->last_error = QMI8658_OK;
        return QMI8658_OK;
    }

    return QMI8658_Fail(dev, QMI8658_ERR_BUS);
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

    if (dev->bus.read_regs(dev->addr, start_reg, buf, len) == 0) {
        dev->last_error = QMI8658_OK;
        return QMI8658_OK;
    }

    return QMI8658_Fail(dev, QMI8658_ERR_BUS);
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
    int8 ret;

    ret = QMI8658_ReadReg(dev, QMI8658_REG_WHO_AM_I, chip_id);
    if ((ret == QMI8658_OK) && (dev != 0) && (chip_id != 0)) {
        dev->last_id = *chip_id;
    }
    return ret;
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
    u16 elapsed_ms;

    ret = QMI8658_CheckDevice(dev);
    if (ret != QMI8658_OK) {
        return QMI8658_Fail(dev, ret);
    }

    dev->initialized = 0U;
    dev->data_ready = 0U;
    dev->last_error = QMI8658_OK;
    dev->last_id = 0U;
    dev->last_ctrl1 = 0U;
    dev->last_ctrl2 = 0U;
    dev->last_ctrl3 = 0U;
    dev->last_ctrl5 = 0U;
    dev->last_ctrl7 = 0U;
    dev->last_status0 = 0U;
    QMI8658_ResetConfigDiag(dev);

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
        return QMI8658_Fail(dev, ret);
    }

    ret = QMI8658_ConfigureWithRetry(dev);
    if (ret != QMI8658_OK) {
        return QMI8658_Fail(dev, ret);
    }

    ret = QMI8658_ERR_NOT_READY;
    for (elapsed_ms = 0U;
         elapsed_ms < QMI8658_READY_TIMEOUT_MS;
         elapsed_ms = (u16)(elapsed_ms + QMI8658_VERIFY_RETRY_DELAY_MS)) {
        ret = QMI8658_ReadStatus0(dev, &status0);
        if ((ret == QMI8658_OK) && (QMI8658_IsStatusReady(status0) != 0U)) {
            break;
        }
        QMI8658_Delay(dev, QMI8658_VERIFY_RETRY_DELAY_MS);
    }
    if ((ret != QMI8658_OK) || (QMI8658_IsStatusReady(dev->last_status0) == 0U)) {
        return QMI8658_Fail(dev, (ret == QMI8658_OK) ? QMI8658_ERR_NOT_READY : ret);
    }

    dev->initialized = 1U;
    dev->data_ready = 1U;
    dev->last_error = QMI8658_OK;
    return QMI8658_OK;
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

int8 QMI8658_ReadDiagRegs(qmi8658_t *dev, qmi8658_diag_regs_t *regs)
{
    int8 ret;
    int8 saved_error;

    if (regs == 0) {
        return QMI8658_ERR_PARAM;
    }

    saved_error = (dev != 0) ? dev->last_error : QMI8658_ERR_PARAM;
    ret = QMI8658_ReadReg(dev, QMI8658_REG_WHO_AM_I, &regs->who_am_i);
    if (ret == QMI8658_OK) {
        ret = QMI8658_ReadReg(dev, QMI8658_REG_CTRL1, &regs->ctrl1);
    }
    if (ret == QMI8658_OK) {
        ret = QMI8658_ReadReg(dev, QMI8658_REG_CTRL2, &regs->ctrl2);
    }
    if (ret == QMI8658_OK) {
        ret = QMI8658_ReadReg(dev, QMI8658_REG_CTRL3, &regs->ctrl3);
    }
    if (ret == QMI8658_OK) {
        ret = QMI8658_ReadReg(dev, QMI8658_REG_CTRL5, &regs->ctrl5);
    }
    if (ret == QMI8658_OK) {
        ret = QMI8658_ReadReg(dev, QMI8658_REG_CTRL7, &regs->ctrl7);
    }
    if (ret == QMI8658_OK) {
        ret = QMI8658_ReadReg(dev, QMI8658_REG_STATUS0, &regs->status0);
    }
    if (dev != 0) {
        dev->last_error = saved_error;
    }
    return ret;
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

int8 QMI8658_GetLastError(const qmi8658_t *dev)
{
    return (dev != 0) ? dev->last_error : QMI8658_ERR_PARAM;
}

u8 QMI8658_GetLastID(const qmi8658_t *dev)
{
    return (dev != 0) ? dev->last_id : 0U;
}
