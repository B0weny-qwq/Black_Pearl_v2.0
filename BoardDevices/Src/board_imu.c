#include "board_imu.h"
#include "QMI8658.h"
#include "board_sensor_bus.h"

typedef struct
{
    u8 state;
    u8 data_ready;
    qmi8658_t chip;
    int8 init_error;
    ef_iic_diag_t init_iic_diag;
} board_imu_context_t;

#define BOARD_IMU_FIRST_SAMPLE_RETRY_MAX      5U
#define BOARD_IMU_FIRST_SAMPLE_RETRY_DELAY_MS 20U

static int8 board_imu_write_reg(u8 addr, u8 reg, u8 value)
{
    return board_sensor_bus_write_reg(addr, reg, value);
}

static int8 board_imu_read_regs(u8 addr, u8 start_reg, u8 *buf, u8 len)
{
    return board_sensor_bus_read_regs(addr, start_reg, buf, len);
}

static void board_imu_delay_ms(u16 ms)
{
    board_sensor_bus_delay_ms(ms);
}

static board_imu_context_t board_imu_ctx = { BOARD_IMU_STATE_IDLE, 0U, {0}, QMI8658_OK, {0} };

static u8 board_imu_status_is_ready(u8 status0)
{
    return (((status0 & (QMI8658_STATUS0_A_DA | QMI8658_STATUS0_G_DA)) ==
             (QMI8658_STATUS0_A_DA | QMI8658_STATUS0_G_DA)) ? 1U : 0U);
}

static void board_imu_capture_iic_diag(void)
{
    if (board_sensor_bus_get_diag(&board_imu_ctx.init_iic_diag) != 0) {
        board_imu_ctx.init_iic_diag.op = 0U;
        board_imu_ctx.init_iic_diag.stage = 0U;
        board_imu_ctx.init_iic_diag.ret = 0;
        board_imu_ctx.init_iic_diag.dev_addr = 0U;
        board_imu_ctx.init_iic_diag.reg_addr = 0U;
        board_imu_ctx.init_iic_diag.msst = 0U;
        board_imu_ctx.init_iic_diag.mscr = 0U;
        board_imu_ctx.init_iic_diag.bus_state_before = 0U;
        board_imu_ctx.init_iic_diag.bus_state_after = 0U;
        board_imu_ctx.init_iic_diag.recover_ret = 0;
    }
}

static void board_imu_accept_diag_configured(const qmi8658_diag_regs_t *diag)
{
    board_imu_ctx.chip.initialized = 1U;
    board_imu_ctx.chip.data_ready = board_imu_status_is_ready(diag->status0);
    board_imu_ctx.chip.last_error = QMI8658_OK;
    board_imu_ctx.chip.last_id = diag->who_am_i;
    board_imu_ctx.chip.last_ctrl1 = diag->ctrl1;
    board_imu_ctx.chip.last_ctrl2 = diag->ctrl2;
    board_imu_ctx.chip.last_ctrl3 = diag->ctrl3;
    board_imu_ctx.chip.last_ctrl5 = diag->ctrl5;
    board_imu_ctx.chip.last_ctrl7 = diag->ctrl7;
    board_imu_ctx.chip.last_status0 = diag->status0;
}

static u8 board_imu_diag_is_configured(const qmi8658_diag_regs_t *diag)
{
    if (diag == 0) {
        return 0U;
    }

    if ((diag->who_am_i == QMI8658_CHIP_ID_VALUE) &&
        (diag->ctrl1 == QMI8658_CTRL1_INIT) &&
        (diag->ctrl2 == QMI8658_CTRL2_INIT) &&
        (diag->ctrl3 == QMI8658_CTRL3_INIT) &&
        (diag->ctrl5 == QMI8658_CTRL5_INIT) &&
        (diag->ctrl7 == QMI8658_CTRL7_INIT)) {
        return 1U;
    }

    return 0U;
}

static u8 board_imu_diag_is_ready_configured(const qmi8658_diag_regs_t *diag)
{
    if (board_imu_diag_is_configured(diag) == 0U) {
        return 0U;
    }
    return board_imu_status_is_ready(diag->status0);
}

static int8 board_imu_wait_first_sample(void)
{
    qmi8658_sample_t sample;
    u8 retry;
    u8 status0;
    int8 ret;

    ret = QMI8658_ERR_NOT_READY;
    for (retry = 0U; retry < BOARD_IMU_FIRST_SAMPLE_RETRY_MAX; retry++) {
        status0 = 0U;
        ret = QMI8658_ReadStatus0(&board_imu_ctx.chip, &status0);
        if ((ret == QMI8658_OK) && (board_imu_status_is_ready(status0) != 0U)) {
            ret = QMI8658_ReadRawSample(&board_imu_ctx.chip, &sample);
            if (ret == QMI8658_OK) {
                return QMI8658_OK;
            }
        }
        if ((u8)(retry + 1U) < BOARD_IMU_FIRST_SAMPLE_RETRY_MAX) {
            board_sensor_bus_delay_ms(BOARD_IMU_FIRST_SAMPLE_RETRY_DELAY_MS);
        }
    }

    return ret;
}

static int8 board_imu_fill_diag(board_imu_diag_t *diag)
{
    qmi8658_diag_regs_t chip_diag;
    ef_iic_diag_t iic_diag;
    int8 ret;

    if (diag == 0) {
        return BOARD_IMU_ERR_PARAM;
    }

    diag->chip_error = QMI8658_GetLastError(&board_imu_ctx.chip);
    if (board_imu_ctx.init_error != QMI8658_OK) {
        diag->chip_error = board_imu_ctx.init_error;
    }
    diag->i2c_addr = QMI8658_GetAddress(&board_imu_ctx.chip);
    diag->who_am_i = QMI8658_GetLastID(&board_imu_ctx.chip);
    diag->ctrl1 = 0xFFU;
    diag->ctrl2 = 0xFFU;
    diag->ctrl3 = 0xFFU;
    diag->ctrl5 = 0xFFU;
    diag->ctrl7 = 0xFFU;
    diag->status0 = 0xFFU;
    diag->cfg_retry = board_imu_ctx.chip.last_cfg_retry;
    diag->cfg_reg = board_imu_ctx.chip.last_cfg_reg;
    diag->cfg_write = board_imu_ctx.chip.last_cfg_write;
    diag->cfg_read = board_imu_ctx.chip.last_cfg_read;
    diag->cfg_ret = board_imu_ctx.chip.last_cfg_ret;
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
    if (board_imu_ctx.init_error != QMI8658_OK) {
        diag->i2c_op = board_imu_ctx.init_iic_diag.op;
        diag->i2c_stage = board_imu_ctx.init_iic_diag.stage;
        diag->i2c_ret = board_imu_ctx.init_iic_diag.ret;
        diag->i2c_state_before = board_imu_ctx.init_iic_diag.bus_state_before;
        diag->i2c_state_after = board_imu_ctx.init_iic_diag.bus_state_after;
        diag->i2c_recover_ret = board_imu_ctx.init_iic_diag.recover_ret;
        diag->i2c_msst = board_imu_ctx.init_iic_diag.msst;
        diag->i2c_mscr = board_imu_ctx.init_iic_diag.mscr;
    }

    ret = QMI8658_ReadDiagRegs(&board_imu_ctx.chip, &chip_diag);
    if (ret == QMI8658_OK) {
        diag->who_am_i = chip_diag.who_am_i;
        diag->ctrl1 = chip_diag.ctrl1;
        diag->ctrl2 = chip_diag.ctrl2;
        diag->ctrl3 = chip_diag.ctrl3;
        diag->ctrl5 = chip_diag.ctrl5;
        diag->ctrl7 = chip_diag.ctrl7;
        diag->status0 = chip_diag.status0;
    }
    return BOARD_IMU_OK;
}

int8 board_imu_init(void)
{
    qmi8658_bus_t bus;
    qmi8658_diag_regs_t diag_regs;
    int8 ret;

    ret = board_sensor_bus_init();
    if (ret != 0) {
        board_imu_ctx.state = BOARD_IMU_STATE_ERROR;
        board_imu_ctx.data_ready = 0U;
        return BOARD_IMU_ERR_DRIVER;
    }

    bus.write_reg = board_imu_write_reg;
    bus.read_regs = board_imu_read_regs;
    bus.delay_ms = board_imu_delay_ms;

    ret = QMI8658_Bind(&board_imu_ctx.chip, &bus);
    if (ret != QMI8658_OK) {
        board_imu_ctx.state = BOARD_IMU_STATE_ERROR;
        board_imu_ctx.data_ready = 0U;
        return BOARD_IMU_ERR_DRIVER;
    }

    board_imu_ctx.state = BOARD_IMU_STATE_INIT;
    board_imu_ctx.data_ready = 0U;
    board_imu_ctx.init_error = QMI8658_OK;
    board_imu_capture_iic_diag();

    (void)board_sensor_bus_recover();
    ret = QMI8658_Init(&board_imu_ctx.chip);
    if (ret != QMI8658_OK) {
        board_imu_ctx.init_error = ret;
        board_imu_capture_iic_diag();
        if ((QMI8658_ReadDiagRegs(&board_imu_ctx.chip, &diag_regs) == QMI8658_OK) &&
            (board_imu_diag_is_ready_configured(&diag_regs) != 0U)) {
            board_imu_accept_diag_configured(&diag_regs);
            board_imu_ctx.init_error = QMI8658_OK;
        } else {
            (void)board_sensor_bus_recover();
            board_sensor_bus_delay_ms(20U);
            ret = QMI8658_Init(&board_imu_ctx.chip);
            if (ret == QMI8658_OK) {
                ret = board_imu_wait_first_sample();
                if (ret == QMI8658_OK) {
                    board_imu_ctx.state = BOARD_IMU_STATE_READY;
                    board_imu_ctx.data_ready = QMI8658_HasDataReady(&board_imu_ctx.chip);
                    board_imu_ctx.init_error = QMI8658_OK;
                    return BOARD_IMU_OK;
                }
            }
            board_imu_ctx.init_error = ret;
            board_imu_capture_iic_diag();
            if ((QMI8658_ReadDiagRegs(&board_imu_ctx.chip, &diag_regs) == QMI8658_OK) &&
                (board_imu_diag_is_ready_configured(&diag_regs) != 0U)) {
                board_imu_accept_diag_configured(&diag_regs);
                board_imu_ctx.init_error = QMI8658_OK;
            } else {
                board_imu_ctx.state = BOARD_IMU_STATE_ERROR;
                board_imu_ctx.data_ready = 0U;
                return BOARD_IMU_ERR_DRIVER;
            }
        }
    }

    /*
     * 短路径下如果芯片层返回成功但本地 ready 尚未刷新，再用一次诊断确认。
     * 这避免启动阶段继续等待很久，同时保留真实失败返回。
     */
    ret = board_imu_wait_first_sample();
    if (ret != QMI8658_OK) {
        board_imu_ctx.init_error = ret;
        board_imu_capture_iic_diag();
        board_imu_ctx.state = BOARD_IMU_STATE_ERROR;
        board_imu_ctx.data_ready = 0U;
        return BOARD_IMU_ERR_DATA;
    }

    board_imu_ctx.state = BOARD_IMU_STATE_READY;
    board_imu_ctx.data_ready = QMI8658_HasDataReady(&board_imu_ctx.chip);
    board_imu_ctx.init_error = QMI8658_OK;
    return BOARD_IMU_OK;
}

int8 board_imu_service(void)
{
    u8 status0;
    int8 ret;

    if (board_imu_ctx.state == BOARD_IMU_STATE_ERROR) {
        return BOARD_IMU_ERR_DRIVER;
    }
    if (board_imu_ctx.state != BOARD_IMU_STATE_READY) {
        return BOARD_IMU_ERR_NOT_READY;
    }

    ret = QMI8658_ReadStatus0(&board_imu_ctx.chip, &status0);
    if (ret != QMI8658_OK) {
        board_imu_ctx.state = BOARD_IMU_STATE_ERROR;
        board_imu_ctx.data_ready = 0U;
        return BOARD_IMU_ERR_DATA;
    }

    board_imu_ctx.data_ready = QMI8658_HasDataReady(&board_imu_ctx.chip);

    return BOARD_IMU_OK;
}

u8 board_imu_is_ready(void)
{
    return (board_imu_ctx.state == BOARD_IMU_STATE_READY) ? 1U : 0U;
}

u8 board_imu_has_data_ready(void)
{
    return board_imu_ctx.data_ready;
}

void board_imu_clear_data_ready(void)
{
    QMI8658_ClearDataReady(&board_imu_ctx.chip);
    board_imu_ctx.data_ready = 0U;
}

int8 board_imu_read(board_imu_sample_t *sample)
{
    qmi8658_sample_t raw_sample;
    int8 ret;

    if (sample == 0) {
        return BOARD_IMU_ERR_PARAM;
    }

    /*
     * 先清输出缓冲，保证错误返回时调用者不会拿到上一次栈上的随机值。
     * 成功路径会由 QMI8658 实时采样覆盖这些字段。
     */
    sample->acc_x_raw = 0;
    sample->acc_y_raw = 0;
    sample->acc_z_raw = 0;
    sample->gyro_x_raw = 0;
    sample->gyro_y_raw = 0;
    sample->gyro_z_raw = 0;
    sample->temp_raw = 0;
    sample->has_temp = 0U;

    if (board_imu_ctx.state != BOARD_IMU_STATE_READY) {
        return BOARD_IMU_ERR_NOT_READY;
    }

    ret = QMI8658_ReadRawSample(&board_imu_ctx.chip, &raw_sample);
    if (ret != QMI8658_OK) {
        return BOARD_IMU_ERR_DATA;
    }

    sample->acc_x_raw = raw_sample.acc_x_raw;
    sample->acc_y_raw = raw_sample.acc_y_raw;
    sample->acc_z_raw = raw_sample.acc_z_raw;
    sample->gyro_x_raw = raw_sample.gyro_x_raw;
    sample->gyro_y_raw = raw_sample.gyro_y_raw;
    sample->gyro_z_raw = raw_sample.gyro_z_raw;
    sample->temp_raw = raw_sample.temp_raw;
    sample->has_temp = raw_sample.has_temp;
    board_imu_ctx.data_ready = QMI8658_HasDataReady(&board_imu_ctx.chip);
    return BOARD_IMU_OK;
}

u8 board_imu_get_state(void)
{
    return board_imu_ctx.state;
}

int8 board_imu_get_diag(board_imu_diag_t *diag)
{
    return board_imu_fill_diag(diag);
}
