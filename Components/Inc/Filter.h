/**
 * @file Filter.h
 * @brief 三轴传感器一阶低通滤波接口。
 *
 * 本文件属于 Components 层，只提供纯算法能力，不依赖板级外设、STC 官方
 * 驱动或平台寄存器定义。当前保留旧工程 `Filter_*` 接口命名，便于后续把
 * QMI8658、QMC6309 等传感器读数路径平滑迁移到新工程结构。
 */

#ifndef __FILTER_H__
#define __FILTER_H__

#include "type_def.h"

/* 内部固定点状态的小数位数，Q8 可以在平滑精度和 8-bit MCU 开销之间取得平衡。 */
#define FILTER_LPF_STATE_Q       8U

/* shift 越大，滤波越平滑，但响应越慢。当前值沿用旧版模块默认配置。 */
#define FILTER_GYRO_LPF_SHIFT    2U
#define FILTER_MAG_LPF_SHIFT     2U

/**
 * @brief 复位陀螺仪低通滤波器状态。
 *
 * 清空三轴陀螺仪低通状态。下一帧有效输入会直接灌入状态，并作为首帧输出。
 */
void Filter_ResetGyroLowPass(void);

/**
 * @brief 复位地磁计低通滤波器状态。
 *
 * 清空三轴地磁计低通状态。下一帧有效输入会直接灌入状态，并作为首帧输出。
 */
void Filter_ResetMagLowPass(void);

/**
 * @brief 对三轴陀螺仪原始数据执行低通滤波。
 * @param in_x 原始 X 轴角速度。
 * @param in_y 原始 Y 轴角速度。
 * @param in_z 原始 Z 轴角速度。
 * @param out_x 滤波后 X 轴输出指针。
 * @param out_y 滤波后 Y 轴输出指针。
 * @param out_z 滤波后 Z 轴输出指针。
 * @return 0 表示成功，-1 表示空指针或输入帧无效。
 *
 * 输入和输出都保持 `int16` 原始量纲，不在组件层引入浮点换算。
 */
s8 Filter_GyroLowPass(int16 in_x, int16 in_y, int16 in_z,
                      int16 *out_x, int16 *out_y, int16 *out_z);

/**
 * @brief 对三轴地磁原始数据执行低通滤波。
 * @param in_x 原始 X 轴磁场数据。
 * @param in_y 原始 Y 轴磁场数据。
 * @param in_z 原始 Z 轴磁场数据。
 * @param out_x 滤波后 X 轴输出指针。
 * @param out_y 滤波后 Y 轴输出指针。
 * @param out_z 滤波后 Z 轴输出指针。
 * @return 0 表示成功，-1 表示空指针或输入帧无效。
 */
s8 Filter_MagLowPass(int16 in_x, int16 in_y, int16 in_z,
                     int16 *out_x, int16 *out_y, int16 *out_z);

#endif
