/**
 * @file HeadingEstimator.h
 * @brief 航向估计组件公共接口。
 *
 * 本组件属于 Components 层，只融合陀螺 Z 轴角速度、磁航向和静止标志，输出
 * 绝对/相对航向诊断值；不访问 GPS、磁力计、GPIO 或 BoardDevices。
 */

#ifndef __HEADING_ESTIMATOR_H__
#define __HEADING_ESTIMATOR_H__

#include "type_def.h"

#ifndef HEADING_USE_INTERNAL_BIAS
#define HEADING_USE_INTERNAL_BIAS      0
#endif

#ifndef HEADING_DBG_FORCE_MAG_OFF
#define HEADING_DBG_FORCE_MAG_OFF      0
#endif

#define HEADING_GYRO_DEADZONE_DPS      0.05f
#define HEADING_BIAS_LIMIT_DPS         10.0f
#define HEADING_MAG_ERR_GATE_DEG       25.0f
#define HEADING_MAG_JUMP_GATE_DEG      10.0f
#define HEADING_STATIC_MAG_STABLE_DEG  1.00f
#define HEADING_STATIC_MAG_ERR_GATE_DEG 3.00f
#define HEADING_MAG_FUSE_MIN_GZ_DPS    0.30f
#define HEADING_KMAG_STATIC            0.0008f
#define HEADING_KMAG_MOVE              0.0f

typedef struct
{
    float heading_deg;
    float heading_zero_deg;

    float gyro_z_bias_dps;

    float yaw_gyro_deg;
    float yaw_mag_deg;
    float heading_pred_deg;
    float heading_err_deg;
    float static_mag_ref_deg;

    float mag_confidence;
    float last_yaw_mag_deg;

    u8 mag_valid;
    u8 mag_used;
    u8 static_flag;
    u8 static_mag_ref_valid;
    u8 raw_mag_valid;
    u8 last_mag_sample_valid;
} HeadingEstimator_t;

/**
 * @brief 初始化航向估计器实例。
 *
 * 清零航向、零点、磁融合状态和内部零偏。该函数不阻塞，不访问硬件。
 *
 * @param h 航向估计器实例，不能为 NULL。
 */
void Heading_Init(HeadingEstimator_t *h);

/**
 * @brief 将当前航向设置为新的相对零点。
 *
 * 后续 `Heading_GetRelativeDeg100()` 以该零点计算 -18000..17999 范围内的相对角。
 *
 * @param h 航向估计器实例，不能为 NULL。
 */
void Heading_ResetZero(HeadingEstimator_t *h);

/**
 * @brief 强制设置当前绝对航向。
 *
 * 输入单位为度，函数会折返到 0..360 范围，并清除磁样本门控状态。通常在启动、
 * GPS/人工校准或重新建立航向参考时调用。
 *
 * @param h 航向估计器实例，不能为 NULL。
 * @param heading_deg 航向角，单位 deg。
 */
void Heading_SetHeadingDeg(HeadingEstimator_t *h, float heading_deg);

/**
 * @brief 更新航向估计。
 *
 * 移动时主要积分陀螺 Z 轴；静止且磁航向稳定时缓慢融合磁参考。函数只处理传入
 * 数值，不访问硬件或全局时钟。
 *
 * @param h 航向估计器实例，不能为 NULL。
 * @param gyro_z_dps Z 轴角速度，单位 deg/s。
 * @param yaw_mag_deg 磁航向，单位 deg。
 * @param mag_valid 磁航向是否有效，0/1。
 * @param static_flag 当前平台是否静止，0/1。
 * @param dt 更新时间，单位 s，必须大于 0。
 */
void Heading_Update(HeadingEstimator_t *h,
                    float gyro_z_dps,
                    float yaw_mag_deg,
                    u8 mag_valid,
                    u8 static_flag,
                    float dt);

/** @brief 获取 0..360 范围内的绝对航向，单位 deg。 */
float Heading_GetDeg(const HeadingEstimator_t *h);

/** @brief 获取 0..35999 范围内的绝对航向，单位 deg*100。 */
int32 Heading_GetDeg100(const HeadingEstimator_t *h);

/** @brief 获取相对零点的航向偏差，单位 deg*100，范围约为 -18000..17999。 */
int32 Heading_GetRelativeDeg100(const HeadingEstimator_t *h);

/** @brief 获取陀螺积分航向诊断值，单位 deg*100。 */
int32 Heading_GetGyroDeg100(const HeadingEstimator_t *h);

/** @brief 获取最近一次磁航向诊断值，单位 deg*100。 */
int32 Heading_GetMagDeg100(const HeadingEstimator_t *h);

/** @brief 获取内部陀螺零偏诊断值，单位 dps*100；未启用内部零偏时固定返回 0。 */
int32 Heading_GetBiasDps100(const HeadingEstimator_t *h);

#endif
