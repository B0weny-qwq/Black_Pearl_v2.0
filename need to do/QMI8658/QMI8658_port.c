#include "..\..\..\User\Config.h"
#include "QMI8658_port.h"
#include "..\..\..\Driver\inc\STC32G_Delay.h"
#include "..\..\..\Driver\inc\STC32G_GPIO.h"
#include "..\..\..\Driver\inc\STC32G_I2C.h"
#include "..\..\..\Driver\inc\STC32G_NVIC.h"
#include "..\..\..\Driver\inc\STC32G_Switch.h"

#define QMI8658_I2C_MSIF_MASK          0x40U
#define QMI8658_I2C_MSACKI_MASK        0x02U
#define QMI8658_I2C_WAIT_DONE_TIMEOUT  60000U

static u8 QMI8658Port_BusState(void);
static u8 QMI8658Port_HardWaitDone(void);
static u8 QMI8658Port_HardStart(void);
static u8 QMI8658Port_HardSendData(u8 dat);
static u8 QMI8658Port_HardRecvAck(u8 *nack);
static u8 QMI8658Port_HardRecvData(u8 *dat);
static u8 QMI8658Port_HardSendMasterAck(u8 nack);
static u8 QMI8658Port_HardStop(void);
static void QMI8658Port_ConfigPins(void)
{
    EAXSFR();
    P1_MODE_OUT_OD(GPIO_Pin_4 | GPIO_Pin_5);
    P1_PULL_UP_ENABLE(GPIO_Pin_4 | GPIO_Pin_5);
    /* Release SDA/SCL before switching the I2C function onto the pins. */
    P14 = 1;
    P15 = 1;
    I2C_SW(I2C_P14_P15);
}

static void QMI8658Port_HardI2cRestore(void)
{
    I2C_InitTypeDef i2c_init;

    I2C_Function(DISABLE);
    I2C_Master();
    I2CMSST = 0x00U;
    I2CMSCR = 0x00U;
    QMI8658Port_ConfigPins();
    i2c_init.I2C_Mode = I2C_Mode_Master;
    i2c_init.I2C_Enable = ENABLE;
    i2c_init.I2C_MS_WDTA = DISABLE;
    i2c_init.I2C_Speed = SENSOR_I2C_SPEED_CFG;
    I2C_Init(&i2c_init);
    I2CMSST = 0x00U;
    I2CMSCR = 0x00U;
    NVIC_I2C_Init(I2C_Mode_Master, DISABLE, Priority_0);
}

#if QMI8658_I2C_USE_SOFT
static void QMI8658Port_SoftEnter(void)
{
    I2C_Function(DISABLE);
    I2C_Master();
    QMI8658Port_ConfigPins();
    P14 = 1;
    P15 = 1;
}

static void QMI8658Port_SoftLeave(void)
{
    QMI8658Port_HardI2cRestore();
}

static void QMI8658Port_SoftDelay(void)
{
    if (QMI8658_SOFT_I2C_DELAY_US != 0U) {
        QMI8658Port_DelayUs(QMI8658_SOFT_I2C_DELAY_US);
    }
}

static void QMI8658Port_SoftStart(void)
{
    P14 = 1;
    P15 = 1;
    QMI8658Port_SoftDelay();
    P14 = 0;
    QMI8658Port_SoftDelay();
    P15 = 0;
    QMI8658Port_SoftDelay();
}

static void QMI8658Port_SoftStop(void)
{
    P14 = 0;
    QMI8658Port_SoftDelay();
    P15 = 1;
    QMI8658Port_SoftDelay();
    P14 = 1;
    QMI8658Port_SoftDelay();
}

static u8 QMI8658Port_SoftWriteByte(u8 value)
{
    u8 bit_mask;

    for (bit_mask = 0x80U; bit_mask != 0U; bit_mask >>= 1) {
        P14 = ((value & bit_mask) != 0U) ? 1 : 0;
        QMI8658Port_SoftDelay();
        P15 = 1;
        QMI8658Port_SoftDelay();
        P15 = 0;
        QMI8658Port_SoftDelay();
    }

    P14 = 1;
    QMI8658Port_SoftDelay();
    P15 = 1;
    QMI8658Port_SoftDelay();
    bit_mask = (P14 != 0U) ? 1U : 0U;
    P15 = 0;
    QMI8658Port_SoftDelay();
    return bit_mask;
}

static u8 QMI8658Port_SoftReadByte(u8 send_ack)
{
    u8 value;
    u8 i;

    value = 0U;
    P14 = 1;
    for (i = 0; i < 8U; i++) {
        value <<= 1;
        P15 = 1;
        QMI8658Port_SoftDelay();
        if (P14 != 0U) {
            value |= 0x01U;
        }
        P15 = 0;
        QMI8658Port_SoftDelay();
    }

    P14 = (send_ack != 0U) ? 0 : 1;
    QMI8658Port_SoftDelay();
    P15 = 1;
    QMI8658Port_SoftDelay();
    P15 = 0;
    QMI8658Port_SoftDelay();
    P14 = 1;
    return value;
}
#endif

s8 QMI8658Port_Init(void)
{
    QMI8658Port_ConfigPins();
    QMI8658Port_HardI2cRestore();
    if (QMI8658Port_BusNeedsRecover() != 0U) {
        QMI8658Port_BusRecover();
    }
    return SUCCESS;
}

char *QMI8658Port_BackendName(void)
{
#if QMI8658_I2C_USE_SOFT
    return "soft";
#else
    return "hard";
#endif
}

void QMI8658Port_DelayMs(u16 ms)
{
    delay_ms(ms);
}

void QMI8658Port_DelayUs(u16 us)
{
    u8 dly;

    while (us > 0U) {
        dly = (u8)(MAIN_Fosc / 2000000UL);
        while (--dly) {
        }
        us--;
    }
}

static u8 QMI8658Port_HardWaitDone(void)
{
    u16 timeout;

    timeout = QMI8658_I2C_WAIT_DONE_TIMEOUT;
    while ((I2CMSST & QMI8658_I2C_MSIF_MASK) == 0U) {
        if (timeout == 0U) {
            return 1U;
        }
        timeout--;
    }
    I2CMSST &= (u8)(~QMI8658_I2C_MSIF_MASK);
    return 0U;
}

static u8 QMI8658Port_HardStart(void)
{
    I2CMSCR = I2C_CMD_START;
    return QMI8658Port_HardWaitDone();
}

static u8 QMI8658Port_HardSendData(u8 dat)
{
    I2CTXD = dat;
    I2CMSCR = I2C_CMD_SEND;
    return QMI8658Port_HardWaitDone();
}

static u8 QMI8658Port_HardRecvAck(u8 *nack)
{
    I2CMSCR = I2C_CMD_RACK;
    if (QMI8658Port_HardWaitDone() != 0U) {
        return 1U;
    }

    if (nack != NULL) {
        *nack = ((I2CMSST & QMI8658_I2C_MSACKI_MASK) != 0U) ? 1U : 0U;
    }
    return 0U;
}

static u8 QMI8658Port_HardRecvData(u8 *dat)
{
    I2CMSCR = I2C_CMD_RDATA;
    if (QMI8658Port_HardWaitDone() != 0U) {
        return 1U;
    }

    if (dat != NULL) {
        *dat = I2CRXD;
    }
    return 0U;
}

static u8 QMI8658Port_HardSendMasterAck(u8 nack)
{
    I2CMSST = (nack != 0U) ? 0x01U : 0x00U;
    I2CMSCR = I2C_CMD_SACK;
    return QMI8658Port_HardWaitDone();
}

static u8 QMI8658Port_HardStop(void)
{
    I2CMSCR = I2C_CMD_STOP;
    return QMI8658Port_HardWaitDone();
}

static u8 QMI8658Port_BusState(void)
{
    u8 state;

    state = 0U;
    if (Get_MSBusy_Status() != 0U) {
        state |= 0x01U;
    }
    if (P14 == 0) {
        state |= 0x02U;
    }
    if (P15 == 0) {
        state |= 0x04U;
    }
    return state;
}

u8 QMI8658Port_BusNeedsRecover(void)
{
    return (QMI8658Port_BusState() != 0U) ? 1U : 0U;
}

void QMI8658Port_BusRecover(void)
{
    u8 i;
    u8 state_before;
    u8 state_after_gpio;
    u8 state_after_restore;

    state_before = QMI8658Port_BusState();
    LOGW("I2C", "recover start state=0x%02X msst=0x%02X sda=%u scl=%u",
         state_before, I2CMSST, (u16)P14, (u16)P15);

    if ((state_before & 0x06U) == 0U) {
        I2CMSST = 0x00U;
        I2CMSCR = 0x00U;
        QMI8658Port_HardI2cRestore();
        state_after_restore = QMI8658Port_BusState();
        LOGW("I2C", "recover ctrl-only state=0x%02X msst=0x%02X sda=%u scl=%u",
             state_after_restore, I2CMSST, (u16)P14, (u16)P15);
        return;
    }

    I2C_Function(DISABLE);
    I2C_Master();
    I2CMSST = 0x00U;
    I2CMSCR = 0x00U;
    QMI8658Port_ConfigPins();

    P14 = 1;
    P15 = 1;
    QMI8658Port_DelayUs(5U);
    for (i = 0; i < 9U; i++) {
        P15 = 0;
        QMI8658Port_DelayUs(5U);
        P15 = 1;
        QMI8658Port_DelayUs(5U);
        if (P14 != 0U) {
            break;
        }
    }

    P14 = 0;
    QMI8658Port_DelayUs(5U);
    P15 = 1;
    QMI8658Port_DelayUs(5U);
    P14 = 1;
    QMI8658Port_DelayUs(5U);

    state_after_gpio = QMI8658Port_BusState();
    LOGW("I2C", "recover gpio-done state=0x%02X msst=0x%02X sda=%u scl=%u",
         state_after_gpio, I2CMSST, (u16)P14, (u16)P15);

    QMI8658Port_HardI2cRestore();
    state_after_restore = QMI8658Port_BusState();
    LOGW("I2C", "recover done state=0x%02X msst=0x%02X sda=%u scl=%u",
         state_after_restore, I2CMSST, (u16)P14, (u16)P15);
}

u8 QMI8658Port_WriteReg(u8 addr, u8 reg_addr, u8 reg_val, u8 *err_code)
{
    u8 nack;

    nack = 0U;
    if (err_code != NULL) {
        *err_code = QMI8658_I2C_OK;
    }

#if QMI8658_I2C_USE_SOFT
    if (QMI8658Port_BusNeedsRecover() != 0U) {
        QMI8658Port_BusRecover();
        if (QMI8658Port_BusNeedsRecover() != 0U) {
            if (err_code != NULL) {
                *err_code = QMI8658_I2C_ERR_BUSY;
            }
            return 1U;
        }
    }
    QMI8658Port_SoftEnter();
    QMI8658Port_SoftStart();
    if (QMI8658Port_SoftWriteByte(QMI8658_I2C_WRITE(addr)) != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_DEVW_NACK;
        }
        QMI8658Port_SoftStop();
        QMI8658Port_SoftLeave();
        return 1U;
    }
    if (QMI8658Port_SoftWriteByte(reg_addr) != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_REG_NACK;
        }
        QMI8658Port_SoftStop();
        QMI8658Port_SoftLeave();
        return 1U;
    }
    if (QMI8658Port_SoftWriteByte(reg_val) != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_DATA_NACK;
        }
        QMI8658Port_SoftStop();
        QMI8658Port_SoftLeave();
        return 1U;
    }
    QMI8658Port_SoftStop();
    QMI8658Port_SoftLeave();
    return 0U;
#else
    if (QMI8658Port_HardStart() != 0U) {
        goto hard_write_busy;
    }
    if (QMI8658Port_HardSendData(QMI8658_I2C_WRITE(addr)) != 0U) {
        goto hard_write_busy;
    }
    if (QMI8658Port_HardRecvAck(&nack) != 0U) {
        goto hard_write_busy;
    }
    if (nack != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_DEVW_NACK;
        }
        (void)QMI8658Port_HardStop();
        return 1U;
    }
    if (QMI8658Port_HardSendData(reg_addr) != 0U) {
        goto hard_write_busy;
    }
    if (QMI8658Port_HardRecvAck(&nack) != 0U) {
        goto hard_write_busy;
    }
    if (nack != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_REG_NACK;
        }
        (void)QMI8658Port_HardStop();
        return 1U;
    }
    if (QMI8658Port_HardSendData(reg_val) != 0U) {
        goto hard_write_busy;
    }
    if (QMI8658Port_HardRecvAck(&nack) != 0U) {
        goto hard_write_busy;
    }
    if (nack != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_DATA_NACK;
        }
        (void)QMI8658Port_HardStop();
        return 1U;
    }
    if (QMI8658Port_HardStop() != 0U) {
        goto hard_write_busy;
    }
    return 0U;

hard_write_busy:
    LOGW("I2C", "write timeout/busy addr=0x%02X reg=0x%02X msst=0x%02X mscr=0x%02X",
         QMI8658_I2C_WRITE(addr), reg_addr, I2CMSST, I2CMSCR);
    QMI8658Port_BusRecover();
    if (err_code != NULL) {
        *err_code = QMI8658_I2C_ERR_BUSY;
    }
    return 1U;
#endif
}

u8 QMI8658Port_ReadN(u8 addr, u8 start_reg, u8 *buf, u8 len, u8 *err_code)
{
    u8 i;
    u8 nack;

    i = 0U;
    nack = 0U;

    if ((buf == NULL) || (len == 0U)) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_PARAM;
        }
        return 1U;
    }

    if (err_code != NULL) {
        *err_code = QMI8658_I2C_OK;
    }

#if QMI8658_I2C_USE_SOFT
    if (QMI8658Port_BusNeedsRecover() != 0U) {
        QMI8658Port_BusRecover();
        if (QMI8658Port_BusNeedsRecover() != 0U) {
            if (err_code != NULL) {
                *err_code = QMI8658_I2C_ERR_BUSY;
            }
            return 1U;
        }
    }
    QMI8658Port_SoftEnter();
    QMI8658Port_SoftStart();
    if (QMI8658Port_SoftWriteByte(QMI8658_I2C_WRITE(addr)) != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_DEVW_NACK;
        }
        QMI8658Port_SoftStop();
        QMI8658Port_SoftLeave();
        return 1U;
    }
    if (QMI8658Port_SoftWriteByte(start_reg) != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_REG_NACK;
        }
        QMI8658Port_SoftStop();
        QMI8658Port_SoftLeave();
        return 1U;
    }
    QMI8658Port_SoftStart();
    if (QMI8658Port_SoftWriteByte(QMI8658_I2C_READ(addr)) != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_DEVR_NACK;
        }
        QMI8658Port_SoftStop();
        QMI8658Port_SoftLeave();
        return 1U;
    }
    for (i = 0U; i < len; i++) {
        buf[i] = QMI8658Port_SoftReadByte(((u8)(i + 1U) < len) ? 1U : 0U);
    }
    QMI8658Port_SoftStop();
    QMI8658Port_SoftLeave();
    return 0U;
#else
    if (QMI8658Port_HardStart() != 0U) {
        goto hard_read_busy;
    }
    if (QMI8658Port_HardSendData(QMI8658_I2C_WRITE(addr)) != 0U) {
        goto hard_read_busy;
    }
    if (QMI8658Port_HardRecvAck(&nack) != 0U) {
        goto hard_read_busy;
    }
    if (nack != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_DEVW_NACK;
        }
        (void)QMI8658Port_HardStop();
        return 1U;
    }
    if (QMI8658Port_HardSendData(start_reg) != 0U) {
        goto hard_read_busy;
    }
    if (QMI8658Port_HardRecvAck(&nack) != 0U) {
        goto hard_read_busy;
    }
    if (nack != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_REG_NACK;
        }
        (void)QMI8658Port_HardStop();
        return 1U;
    }
    if (QMI8658Port_HardStart() != 0U) {
        goto hard_read_busy;
    }
    if (QMI8658Port_HardSendData(QMI8658_I2C_READ(addr)) != 0U) {
        goto hard_read_busy;
    }
    if (QMI8658Port_HardRecvAck(&nack) != 0U) {
        goto hard_read_busy;
    }
    if (nack != 0U) {
        if (err_code != NULL) {
            *err_code = QMI8658_I2C_ERR_DEVR_NACK;
        }
        (void)QMI8658Port_HardStop();
        return 1U;
    }

    for (i = 0U; i < len; i++) {
        if (QMI8658Port_HardRecvData(&buf[i]) != 0U) {
            goto hard_read_busy;
        }
        if (QMI8658Port_HardSendMasterAck((((u8)(i + 1U) < len) ? 0U : 1U)) != 0U) {
            goto hard_read_busy;
        }
    }
    if (QMI8658Port_HardStop() != 0U) {
        goto hard_read_busy;
    }
    return 0U;

hard_read_busy:
    LOGW("I2C", "read timeout/busy addr=0x%02X reg=0x%02X len=%u msst=0x%02X mscr=0x%02X",
         QMI8658_I2C_WRITE(addr), start_reg, (u16)len, I2CMSST, I2CMSCR);
    QMI8658Port_BusRecover();
    if (err_code != NULL) {
        *err_code = QMI8658_I2C_ERR_BUSY;
    }
    return 1U;
#endif
}
