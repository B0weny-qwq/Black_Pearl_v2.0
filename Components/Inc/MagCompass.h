/**
 * @file MagCompass.h
 * @brief QMC6309 磁罗盘航向解算组件。
 *
 * 本组件从 v1.1 主循环中拆出磁力计水平航向、安装偏置、模长门控和 IIR
 * 稳定判定逻辑。它只处理原始三轴磁场数据，不访问 I2C、寄存器或板级设备。
 */

#ifndef __MAG_COMPASS_H__
#define __MAG_COMPASS_H__

#include "type_def.h"

#define MAG_COMPASS_READY_COUNT        5U
#define MAG_COMPASS_IIR_DIV            8L
#define MAG_COMPASS_JUMP_GATE_CD       3000L
#define MAG_COMPASS_NORM_TRACK_PCT     25U
#define MAG_COMPASS_NORM_REJECT_PCT    80U
#define MAG_COMPASS_HORIZ_MIN_SUM      40UL
#define MAG_COMPASS_RAW_OFFSET_CD      0L
#define MAG_COMPASS_DIRECTION_SIGN     (-1L)
#define MAG_COMPASS_INSTALL_OFFSET_CD  21930L
#define MAG_COMPASS_DECLINATION_CD     0L

typedef struct
{
    u8 ready;
    u16 heading_deg100;
    u32 norm1;
    u32 horiz_sum;
    u8 stable_count;
} mag_compass_state_t;

/**
 * @brief 复位磁罗盘稳定滤波器。
 *
 * 清除模长基准、IIR 航向和 ready 快照。通常在系统启动或重新进入静止门控时调用。
 */
void MagCompass_Reset(void);

/**
 * @brief 输入一帧原始磁力计数据并更新罗盘航向。
 *
 * @param raw_x 原始 X 轴磁场。
 * @param raw_y 原始 Y 轴磁场。
 * @param raw_z 原始 Z 轴磁场。
 * @param heading_out 输出稳定后的航向，单位 deg*100，范围 0..35999，可为 NULL。
 * @return 1 表示本次得到稳定航向，0 表示样本被门控或仍在稳定计数。
 */
u8 MagCompass_Update(int16 raw_x, int16 raw_y, int16 raw_z, u16 *heading_out);

/**
 * @brief 直接计算单帧原始磁力计罗盘航向。
 *
 * 该函数包含 v1.1 的轴映射、方向符号、安装偏置和磁偏角修正。
 */
u8 MagCompass_ComputeRawHeading(int16 raw_x, int16 raw_y, int16 raw_z,
                                u16 *heading_out,
                                u32 *norm1_out,
                                u32 *horiz_sum_out);

/** @brief 获取最近一次磁罗盘稳定状态快照。 */
const mag_compass_state_t *MagCompass_GetState(void);

#endif
