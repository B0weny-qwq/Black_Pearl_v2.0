#include "board_imu.h"
#include "QMI8658.h"
#include "board_sensor_bus.h"

typedef struct
{
    u8 state;
    u8 data_ready;
    qmi8658_t chip;
} board_imu_context_t;

static int8 board_imu_write_reg(void *ctx, u8 addr, u8 reg, u8 value)
{
    (void)ctx;
    return board_sensor_bus_write_reg(addr, reg, value);
}

static int8 board_imu_read_regs(void *ctx, u8 addr, u8 start_reg, u8 *buf, u8 len)
{
    (void)ctx;
    return board_sensor_bus_read_regs(addr, start_reg, buf, len);
}

static void board_imu_delay_ms(void *ctx, u16 ms)
{
    (void)ctx;
    board_sensor_bus_delay_ms(ms);
}

static board_imu_context_t board_imu_ctx = { BOARD_IMU_STATE_IDLE, 0U, {0} };

int8 board_imu_init(void)
{
    qmi8658_bus_t bus;
    int8 ret;

    ret = board_sensor_bus_init();
    if (ret != 0) {
        board_imu_ctx.state = BOARD_IMU_STATE_ERROR;
        board_imu_ctx.data_ready = 0U;
        return BOARD_IMU_ERR_DRIVER;
    }

    bus.ctx = 0;
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

    ret = QMI8658_Init(&board_imu_ctx.chip);
    if (ret != QMI8658_OK) {
        board_imu_ctx.state = BOARD_IMU_STATE_ERROR;
        return BOARD_IMU_ERR_DRIVER;
    }

    board_imu_ctx.state = BOARD_IMU_STATE_READY;
    board_imu_ctx.data_ready = QMI8658_HasDataReady(&board_imu_ctx.chip);
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
