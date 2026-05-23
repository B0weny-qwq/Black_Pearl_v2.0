/**
 * @file    Filter.h
 * @brief   三轴传感器一阶低通滤波接口。
 * @author  boweny
 * @date    2026-05-05
 * @version v1.0
 *
 * @details
 * 为陀螺仪和地磁计三轴原始数据提供独立的软件低通滤波器。内部使用
 * Q8 定点状态和一阶 IIR 算法，不依赖浮点运算。
 *
 * @note    输入和输出均为 int16 原始传感器量纲。
 * @note    空指针或无效帧返回 -1，且不会推进内部滤波状态。
 *
 * @see     Code_boweny/Function/Filter/Filter.c
 */

#ifndef __FILTER_H__
#define __FILTER_H__

#include "config.h"

#define FILTER_LPF_STATE_Q       8  /**< 内部滤波状态的小数位数，Q8 格式。 */
#define FILTER_GYRO_LPF_SHIFT    2  /**< 陀螺仪低通平滑强度，值越大响应越慢。 */
#define FILTER_MAG_LPF_SHIFT     2  /**< 地磁计低通平滑强度，值越大响应越慢。 */

/**
 * @brief   复位陀螺仪低通滤波器状态。
 * @return  none
 *
 * @details
 * 清空三轴陀螺仪低通滤波状态。下一帧有效数据会作为首帧直接灌入状态。
 */
void Filter_ResetGyroLowPass(void);

/**
 * @brief   复位地磁计低通滤波器状态。
 * @return  none
 *
 * @details
 * 清空三轴地磁计低通滤波状态。下一帧有效数据会作为首帧直接灌入状态。
 */
void Filter_ResetMagLowPass(void);

/**
 * @brief      对三轴陀螺仪数据执行低通滤波。
 * @param[in]  in_x   原始 X 轴角速度。
 * @param[in]  in_y   原始 Y 轴角速度。
 * @param[in]  in_z   原始 Z 轴角速度。
 * @param[out] out_x  滤波后 X 轴角速度输出指针。
 * @param[out] out_y  滤波后 Y 轴角速度输出指针。
 * @param[out] out_z  滤波后 Z 轴角速度输出指针。
 * @return     0=成功，-1=空指针或输入帧无效。
 *
 * @details
 * 使用一阶 IIR 公式：state += (((input << Q) - state) >> shift)。
 * 首帧有效数据直接写入状态，保证首次输出等于输入值。
 */
s8 Filter_GyroLowPass(int16 in_x, int16 in_y, int16 in_z,
                      int16 *out_x, int16 *out_y, int16 *out_z);

/**
 * @brief      对三轴地磁计数据执行低通滤波。
 * @param[in]  in_x   原始 X 轴磁场数据。
 * @param[in]  in_y   原始 Y 轴磁场数据。
 * @param[in]  in_z   原始 Z 轴磁场数据。
 * @param[out] out_x  滤波后 X 轴磁场数据输出指针。
 * @param[out] out_y  滤波后 Y 轴磁场数据输出指针。
 * @param[out] out_z  滤波后 Z 轴磁场数据输出指针。
 * @return     0=成功，-1=空指针或输入帧无效。
 */
s8 Filter_MagLowPass(int16 in_x, int16 in_y, int16 in_z,
                     int16 *out_x, int16 *out_y, int16 *out_z);

#endif
