/**
 * @file Filter.c
 * @brief 三轴传感器低通滤波模块实现。
 *
 * 当前实现保留旧版模块行为：
 * - 使用一阶 IIR 低通。
 * - 使用 Q8 定点状态，避免引入浮点运算。
 * - 分别维护 gyro / mag 两套独立滤波上下文，互不干扰。
 * - 首帧有效输入直接灌入状态，保证首次输出等于首次输入。
 */

#include "Filter.h"

#define FILTER_LPF_SCALE                ((int32)1L << FILTER_LPF_STATE_Q)
#define FILTER_DATA_IS_ZERO(x, y, z)    (((x) == 0) && ((y) == 0) && ((z) == 0))
#define FILTER_DATA_IS_INVALID(x, y, z) (((x) == -1) && ((y) == -1) && ((z) == -1))

typedef struct
{
    int32 state_x;
    int32 state_y;
    int32 state_z;
    u8 initialized;
} filter_low_pass_3axis_t;

static filter_low_pass_3axis_t filter_gyro_ctx;
static filter_low_pass_3axis_t filter_mag_ctx;

static void Filter_ResetState(filter_low_pass_3axis_t *ctx)
{
    if (ctx == 0) {
        return;
    }

    ctx->state_x = 0;
    ctx->state_y = 0;
    ctx->state_z = 0;
    ctx->initialized = 0U;
}

static int16 Filter_UpdateAxis(int32 *state, int16 input, u8 shift)
{
    int32 target;

    target = (int32)input * FILTER_LPF_SCALE;
    *state += ((target - *state) >> shift);
    return (int16)(*state >> FILTER_LPF_STATE_Q);
}

static s8 Filter_LowPassApply(filter_low_pass_3axis_t *ctx, u8 shift,
                              int16 in_x, int16 in_y, int16 in_z,
                              int16 *out_x, int16 *out_y, int16 *out_z)
{
    if ((ctx == 0) || (out_x == 0) || (out_y == 0) || (out_z == 0)) {
        return -1;
    }

    /*
     * 这里沿用旧模块约定：全 0 或全 -1 视为无效帧，不推进滤波状态。
     * 这样可以过滤传感器未就绪、总线浮空或初始化阶段的假数据。
     */
    if (FILTER_DATA_IS_ZERO(in_x, in_y, in_z) ||
        FILTER_DATA_IS_INVALID(in_x, in_y, in_z)) {
        return -1;
    }

    if (ctx->initialized == 0U) {
        ctx->state_x = (int32)in_x * FILTER_LPF_SCALE;
        ctx->state_y = (int32)in_y * FILTER_LPF_SCALE;
        ctx->state_z = (int32)in_z * FILTER_LPF_SCALE;
        ctx->initialized = 1U;

        *out_x = in_x;
        *out_y = in_y;
        *out_z = in_z;
        return 0;
    }

    *out_x = Filter_UpdateAxis(&ctx->state_x, in_x, shift);
    *out_y = Filter_UpdateAxis(&ctx->state_y, in_y, shift);
    *out_z = Filter_UpdateAxis(&ctx->state_z, in_z, shift);

    return 0;
}

void Filter_ResetGyroLowPass(void)
{
    Filter_ResetState(&filter_gyro_ctx);
}

void Filter_ResetMagLowPass(void)
{
    Filter_ResetState(&filter_mag_ctx);
}

s8 Filter_GyroLowPass(int16 in_x, int16 in_y, int16 in_z,
                      int16 *out_x, int16 *out_y, int16 *out_z)
{
    return Filter_LowPassApply(&filter_gyro_ctx, FILTER_GYRO_LPF_SHIFT,
                               in_x, in_y, in_z, out_x, out_y, out_z);
}

s8 Filter_MagLowPass(int16 in_x, int16 in_y, int16 in_z,
                     int16 *out_x, int16 *out_y, int16 *out_z)
{
    return Filter_LowPassApply(&filter_mag_ctx, FILTER_MAG_LPF_SHIFT,
                               in_x, in_y, in_z, out_x, out_y, out_z);
}
