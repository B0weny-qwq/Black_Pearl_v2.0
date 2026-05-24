#include "ef_iic.h"
#include "STC32G_GPIO.h"
#include "STC32G_I2C.h"
#include "STC32G_NVIC.h"
#include "STC32G_Switch.h"
#include "stc32g.h"

/*
 * STC32G 当前使用纯硬件 IIC 主机命令流：
 * - write: START -> dev_w -> reg -> data... -> STOP
 * - read:  START -> dev_w -> reg -> START -> dev_r -> data... -> STOP
 *
 * 对外继续统一使用 7-bit 设备地址 + 8-bit 寄存器地址语义，
 * 上层不需要直接接触 STC 官方 I2C 命令字。
 */

/* I2CMSST bit6 = MSIF，主机命令完成标志；bit1 = MSACKI，从机 ACK 输入。 */
#define EF_IIC_MSIF_MASK         0x40U
#define EF_IIC_MSACKI_MASK       0x02U
#define EF_IIC_RECOVER_PULSES    16U
#define EF_IIC_RECOVER_DELAY_US  5U
#define EF_IIC_BUS_FREE_DELAY_US 5U

static u16 ef_iic_timeout = EF_IIC_DEFAULT_TIMEOUT;
static u8 ef_iic_ready = 0U;
static u8 ef_iic_pin_group = EF_IIC_PIN_P14_P15;
static u8 ef_iic_speed = EF_IIC_SPEED_100K;
static ef_iic_diag_t ef_iic_last_diag = {
    EF_IIC_OP_NONE,
    EF_IIC_STAGE_IDLE,
    EF_IIC_OK,
    0U,
    0U,
    0U,
    0U,
    0U,
    0U,
    EF_IIC_OK
};

static void ef_iic_config_pins(u8 pin_group);
static int8 ef_iic_bus_recover_internal(void);

static void ef_iic_delay_recover(void)
{
    u8 ticks;

    ticks = (u8)(MAIN_Fosc / 2000000UL);
    while (ticks > 0U) {
        ticks--;
    }
}

static void ef_iic_delay_recover_n(u8 count)
{
    while (count > 0U) {
        ef_iic_delay_recover();
        count--;
    }
}

static void ef_iic_set_sda(u8 pin_group, u8 level)
{
    if (pin_group == EF_IIC_PIN_P14_P15) {
        P14 = (level != 0U) ? 1 : 0;
    } else if (pin_group == EF_IIC_PIN_P24_P25) {
        P24 = (level != 0U) ? 1 : 0;
    } else if (pin_group == EF_IIC_PIN_P76_P77) {
        P76 = (level != 0U) ? 1 : 0;
    } else {
        P33 = (level != 0U) ? 1 : 0;
    }
}

static void ef_iic_set_scl(u8 pin_group, u8 level)
{
    if (pin_group == EF_IIC_PIN_P14_P15) {
        P15 = (level != 0U) ? 1 : 0;
    } else if (pin_group == EF_IIC_PIN_P24_P25) {
        P25 = (level != 0U) ? 1 : 0;
    } else if (pin_group == EF_IIC_PIN_P76_P77) {
        P77 = (level != 0U) ? 1 : 0;
    } else {
        P32 = (level != 0U) ? 1 : 0;
    }
}

static u8 ef_iic_get_sda(u8 pin_group)
{
    if (pin_group == EF_IIC_PIN_P14_P15) {
        return (P14 != 0U) ? 1U : 0U;
    }
    if (pin_group == EF_IIC_PIN_P24_P25) {
        return (P24 != 0U) ? 1U : 0U;
    }
    if (pin_group == EF_IIC_PIN_P76_P77) {
        return (P76 != 0U) ? 1U : 0U;
    }
    return (P33 != 0U) ? 1U : 0U;
}

static u8 ef_iic_get_scl(u8 pin_group)
{
    if (pin_group == EF_IIC_PIN_P14_P15) {
        return (P15 != 0U) ? 1U : 0U;
    }
    if (pin_group == EF_IIC_PIN_P24_P25) {
        return (P25 != 0U) ? 1U : 0U;
    }
    if (pin_group == EF_IIC_PIN_P76_P77) {
        return (P77 != 0U) ? 1U : 0U;
    }
    return (P32 != 0U) ? 1U : 0U;
}

static u8 ef_iic_get_bus_state(void)
{
    u8 state;

    state = 0U;
    if (Get_MSBusy_Status() != 0U) {
        state |= 0x01U;
    }
    if (ef_iic_get_sda(ef_iic_pin_group) == 0U) {
        state |= 0x02U;
    }
    if (ef_iic_get_scl(ef_iic_pin_group) == 0U) {
        state |= 0x04U;
    }
    return state;
}

static void ef_iic_diag_begin(u8 op, u8 dev_addr, u8 reg_addr)
{
    ef_iic_last_diag.op = op;
    ef_iic_last_diag.stage = EF_IIC_STAGE_IDLE;
    ef_iic_last_diag.ret = EF_IIC_OK;
    ef_iic_last_diag.dev_addr = dev_addr;
    ef_iic_last_diag.reg_addr = reg_addr;
    ef_iic_last_diag.msst = I2CMSST;
    ef_iic_last_diag.mscr = I2CMSCR;
    ef_iic_last_diag.bus_state_before = ef_iic_get_bus_state();
    ef_iic_last_diag.bus_state_after = ef_iic_last_diag.bus_state_before;
    ef_iic_last_diag.recover_ret = EF_IIC_OK;
}

static void ef_iic_diag_record(u8 stage, int8 ret)
{
    ef_iic_last_diag.stage = stage;
    ef_iic_last_diag.ret = ret;
    ef_iic_last_diag.msst = I2CMSST;
    ef_iic_last_diag.mscr = I2CMSCR;
    ef_iic_last_diag.bus_state_after = ef_iic_get_bus_state();
}

static void ef_iic_restore_hardware(void)
{
    I2C_InitTypeDef i2c_init;

    I2C_Function(DISABLE);
    I2C_Master();
    I2CMSST = 0x00U;
    I2CMSCR = 0x00U;
    ef_iic_config_pins(ef_iic_pin_group);

    i2c_init.I2C_Mode = I2C_Mode_Master;
    i2c_init.I2C_Enable = ENABLE;
    i2c_init.I2C_MS_WDTA = DISABLE;
    i2c_init.I2C_Speed = ef_iic_speed;
    I2C_Init(&i2c_init);
    I2CMSST = 0x00U;
    I2CMSCR = 0x00U;
    NVIC_I2C_Init(I2C_Mode_Master, DISABLE, Priority_0);
}

static int8 ef_iic_bus_recover_internal(void)
{
    u8 pulse_count;
    u8 bus_state;
    int8 recover_ret;

    ef_iic_last_diag.op = EF_IIC_OP_RECOVER;
    ef_iic_last_diag.stage = EF_IIC_STAGE_RECOVER;
    ef_iic_last_diag.bus_state_before = ef_iic_get_bus_state();
    ef_iic_last_diag.msst = I2CMSST;
    ef_iic_last_diag.mscr = I2CMSCR;

    bus_state = ef_iic_get_bus_state();
    if ((bus_state & 0x06U) == 0U) {
        I2CMSST = 0x00U;
        I2CMSCR = 0x00U;
        ef_iic_restore_hardware();
        recover_ret = (ef_iic_get_bus_state() == 0U) ? EF_IIC_OK : EF_IIC_ERR_BUSY;
        ef_iic_last_diag.ret = recover_ret;
        ef_iic_last_diag.recover_ret = recover_ret;
        ef_iic_last_diag.bus_state_after = ef_iic_get_bus_state();
        ef_iic_last_diag.msst = I2CMSST;
        ef_iic_last_diag.mscr = I2CMSCR;
        return recover_ret;
    }

    I2C_Function(DISABLE);
    I2C_Master();
    I2CMSST = 0x00U;
    I2CMSCR = 0x00U;
    ef_iic_config_pins(ef_iic_pin_group);

    ef_iic_set_sda(ef_iic_pin_group, 1U);
    ef_iic_set_scl(ef_iic_pin_group, 1U);
    ef_iic_delay_recover_n(EF_IIC_RECOVER_DELAY_US);

    for (pulse_count = 0U; pulse_count < EF_IIC_RECOVER_PULSES; pulse_count++) {
        ef_iic_set_scl(ef_iic_pin_group, 0U);
        ef_iic_delay_recover_n(EF_IIC_RECOVER_DELAY_US);
        ef_iic_set_scl(ef_iic_pin_group, 1U);
        ef_iic_delay_recover_n(EF_IIC_RECOVER_DELAY_US);
        if (ef_iic_get_sda(ef_iic_pin_group) != 0U) {
            break;
        }
    }

    ef_iic_set_sda(ef_iic_pin_group, 0U);
    ef_iic_delay_recover_n(EF_IIC_RECOVER_DELAY_US);
    ef_iic_set_scl(ef_iic_pin_group, 1U);
    ef_iic_delay_recover_n(EF_IIC_RECOVER_DELAY_US);
    ef_iic_set_sda(ef_iic_pin_group, 1U);
    ef_iic_delay_recover_n(EF_IIC_RECOVER_DELAY_US);

    ef_iic_restore_hardware();
    recover_ret = (ef_iic_get_bus_state() == 0U) ? EF_IIC_OK : EF_IIC_ERR_BUSY;
    ef_iic_last_diag.ret = recover_ret;
    ef_iic_last_diag.recover_ret = recover_ret;
    ef_iic_last_diag.bus_state_after = ef_iic_get_bus_state();
    ef_iic_last_diag.msst = I2CMSST;
    ef_iic_last_diag.mscr = I2CMSCR;
    return recover_ret;
}

static u16 ef_iic_get_timeout(void)
{
    return (ef_iic_timeout == 0U) ? EF_IIC_DEFAULT_TIMEOUT : ef_iic_timeout;
}

static int8 ef_iic_wait_master_idle(void)
{
    u16 timeout;

    timeout = ef_iic_get_timeout();
    while (Get_MSBusy_Status() != 0U) {
        if (timeout == 0U) {
            return EF_IIC_ERR_BUSY;
        }
        timeout--;
    }

    return EF_IIC_OK;
}

static int8 ef_iic_prepare_transaction(u8 op, u8 dev_addr, u8 reg_addr)
{
    int8 ret;

    ef_iic_diag_begin(op, dev_addr, reg_addr);
    ef_iic_last_diag.stage = EF_IIC_STAGE_PREPARE;

    if (ef_iic_get_bus_state() != 0U) {
        ret = ef_iic_bus_recover_internal();
        ef_iic_last_diag.op = op;
        ef_iic_last_diag.stage = EF_IIC_STAGE_PREPARE;
        ef_iic_last_diag.dev_addr = dev_addr;
        ef_iic_last_diag.reg_addr = reg_addr;
        ef_iic_last_diag.recover_ret = ret;
        if (ret != EF_IIC_OK) {
            ef_iic_diag_record(EF_IIC_STAGE_PREPARE, ret);
            return ret;
        }
    }

    ret = ef_iic_wait_master_idle();
    if (ret != EF_IIC_OK) {
        ef_iic_diag_record(EF_IIC_STAGE_PREPARE, ret);
        return ret;
    }
    if (ef_iic_get_bus_state() != 0U) {
        ret = ef_iic_bus_recover_internal();
        ef_iic_last_diag.op = op;
        ef_iic_last_diag.stage = EF_IIC_STAGE_PREPARE;
        ef_iic_last_diag.dev_addr = dev_addr;
        ef_iic_last_diag.reg_addr = reg_addr;
        ef_iic_last_diag.recover_ret = ret;
        if (ret != EF_IIC_OK) {
            ef_iic_diag_record(EF_IIC_STAGE_PREPARE, ret);
            return ret;
        }
    }

    ef_iic_diag_record(EF_IIC_STAGE_PREPARE, EF_IIC_OK);
    return EF_IIC_OK;
}

static int8 ef_iic_wait_done(void)
{
    u16 timeout;

    timeout = ef_iic_get_timeout();
    while ((I2CMSST & EF_IIC_MSIF_MASK) == 0U) {
        if (timeout == 0U) {
            return EF_IIC_ERR_TIMEOUT;
        }
        timeout--;
    }

    I2CMSST &= (u8)(~EF_IIC_MSIF_MASK);
    return EF_IIC_OK;
}

static int8 ef_iic_start(void)
{
    I2CMSCR = I2C_CMD_START;
    return ef_iic_wait_done();
}

static int8 ef_iic_stop(void)
{
    I2CMSCR = I2C_CMD_STOP;
    return ef_iic_wait_done();
}

static int8 ef_iic_send_data_byte(u8 byte)
{
    I2CTXD = byte;
    I2CMSCR = I2C_CMD_SEND;
    return ef_iic_wait_done();
}

static int8 ef_iic_recv_ack(u8 *nack)
{
    I2CMSCR = I2C_CMD_RACK;
    if (ef_iic_wait_done() != EF_IIC_OK) {
        return EF_IIC_ERR_TIMEOUT;
    }

    if (nack != 0) {
        *nack = ((I2CMSST & EF_IIC_MSACKI_MASK) != 0U) ? 1U : 0U;
    }
    return EF_IIC_OK;
}

static int8 ef_iic_recv_data(u8 *byte)
{
    I2CMSCR = I2C_CMD_RDATA;
    if (ef_iic_wait_done() != EF_IIC_OK) {
        return EF_IIC_ERR_TIMEOUT;
    }

    if (byte != 0) {
        *byte = I2CRXD;
    }
    return EF_IIC_OK;
}

static int8 ef_iic_send_master_ack(u8 nack)
{
    I2CMSST = (nack != 0U) ? 0x01U : 0x00U;
    I2CMSCR = I2C_CMD_SACK;
    return ef_iic_wait_done();
}

static int8 ef_iic_finish_stop(u8 stage)
{
    int8 ret;
    int8 recover_ret;
    u8 op;
    u8 dev_addr;
    u8 reg_addr;

    ret = ef_iic_stop();
    if (ret != EF_IIC_OK) {
        ef_iic_diag_record(stage, ret);
        op = ef_iic_last_diag.op;
        dev_addr = ef_iic_last_diag.dev_addr;
        reg_addr = ef_iic_last_diag.reg_addr;
        recover_ret = ef_iic_bus_recover_internal();
        ef_iic_last_diag.op = op;
        ef_iic_last_diag.dev_addr = dev_addr;
        ef_iic_last_diag.reg_addr = reg_addr;
        ef_iic_last_diag.recover_ret = recover_ret;
        ef_iic_diag_record(stage, ret);
        return ret;
    }

    ef_iic_delay_recover_n(EF_IIC_BUS_FREE_DELAY_US);
    if (ef_iic_get_bus_state() != 0U) {
        op = ef_iic_last_diag.op;
        dev_addr = ef_iic_last_diag.dev_addr;
        reg_addr = ef_iic_last_diag.reg_addr;
        recover_ret = ef_iic_bus_recover_internal();
        ef_iic_last_diag.op = op;
        ef_iic_last_diag.dev_addr = dev_addr;
        ef_iic_last_diag.reg_addr = reg_addr;
        ef_iic_last_diag.recover_ret = recover_ret;
        if (recover_ret != EF_IIC_OK) {
            ef_iic_diag_record(stage, recover_ret);
            return recover_ret;
        }
    }

    ef_iic_diag_record(stage, EF_IIC_OK);
    return EF_IIC_OK;
}

static int8 ef_iic_abort_transaction(u8 stage, int8 ret)
{
    int8 stop_ret;
    int8 recover_ret;
    u8 op;
    u8 dev_addr;
    u8 reg_addr;

    ef_iic_diag_record(stage, ret);
    stop_ret = ef_iic_stop();
    ef_iic_delay_recover_n(EF_IIC_BUS_FREE_DELAY_US);
    if ((stop_ret != EF_IIC_OK) || (ef_iic_get_bus_state() != 0U)) {
        op = ef_iic_last_diag.op;
        dev_addr = ef_iic_last_diag.dev_addr;
        reg_addr = ef_iic_last_diag.reg_addr;
        recover_ret = ef_iic_bus_recover_internal();
        ef_iic_last_diag.op = op;
        ef_iic_last_diag.dev_addr = dev_addr;
        ef_iic_last_diag.reg_addr = reg_addr;
        ef_iic_last_diag.recover_ret = recover_ret;
    }
    ef_iic_diag_record(stage, ret);
    return ret;
}

static int8 ef_iic_abort_nack(u8 stage)
{
    int8 stop_ret;

    stop_ret = ef_iic_finish_stop(stage);
    if (stop_ret != EF_IIC_OK) {
        return stop_ret;
    }

    ef_iic_diag_record(stage, EF_IIC_ERR_NACK);
    return EF_IIC_ERR_NACK;
}

static void ef_iic_config_pins(u8 pin_group)
{
    EAXSFR();

    if (pin_group == EF_IIC_PIN_P14_P15) {
        P1_MODE_OUT_OD(GPIO_Pin_4 | GPIO_Pin_5);
        P1_PULL_UP_ENABLE(GPIO_Pin_4 | GPIO_Pin_5);
        P14 = 1;
        P15 = 1;
        I2C_SW(I2C_P14_P15);
    } else if (pin_group == EF_IIC_PIN_P24_P25) {
        P2_MODE_OUT_OD(GPIO_Pin_4 | GPIO_Pin_5);
        P2_PULL_UP_ENABLE(GPIO_Pin_4 | GPIO_Pin_5);
        P24 = 1;
        P25 = 1;
        I2C_SW(I2C_P24_P25);
    } else if (pin_group == EF_IIC_PIN_P76_P77) {
        P7_MODE_OUT_OD(GPIO_Pin_6 | GPIO_Pin_7);
        P7_PULL_UP_ENABLE(GPIO_Pin_6 | GPIO_Pin_7);
        P76 = 1;
        P77 = 1;
        I2C_SW(I2C_P76_P77);
    } else {
        P3_MODE_OUT_OD(GPIO_Pin_2 | GPIO_Pin_3);
        P3_PULL_UP_ENABLE(GPIO_Pin_2 | GPIO_Pin_3);
        P33 = 1;
        P32 = 1;
        I2C_SW(I2C_P33_P32);
    }
}

static u8 ef_iic_pin_group_is_valid(u8 pin_group)
{
    return (pin_group <= EF_IIC_PIN_P33_P32) ? 1U : 0U;
}

int8 ef_iic_init(const ef_iic_config_t *config)
{
    I2C_InitTypeDef i2c_init;

    if (config == 0) {
        return EF_IIC_ERR_PARAM;
    }
    if (ef_iic_pin_group_is_valid(config->pin_group) == 0U) {
        return EF_IIC_ERR_PARAM;
    }

    ef_iic_ready = 0U;
    ef_iic_timeout = (config->timeout == 0U) ? EF_IIC_DEFAULT_TIMEOUT : config->timeout;
    ef_iic_pin_group = config->pin_group;
    ef_iic_speed = config->speed;

    I2C_Function(DISABLE);
    I2C_Master();
    I2CMSST = 0x00U;
    I2CMSCR = 0x00U;
    ef_iic_config_pins(config->pin_group);

    i2c_init.I2C_Mode = I2C_Mode_Master;
    i2c_init.I2C_Enable = ENABLE;
    i2c_init.I2C_MS_WDTA = DISABLE;
    i2c_init.I2C_Speed = config->speed;
    I2C_Init(&i2c_init);
    NVIC_I2C_Init(I2C_Mode_Master, DISABLE, Priority_0);

    ef_iic_ready = 1U;
    (void)ef_iic_bus_recover_internal();
    return EF_IIC_OK;
}

int8 ef_iic_write_regs(u8 dev_addr, u8 reg_addr, const u8 *buffer, u8 len)
{
    u8 i;
    u8 nack;
    int8 ret;

    if (ef_iic_ready == 0U) {
        return EF_IIC_ERR_NOT_INIT;
    }
    if ((buffer == 0) || (len == 0U)) {
        return EF_IIC_ERR_PARAM;
    }
    if (len > EF_IIC_DMA_MAX_LEN) {
        return EF_IIC_ERR_LENGTH;
    }

    ret = ef_iic_prepare_transaction(EF_IIC_OP_WRITE, dev_addr, reg_addr);
    if (ret != EF_IIC_OK) {
        return ret;
    }

    nack = 0U;

    ret = ef_iic_start();
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_START, ret);
    }
    ef_iic_diag_record(EF_IIC_STAGE_START, EF_IIC_OK);

    ret = ef_iic_send_data_byte((u8)(dev_addr << 1));
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_DEV_W, ret);
    }
    ef_iic_diag_record(EF_IIC_STAGE_DEV_W, EF_IIC_OK);

    ret = ef_iic_recv_ack(&nack);
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_DEV_W_ACK, ret);
    }
    if (nack != 0U) {
        return ef_iic_abort_nack(EF_IIC_STAGE_DEV_W_ACK);
    }
    ef_iic_diag_record(EF_IIC_STAGE_DEV_W_ACK, EF_IIC_OK);

    ret = ef_iic_send_data_byte(reg_addr);
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_REG, ret);
    }
    ef_iic_diag_record(EF_IIC_STAGE_REG, EF_IIC_OK);

    ret = ef_iic_recv_ack(&nack);
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_REG_ACK, ret);
    }
    if (nack != 0U) {
        return ef_iic_abort_nack(EF_IIC_STAGE_REG_ACK);
    }
    ef_iic_diag_record(EF_IIC_STAGE_REG_ACK, EF_IIC_OK);

    for (i = 0U; i < len; i++) {
        ret = ef_iic_send_data_byte(buffer[i]);
        if (ret != EF_IIC_OK) {
            return ef_iic_abort_transaction(EF_IIC_STAGE_DATA, ret);
        }
        ef_iic_diag_record(EF_IIC_STAGE_DATA, EF_IIC_OK);

        ret = ef_iic_recv_ack(&nack);
        if (ret != EF_IIC_OK) {
            return ef_iic_abort_transaction(EF_IIC_STAGE_DATA_ACK, ret);
        }
        if (nack != 0U) {
            return ef_iic_abort_nack(EF_IIC_STAGE_DATA_ACK);
        }
        ef_iic_diag_record(EF_IIC_STAGE_DATA_ACK, EF_IIC_OK);
    }

    return ef_iic_finish_stop(EF_IIC_STAGE_STOP);
}

int8 ef_iic_read_regs(u8 dev_addr, u8 reg_addr, u8 *buffer, u8 len)
{
    u8 i;
    u8 nack;
    int8 ret;

    if (ef_iic_ready == 0U) {
        return EF_IIC_ERR_NOT_INIT;
    }
    if ((buffer == 0) || (len == 0U)) {
        return EF_IIC_ERR_PARAM;
    }
    if (len > EF_IIC_DMA_MAX_LEN) {
        return EF_IIC_ERR_LENGTH;
    }

    ret = ef_iic_prepare_transaction(EF_IIC_OP_READ, dev_addr, reg_addr);
    if (ret != EF_IIC_OK) {
        return ret;
    }

    nack = 0U;

    ret = ef_iic_start();
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_START, ret);
    }
    ef_iic_diag_record(EF_IIC_STAGE_START, EF_IIC_OK);

    ret = ef_iic_send_data_byte((u8)(dev_addr << 1));
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_DEV_W, ret);
    }
    ef_iic_diag_record(EF_IIC_STAGE_DEV_W, EF_IIC_OK);

    ret = ef_iic_recv_ack(&nack);
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_DEV_W_ACK, ret);
    }
    if (nack != 0U) {
        return ef_iic_abort_nack(EF_IIC_STAGE_DEV_W_ACK);
    }
    ef_iic_diag_record(EF_IIC_STAGE_DEV_W_ACK, EF_IIC_OK);

    ret = ef_iic_send_data_byte(reg_addr);
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_REG, ret);
    }
    ef_iic_diag_record(EF_IIC_STAGE_REG, EF_IIC_OK);

    ret = ef_iic_recv_ack(&nack);
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_REG_ACK, ret);
    }
    if (nack != 0U) {
        return ef_iic_abort_nack(EF_IIC_STAGE_REG_ACK);
    }
    ef_iic_diag_record(EF_IIC_STAGE_REG_ACK, EF_IIC_OK);

    ret = ef_iic_start();
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_RESTART, ret);
    }
    ef_iic_diag_record(EF_IIC_STAGE_RESTART, EF_IIC_OK);

    ret = ef_iic_send_data_byte((u8)((dev_addr << 1) | 0x01U));
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_DEV_R, ret);
    }
    ef_iic_diag_record(EF_IIC_STAGE_DEV_R, EF_IIC_OK);

    ret = ef_iic_recv_ack(&nack);
    if (ret != EF_IIC_OK) {
        return ef_iic_abort_transaction(EF_IIC_STAGE_DEV_R_ACK, ret);
    }
    if (nack != 0U) {
        return ef_iic_abort_nack(EF_IIC_STAGE_DEV_R_ACK);
    }
    ef_iic_diag_record(EF_IIC_STAGE_DEV_R_ACK, EF_IIC_OK);

    for (i = 0U; i < len; i++) {
        ret = ef_iic_recv_data(&buffer[i]);
        if (ret != EF_IIC_OK) {
            return ef_iic_abort_transaction(EF_IIC_STAGE_READ_DATA, ret);
        }
        ef_iic_diag_record(EF_IIC_STAGE_READ_DATA, EF_IIC_OK);

        ret = ef_iic_send_master_ack((((u8)(i + 1U) < len) ? 0U : 1U));
        if (ret != EF_IIC_OK) {
            return ef_iic_abort_transaction(EF_IIC_STAGE_MASTER_ACK, ret);
        }
        ef_iic_diag_record(EF_IIC_STAGE_MASTER_ACK, EF_IIC_OK);
    }

    return ef_iic_finish_stop(EF_IIC_STAGE_STOP);
}

int8 ef_iic_bus_recover(void)
{
    if (ef_iic_ready == 0U) {
        return EF_IIC_ERR_NOT_INIT;
    }
    return ef_iic_bus_recover_internal();
}

int8 ef_iic_get_last_diag(ef_iic_diag_t *diag)
{
    if (diag == 0) {
        return EF_IIC_ERR_PARAM;
    }

    *diag = ef_iic_last_diag;
    return EF_IIC_OK;
}

u8 ef_iic_is_busy(void)
{
    return (Get_MSBusy_Status() != 0U) ? 1U : 0U;
}
