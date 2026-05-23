#include "QMC6309.h"
#include "QMC6309_port.h"
#include "Filter.h"
#include "Log.h"

#define QMC6309_I2C_WRITE(addr)        ((addr) << 1)
#define QMC6309_I2C_READ(addr)         (((addr) << 1) | 1U)

#define QMC6309_REG_CHIP_ID            0x00
#define QMC6309_REG_DATA_START         0x01
#define QMC6309_REG_CONTROL_1          0x0A
#define QMC6309_REG_CONTROL_2          0x0B

#define QMC6309_CTRL1_STANDBY          0x00
#define QMC6309_CTRL1_ACTIVE           0x1B
#define QMC6309_CTRL2_INIT             0x10

#define QMC6309_PWR_UP_DELAY_MS        1000U

static u8 qmc6309_addr = QMC6309_I2C_ADDR_PRIMARY;

static s8 QMC6309_WriteReg(u8 reg, u8 dat);
static u8 QMC6309_ReadReg(u8 reg);
static s8 QMC6309_Reset(void);
static void QMC6309_Delay_ms(u16 ms);
static u8 QMC6309_ProbeAddr(u8 addr);
static u8 QMC6309_EnsureBusIdle(void);

static u8 QMC6309_EnsureBusIdle(void)
{
    if (QMC6309Port_BusNeedsRecover() == 0U) {
        return 0U;
    }

    LOGW("MAG", "bus not idle, try recover backend=%s", QMC6309Port_BackendName());
    QMC6309Port_BusRecover();
    if (QMC6309Port_BusNeedsRecover() != 0U) {
        LOGE("MAG", "bus recover failed backend=%s", QMC6309Port_BackendName());
        return 1U;
    }

    return 0U;
}

static s8 QMC6309_WriteReg(u8 reg, u8 dat)
{
    u8 err_code;

    if (QMC6309_EnsureBusIdle() != 0U) {
        LOGW("MAG", "WriteReg bus busy reg=0x%02X", reg);
        return -1;
    }

    err_code = QMC6309_I2C_OK;
    if (QMC6309Port_WriteReg(qmc6309_addr, reg, dat, &err_code) != 0U) {
        LOGW("MAG", "WriteReg fail addr=0x%02X reg=0x%02X val=0x%02X err=%u",
             qmc6309_addr, reg, dat, err_code);
        return -1;
    }

    LOGI("MAG", "WriteReg OK reg=0x%02X val=0x%02X", reg, dat);
    return 0;
}

static u8 QMC6309_ReadReg(u8 reg)
{
    u8 err_code;
    u8 reg_value;

    if (QMC6309_EnsureBusIdle() != 0U) {
        LOGW("MAG", "ReadReg bus busy reg=0x%02X", reg);
        return 0xFFU;
    }

    reg_value = 0xFFU;
    err_code = QMC6309_I2C_OK;
    if (QMC6309Port_ReadN(qmc6309_addr, reg, &reg_value, 1U, &err_code) != 0U) {
        LOGW("MAG", "ReadReg fail addr=0x%02X reg=0x%02X err=%u",
             qmc6309_addr, reg, err_code);
        return 0xFFU;
    }

    return reg_value;
}

static void QMC6309_Delay_ms(u16 ms)
{
    QMC6309Port_DelayMs(ms);
}

static u8 QMC6309_ProbeAddr(u8 addr)
{
    u8 err_code;
    u8 chip_id;

    if (QMC6309_EnsureBusIdle() != 0U) {
        LOGW("MAG", "Probe bus busy addr=0x%02X", addr);
        return 1U;
    }

    err_code = QMC6309_I2C_OK;
    chip_id = 0xFFU;
    if (QMC6309Port_ReadN(addr, QMC6309_REG_CHIP_ID, &chip_id, 1U, &err_code) != 0U) {
        LOGI("MAG", "Probe addr=0x%02X write=0x%02X: NACK err=%u",
             addr, QMC6309_I2C_WRITE(addr), err_code);
        return 1U;
    }

    qmc6309_addr = addr;
    LOGI("MAG", "Probe addr=0x%02X write=0x%02X: ACK", addr, QMC6309_I2C_WRITE(addr));
    return 0U;
}

static s8 QMC6309_Reset(void)
{
    if (QMC6309_WriteReg(QMC6309_REG_CONTROL_2, 0x80U) != 0) {
        LOGE("MAG", "soft reset enter failed");
        return -1;
    }
    QMC6309_Delay_ms(5U);

    if (QMC6309_WriteReg(QMC6309_REG_CONTROL_2, 0x00U) != 0) {
        LOGE("MAG", "soft reset exit failed");
        return -1;
    }
    QMC6309_Delay_ms(10U);
    return 0;
}

s8 QMC6309_Wait_Ready(u16 timeout_ms)
{
    u16 i;
    u8 id;

    for (i = 0U; i < timeout_ms; i = (u16)(i + 10U)) {
        id = QMC6309_ReadReg(QMC6309_REG_CHIP_ID);
        if (id == QMC6309_CHIP_ID_VALUE) {
            LOGI("MAG", "ready id=0x%02X in %ums", id, i);
            return 0;
        }
        QMC6309_Delay_ms(10U);
    }

    LOGE("MAG", "wait ready TIMEOUT");
    return -1;
}

u8 QMC6309_ReadID(void)
{
    u8 id;

    id = QMC6309_ReadReg(QMC6309_REG_CHIP_ID);
    if (id != QMC6309_CHIP_ID_VALUE) {
        LOGE("MAG", "ID mismatch got=0x%02X exp=0x%02X", id, QMC6309_CHIP_ID_VALUE);
    }
    return id;
}

void QMC6309_DumpRegs(u8 target_addr)
{
    u8 old_addr;
    u8 reg00;
    u8 reg0A;
    u8 reg0B;

    old_addr = qmc6309_addr;
    qmc6309_addr = target_addr;

    reg00 = QMC6309_ReadReg(QMC6309_REG_CHIP_ID);
    reg0A = QMC6309_ReadReg(QMC6309_REG_CONTROL_1);
    reg0B = QMC6309_ReadReg(QMC6309_REG_CONTROL_2);

    LOGI("MAG", "Dump addr=0x%02X write=0x%02X read=0x%02X reg00=0x%02X reg0A=0x%02X reg0B=0x%02X",
         target_addr,
         QMC6309_I2C_WRITE(target_addr),
         QMC6309_I2C_READ(target_addr),
         reg00,
         reg0A,
         reg0B);

    qmc6309_addr = old_addr;
}

s8 QMC6309_ReadXYZ(int16 *x, int16 *y, int16 *z)
{
    u8 err_code;
    u8 raw[6];

    if ((x == NULL) || (y == NULL) || (z == NULL)) {
        return -1;
    }

    if (QMC6309_EnsureBusIdle() != 0U) {
        LOGW("MAG", "ReadXYZ bus busy");
        return -1;
    }

    err_code = QMC6309_I2C_OK;
    if (QMC6309Port_ReadN(qmc6309_addr, QMC6309_REG_DATA_START, raw, 6U, &err_code) != 0U) {
        LOGW("MAG", "ReadXYZ fail addr=0x%02X err=%u", qmc6309_addr, err_code);
        return -1;
    }

    *x = (int16)(((u16)raw[1] << 8) | raw[0]);
    *y = (int16)(((u16)raw[3] << 8) | raw[2]);
    *z = (int16)(((u16)raw[5] << 8) | raw[4]);

    if ((raw[0] == 0xFFU) && (raw[1] == 0xFFU) &&
        (raw[2] == 0xFFU) && (raw[3] == 0xFFU) &&
        (raw[4] == 0xFFU) && (raw[5] == 0xFFU)) {
        LOGW("MAG", "XYZ all 0xFF, data invalid");
        return -1;
    }

    if ((*x == 0) && (*y == 0) && (*z == 0)) {
        LOGW("MAG", "XYZ all zero, data may be invalid");
        return -1;
    }

    return 0;
}

s8 QMC6309_ReadXYZFiltered(int16 *x, int16 *y, int16 *z)
{
    int16 mx;
    int16 my;
    int16 mz;

    if ((x == NULL) || (y == NULL) || (z == NULL)) {
        return -1;
    }

    if (QMC6309_ReadXYZ(&mx, &my, &mz) != 0) {
        return -1;
    }

    return Filter_MagLowPass(mx, my, mz, x, y, z);
}

s8 QMC6309_SetODR(u8 odr)
{
    if (QMC6309_WriteReg(QMC6309_REG_CONTROL_2, odr) != 0) {
        LOGE("MAG", "SetODR WR fail odr=0x%02X", odr);
        return -1;
    }

    LOGI("MAG", "SetODR=0x%02X", odr);
    return 0;
}

s8 QMC6309_Init(void)
{
    u8 id;
    u8 retry;
    u8 ctrl1_readback;
    u8 ctrl2_readback;
    u16 speed_cfg;
    u16 retry_num;
    u32 bus_hz;

    speed_cfg = (u16)QMC6309_I2C_SPEED_CFG;
    bus_hz = ((u32)MAIN_Fosc / 2UL) / ((((u32)speed_cfg) * 2UL) + 4UL);

    QMC6309Port_Init();

    LOGI("MAG", "========== QMC6309 Init Start ==========");
    LOGI("MAG", "I2C route=P1.4/P1.5 backend=%s speed_cfg=%u (~%luHz)",
         QMC6309Port_BackendName(), speed_cfg, bus_hz);
    LOGI("MAG", "probe order primary=0x%02X alt=0x%02X",
         QMC6309_I2C_ADDR_PRIMARY, QMC6309_I2C_ADDR_ALT);

    if (QMC6309_EnsureBusIdle() != 0U) {
        LOGE("MAG", "bus idle check failed before probe");
        return -1;
    }

    for (retry = 0U; retry < 3U; retry++) {
        retry_num = (u16)retry + 1U;
        if (QMC6309_ProbeAddr(QMC6309_I2C_ADDR_PRIMARY) == 0U) {
            break;
        }

        LOGW("MAG", "primary addr no ACK, try alt");
        if (QMC6309_ProbeAddr(QMC6309_I2C_ADDR_ALT) == 0U) {
            break;
        }

        LOGW("MAG", "addr probe fail try=%u, wait 200ms retry", retry_num);
        QMC6309_Delay_ms(200U);
    }

    if (retry >= 3U) {
        LOGE("MAG", "no device found after 3 tries");
        return -1;
    }

    LOGI("MAG", "selected addr=0x%02X write=0x%02X read=0x%02X",
         qmc6309_addr,
         QMC6309_I2C_WRITE(qmc6309_addr),
         QMC6309_I2C_READ(qmc6309_addr));

    if (QMC6309_Wait_Ready(QMC6309_PWR_UP_DELAY_MS) != 0) {
        LOGE("MAG", "wait ready failed");
        return -1;
    }

    id = QMC6309_ReadID();
    if (id != QMC6309_CHIP_ID_VALUE) {
        LOGE("MAG", "CHIP_ID error got=0x%02X exp=0x%02X", id, QMC6309_CHIP_ID_VALUE);
        return -1;
    }
    LOGI("MAG", "CHIP_ID=0x%02X OK", id);

    if (QMC6309_Reset() != 0) {
        return -1;
    }

    if (QMC6309_WriteReg(QMC6309_REG_CONTROL_1, QMC6309_CTRL1_STANDBY) != 0) {
        return -1;
    }
    if (QMC6309_WriteReg(QMC6309_REG_CONTROL_2, QMC6309_CTRL2_INIT) != 0) {
        return -1;
    }
    if (QMC6309_WriteReg(QMC6309_REG_CONTROL_1, QMC6309_CTRL1_ACTIVE) != 0) {
        return -1;
    }
    QMC6309_Delay_ms(2U);

    ctrl1_readback = QMC6309_ReadReg(QMC6309_REG_CONTROL_1);
    ctrl2_readback = QMC6309_ReadReg(QMC6309_REG_CONTROL_2);
    LOGI("MAG", "CTRL1=0x%02X CTRL2=0x%02X", ctrl1_readback, ctrl2_readback);

    if ((ctrl1_readback == 0xFFU) || (ctrl2_readback == 0xFFU)) {
        LOGE("MAG", "register readback error");
        return -1;
    }

    if (ctrl1_readback != QMC6309_CTRL1_ACTIVE) {
        LOGW("MAG", "CTRL1 mismatch read=0x%02X exp=0x%02X",
             ctrl1_readback, QMC6309_CTRL1_ACTIVE);
    }
    if (ctrl2_readback != QMC6309_CTRL2_INIT) {
        LOGW("MAG", "CTRL2 mismatch read=0x%02X exp=0x%02X",
             ctrl2_readback, QMC6309_CTRL2_INIT);
    }

    Filter_ResetMagLowPass();

    LOGI("MAG", "========== QMC6309 Init OK ==========");
    return 0;
}
