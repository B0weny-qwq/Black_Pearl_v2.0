#include "board_imu.h"

/*
 * 当前文件只建立 BoardDevices 层的 IMU API 外壳。
 *
 * 注意：这里故意不包含 ef_iic.h，也不写 QMI8658 地址和寄存器。
 * 用户要求先把 DMA IIC 驱动独立移植好，因此 QMI8658 的真正绑定应在后续步骤
 * 引入 ChipDrivers/QMI8658 后，再由本文件私有地组合 chip driver 和 ef_iic。
 */

typedef struct
{
    u8 state;
    u8 data_ready;
} board_imu_context_t;

/* 单板通常只有一个 IMU 实例，用静态上下文即可，不引入通用 device model。 */
static board_imu_context_t board_imu_ctx = { BOARD_IMU_STATE_NOT_BOUND, 0U };

int8 board_imu_init(void)
{
    /*
     * 目前尚未绑定具体芯片驱动，保持 NOT_BOUND。
     * 这样 App 即使提前调用，也能得到明确错误，而不是读到假数据。
     */
    board_imu_ctx.state = BOARD_IMU_STATE_NOT_BOUND;
    board_imu_ctx.data_ready = 0U;
    return BOARD_IMU_ERR_NOT_BOUND;
}

int8 board_imu_service(void)
{
    /* 占位阶段没有后台任务；未绑定时显式返回错误，便于上层判断移植进度。 */
    if (board_imu_ctx.state == BOARD_IMU_STATE_NOT_BOUND) {
        return BOARD_IMU_ERR_NOT_BOUND;
    }

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
    board_imu_ctx.data_ready = 0U;
}

int8 board_imu_read(board_imu_sample_t *sample)
{
    if (sample == 0) {
        return BOARD_IMU_ERR_PARAM;
    }

    /*
     * 先清输出缓冲，保证错误返回时调用者不会拿到上一次栈上的随机值。
     * 后续真正接入 QMI8658 后，成功路径会覆盖这些字段。
     */
    sample->acc_x_raw = 0;
    sample->acc_y_raw = 0;
    sample->acc_z_raw = 0;
    sample->gyro_x_raw = 0;
    sample->gyro_y_raw = 0;
    sample->gyro_z_raw = 0;
    sample->temp_raw = 0;
    sample->has_temp = 0U;

    if (board_imu_ctx.state == BOARD_IMU_STATE_NOT_BOUND) {
        return BOARD_IMU_ERR_NOT_BOUND;
    }
    if (board_imu_ctx.state != BOARD_IMU_STATE_READY) {
        return BOARD_IMU_ERR_NOT_READY;
    }

    /* READY 状态暂时不可达；保留 DATA 错误码给后续底层读寄存器失败使用。 */
    return BOARD_IMU_ERR_DATA;
}

u8 board_imu_get_state(void)
{
    return board_imu_ctx.state;
}
