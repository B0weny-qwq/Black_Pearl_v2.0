#include "board_mag.h"
#include "QMC6309.h"
#include "board_sensor_bus.h"

typedef struct
{
    qmc6309_t chip;
    u8 ready;
    int8 init_error;
    ef_iic_diag_t init_iic_diag;
} board_mag_context_t;

#define BOARD_MAG_RECOVER_DELAY_MS 20U

static u8 board_mag_normalize_addr(u8 addr)
{
    /*
     * 兼容旧资料里偶尔出现的 8-bit 地址字节写法，同时保持当前 0x7C/0x0C
     * 旧工程地址值不变。ef_iic 对外统一使用“未左移的设备地址”语义。
     */
    if ((addr == 0xF8U) || (addr == 0xF9U) ||
        (addr == 0x18U) || (addr == 0x19U)) {
        return (u8)(addr >> 1);
    }

    return addr;
}

static int8 board_mag_write_reg(u8 addr, u8 reg, u8 value)
{
    addr = board_mag_normalize_addr(addr);
    return board_sensor_bus_write_reg(addr, reg, value);
}

static int8 board_mag_read_regs(u8 addr, u8 start_reg, u8 *buf, u8 len)
{
    return board_sensor_bus_read_regs(board_mag_normalize_addr(addr), start_reg, buf, len);
}

static void board_mag_delay_ms(u16 ms)
{
    board_sensor_bus_delay_ms(ms);
}

static board_mag_context_t board_mag_ctx = { {0}, 0U, QMC6309_OK, {0} };

static void board_mag_capture_iic_diag(void)
{
    if (board_sensor_bus_get_diag(&board_mag_ctx.init_iic_diag) != 0) {
        board_mag_ctx.init_iic_diag.op = 0U;
        board_mag_ctx.init_iic_diag.stage = 0U;
        board_mag_ctx.init_iic_diag.ret = 0;
        board_mag_ctx.init_iic_diag.dev_addr = 0U;
        board_mag_ctx.init_iic_diag.reg_addr = 0U;
        board_mag_ctx.init_iic_diag.msst = 0U;
        board_mag_ctx.init_iic_diag.mscr = 0U;
        board_mag_ctx.init_iic_diag.bus_state_before = 0U;
        board_mag_ctx.init_iic_diag.bus_state_after = 0U;
        board_mag_ctx.init_iic_diag.recover_ret = 0;
    }
}

static int8 board_mag_reinit_after_error(void)
{
    int8 ret;
    ef_iic_diag_t recover_diag;

    board_mag_ctx.ready = 0U;
    (void)board_sensor_bus_recover();
    if (board_sensor_bus_get_diag(&recover_diag) == 0) {
        board_mag_ctx.init_iic_diag = recover_diag;
    }
    board_sensor_bus_delay_ms(BOARD_MAG_RECOVER_DELAY_MS);
    ret = QMC6309_Init(&board_mag_ctx.chip);
    if (ret == QMC6309_OK) {
        board_mag_ctx.ready = 1U;
        board_mag_ctx.init_error = QMC6309_OK;
        if (board_sensor_bus_get_diag(&recover_diag) == 0) {
            board_mag_ctx.init_iic_diag = recover_diag;
        }
        return BOARD_MAG_OK;
    }

    board_mag_ctx.init_error = ret;
    board_mag_capture_iic_diag();
    return BOARD_MAG_ERR_DRIVER;
}

int8 board_mag_init(void)
{
    qmc6309_bus_t bus;
    int8 ret;

    ret = board_sensor_bus_init();
    if (ret != 0) {
        board_mag_ctx.ready = 0U;
        board_mag_ctx.init_error = QMC6309_ERR_BUS;
        board_mag_capture_iic_diag();
        return BOARD_MAG_ERR_DRIVER;
    }

    bus.write_reg = board_mag_write_reg;
    bus.read_regs = board_mag_read_regs;
    bus.delay_ms = board_mag_delay_ms;

    ret = QMC6309_Bind(&board_mag_ctx.chip, &bus);
    if (ret != QMC6309_OK) {
        board_mag_ctx.ready = 0U;
        board_mag_ctx.init_error = ret;
        board_mag_capture_iic_diag();
        return BOARD_MAG_ERR_DRIVER;
    }

    board_mag_ctx.init_error = QMC6309_OK;
    (void)board_sensor_bus_recover();
    ret = QMC6309_Init(&board_mag_ctx.chip);
    if (ret != QMC6309_OK) {
        board_mag_ctx.init_error = ret;
        board_mag_capture_iic_diag();
        if (board_mag_reinit_after_error() != BOARD_MAG_OK) {
            board_mag_ctx.ready = 0U;
            return BOARD_MAG_ERR_DRIVER;
        }
    } else {
        board_mag_ctx.init_error = QMC6309_OK;
    }

    board_mag_ctx.ready = 1U;
    return BOARD_MAG_OK;
}

u8 board_mag_is_ready(void)
{
    return board_mag_ctx.ready;
}

int8 board_mag_read(board_mag_sample_t *sample)
{
    int8 ret;

    if (sample == 0) {
        return BOARD_MAG_ERR_PARAM;
    }
    if (board_mag_ctx.ready == 0U) {
        return BOARD_MAG_ERR_NOT_READY;
    }

    ret = QMC6309_ReadXYZ(&board_mag_ctx.chip,
                          &sample->mag_x_raw,
                          &sample->mag_y_raw,
                          &sample->mag_z_raw);
    if (ret != QMC6309_OK) {
        board_mag_capture_iic_diag();
        if (ret == QMC6309_ERR_NOT_READY) {
            return BOARD_MAG_ERR_NOT_READY;
        }
        if (board_mag_reinit_after_error() == BOARD_MAG_OK) {
            ret = QMC6309_ReadXYZ(&board_mag_ctx.chip,
                                  &sample->mag_x_raw,
                                  &sample->mag_y_raw,
                                  &sample->mag_z_raw);
            if (ret == QMC6309_OK) {
                return BOARD_MAG_OK;
            }
            board_mag_capture_iic_diag();
        }
        return (ret == QMC6309_ERR_NOT_READY) ? BOARD_MAG_ERR_NOT_READY : BOARD_MAG_ERR_DATA;
    }

    return BOARD_MAG_OK;
}

int8 board_mag_get_diag(board_mag_diag_t *diag)
{
    qmc6309_regs_t regs;
    ef_iic_diag_t iic_diag;
    int8 ret;

    if (diag == 0) {
        return BOARD_MAG_ERR_PARAM;
    }

    diag->chip_error = QMC6309_GetLastError(&board_mag_ctx.chip);
    if (board_mag_ctx.init_error != QMC6309_OK) {
        diag->chip_error = board_mag_ctx.init_error;
    }
    diag->addr = QMC6309_GetAddress(&board_mag_ctx.chip);
    diag->chip_id = 0xFFU;
    diag->status = 0xFFU;
    diag->control_1 = 0xFFU;
    diag->control_2 = 0xFFU;
    diag->i2c_op = 0U;
    diag->i2c_stage = 0U;
    diag->i2c_ret = 0;
    diag->i2c_state_before = 0U;
    diag->i2c_state_after = 0U;
    diag->i2c_recover_ret = 0;
    diag->i2c_msst = 0U;
    diag->i2c_mscr = 0U;

    if (board_sensor_bus_get_diag(&iic_diag) == 0) {
        diag->i2c_op = iic_diag.op;
        diag->i2c_stage = iic_diag.stage;
        diag->i2c_ret = iic_diag.ret;
        diag->i2c_state_before = iic_diag.bus_state_before;
        diag->i2c_state_after = iic_diag.bus_state_after;
        diag->i2c_recover_ret = iic_diag.recover_ret;
        diag->i2c_msst = iic_diag.msst;
        diag->i2c_mscr = iic_diag.mscr;
    }
    if (board_mag_ctx.init_error != QMC6309_OK) {
        diag->i2c_op = board_mag_ctx.init_iic_diag.op;
        diag->i2c_stage = board_mag_ctx.init_iic_diag.stage;
        diag->i2c_ret = board_mag_ctx.init_iic_diag.ret;
        diag->i2c_state_before = board_mag_ctx.init_iic_diag.bus_state_before;
        diag->i2c_state_after = board_mag_ctx.init_iic_diag.bus_state_after;
        diag->i2c_recover_ret = board_mag_ctx.init_iic_diag.recover_ret;
        diag->i2c_msst = board_mag_ctx.init_iic_diag.msst;
        diag->i2c_mscr = board_mag_ctx.init_iic_diag.mscr;
    }

    ret = QMC6309_ReadDiagRegs(&board_mag_ctx.chip, &regs);
    if (ret != QMC6309_OK) {
        return BOARD_MAG_OK;
    }

    diag->chip_id = regs.chip_id;
    diag->status = regs.status;
    diag->control_1 = regs.control_1;
    diag->control_2 = regs.control_2;
    return BOARD_MAG_OK;
}
