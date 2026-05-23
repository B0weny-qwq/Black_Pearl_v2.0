/**
 * @file    Filter.c
 * @brief   三轴传感器低通滤波模块实现
 * @author  boweny
 * @date    2026-04-23
 * @version v1.0
 *
 * @details
 * - 实现陀螺仪与地磁三轴数据的软件低通滤波
 * - 采用一阶 IIR + Q8 定点整数状态，避免使用浮点运算
 * - 分别维护 gyro / mag 两套独立滤波状态，互不干扰
 * - 导出函数：Filter_ResetGyroLowPass() / Filter_ResetMagLowPass() /
 *             Filter_GyroLowPass() / Filter_MagLowPass()
 *
 * @note    首帧有效数据直接灌入状态，保证首次输出等于输入
 * @note    无效帧与空指针直接返回 -1，不会污染内部状态
 *
 * @see     Code_boweny/Function/Filter/Filter.h
 */

#include "Filter.h"

#define FILTER_LPF_SCALE            ((int32)1L << FILTER_LPF_STATE_Q)
#define FILTER_DATA_IS_ZERO(x,y,z)  (((x) == 0) && ((y) == 0) && ((z) == 0))
#define FILTER_DATA_IS_INVALID(x,y,z)  (((x) == -1) && ((y) == -1) && ((z) == -1))

typedef struct
{
    int32 state_x;
    int32 state_y;
    int32 state_z;
    u8 initialized;
} Filter_LowPass3Axis;

static Filter_LowPass3Axis filter_gyro_ctx;
static Filter_LowPass3Axis filter_mag_ctx;

/**
 * @brief      复位单个三轴低通状态
 * @param[out] ctx  指向三轴滤波状态的指针
 * @return     none
 *
 * @details 驱动内部使用，不对外暴露。
 */
static void Filter_ResetState(Filter_LowPass3Axis *ctx)
{
    if (ctx == 0) {
        return;
    }

    ctx->state_x = 0;
    ctx->state_y = 0;
    ctx->state_z = 0;
    ctx->initialized = 0;
}

/**
 * @brief      更新单轴低通状态
 * @param[out] state  指向单轴 Q 格式状态的指针
 * @param[in]  input  原始输入值
 * @param[in]  shift  平滑系数移位值
 * @return     滤波后的 int16 输出
 *
 * @details 采用一阶 IIR 低通，不对外暴露。
 */
static int16 Filter_UpdateAxis(int32 *state, int16 input, u8 shift)
{
    int32 target;

    target = (int32)input * FILTER_LPF_SCALE;
    *state += ((target - *state) >> shift);
    return (int16)(*state >> FILTER_LPF_STATE_Q);
}

/**
 * @brief      对三轴数据执行通用低通处理
 * @param[out] ctx    指向目标滤波状态的指针
 * @param[in]  shift  平滑系数移位值
 * @param[in]  in_x   原始 X 轴输入
 * @param[in]  in_y   原始 Y 轴输入
 * @param[in]  in_z   原始 Z 轴输入
 * @param[out] out_x  指向滤波后 X 轴输出的指针
 * @param[out] out_y  指向滤波后 Y 轴输出的指针
 * @param[out] out_z  指向滤波后 Z 轴输出的指针
 * @return     0=成功，-1=空指针或输入帧无效
 *
 * @details
 * 首帧有效数据直接作为输出并灌入状态。
 * 后续帧按固定点一阶 IIR 低通更新。
 */
static s8 Filter_LowPassApply(Filter_LowPass3Axis *ctx, u8 shift,
                              int16 in_x, int16 in_y, int16 in_z,
                              int16 *out_x, int16 *out_y, int16 *out_z)
{
    if ((ctx == 0) || (out_x == 0) || (out_y == 0) || (out_z == 0)) {
        return -1;
    }

    if (FILTER_DATA_IS_ZERO(in_x, in_y, in_z) ||
        FILTER_DATA_IS_INVALID(in_x, in_y, in_z)) {
        return -1;
    }

    if (!ctx->initialized) {
        ctx->state_x = (int32)in_x * FILTER_LPF_SCALE;
        ctx->state_y = (int32)in_y * FILTER_LPF_SCALE;
        ctx->state_z = (int32)in_z * FILTER_LPF_SCALE;
        ctx->initialized = 1;

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

/**
 * @brief   复位陀螺仪低通滤波器状态
 * @return  none
 *
 * @details 清空陀螺仪三轴低通状态。
 */
void Filter_ResetGyroLowPass(void)
{
    Filter_ResetState(&filter_gyro_ctx);
}

/**
 * @brief   复位地磁低通滤波器状态
 * @return  none
 *
 * @details 清空地磁三轴低通状态。
 */
void Filter_ResetMagLowPass(void)
{
    Filter_ResetState(&filter_mag_ctx);
}

/**
 * @brief      对陀螺仪三轴数据执行低通滤波
 * @param[in]  in_x   原始 X 轴角速度
 * @param[in]  in_y   原始 Y 轴角速度
 * @param[in]  in_z   原始 Z 轴角速度
 * @param[out] out_x  指向滤波后 X 轴角速度的指针
 * @param[out] out_y  指向滤波后 Y 轴角速度的指针
 * @param[out] out_z  指向滤波后 Z 轴角速度的指针
 * @return     0=成功，-1=空指针或输入帧无效
 *
 * @details 使用独立的陀螺仪三轴滤波状态，不影响地磁滤波链路。
 */
s8 Filter_GyroLowPass(int16 in_x, int16 in_y, int16 in_z,
                      int16 *out_x, int16 *out_y, int16 *out_z)
{
    return Filter_LowPassApply(&filter_gyro_ctx, FILTER_GYRO_LPF_SHIFT,
                               in_x, in_y, in_z, out_x, out_y, out_z);
}

/**
 * @brief      对地磁三轴数据执行低通滤波
 * @param[in]  in_x   原始 X 轴磁场数据
 * @param[in]  in_y   原始 Y 轴磁场数据
 * @param[in]  in_z   原始 Z 轴磁场数据
 * @param[out] out_x  指向滤波后 X 轴磁场数据的指针
 * @param[out] out_y  指向滤波后 Y 轴磁场数据的指针
 * @param[out] out_z  指向滤波后 Z 轴磁场数据的指针
 * @return     0=成功，-1=空指针或输入帧无效
 *
 * @details 使用独立的地磁三轴滤波状态，不影响陀螺仪滤波链路。
 */
s8 Filter_MagLowPass(int16 in_x, int16 in_y, int16 in_z,
                     int16 *out_x, int16 *out_y, int16 *out_z)
{
    return Filter_LowPassApply(&filter_mag_ctx, FILTER_MAG_LPF_SHIFT,
                               in_x, in_y, in_z, out_x, out_y, out_z);
}
