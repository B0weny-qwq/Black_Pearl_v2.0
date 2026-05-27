#include "board_mag.h"
#include "QMC6309.h"
#include "board_sensor_bus.h"

typedef struct
{
    qmc6309_t chip;
    u8 ready;
} board_mag_context_t;

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

static board_mag_context_t board_mag_ctx = { {0}, 0U };

int8 board_mag_init(void)
{
    qmc6309_bus_t bus;
    int8 ret;

    ret = board_sensor_bus_init();
    if (ret != 0) {
        board_mag_ctx.ready = 0U;
        return BOARD_MAG_ERR_DRIVER;
    }

    bus.write_reg = board_mag_write_reg;
    bus.read_regs = board_mag_read_regs;
    bus.delay_ms = board_mag_delay_ms;

    ret = QMC6309_Bind(&board_mag_ctx.chip, &bus);
    if (ret != QMC6309_OK) {
        board_mag_ctx.ready = 0U;
        return BOARD_MAG_ERR_DRIVER;
    }

    ret = QMC6309_Init(&board_mag_ctx.chip);
    if (ret != QMC6309_OK) {
        board_mag_ctx.ready = 0U;
        return BOARD_MAG_ERR_DRIVER;
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
        return BOARD_MAG_ERR_DATA;
    }

    return BOARD_MAG_OK;
}

int8 board_mag_get_diag(board_mag_diag_t *diag)
{
    qmc6309_regs_t regs;
    int8 ret;

    if (diag == 0) {
        return BOARD_MAG_ERR_PARAM;
    }

    diag->addr = QMC6309_GetAddress(&board_mag_ctx.chip);
    diag->chip_id = 0xFFU;
    diag->control_1 = 0xFFU;
    diag->control_2 = 0xFFU;

    ret = QMC6309_ReadDiagRegs(&board_mag_ctx.chip, &regs);
    if (ret != QMC6309_OK) {
        return BOARD_MAG_ERR_DATA;
    }

    diag->chip_id = regs.chip_id;
    diag->control_1 = regs.control_1;
    diag->control_2 = regs.control_2;
    return BOARD_MAG_OK;
}
