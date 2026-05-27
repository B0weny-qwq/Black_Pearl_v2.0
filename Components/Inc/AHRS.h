/**
 * @file    AHRS.h
 * @brief   姿态解算组件公共接口。
 * @author  boweny
 * @date    2026-05-11
 * @version v1.2
 *
 * 本组件属于 Components 层，只处理 IMU/MAG 原始数值、轴映射、四元数姿态和
 * deg*100 诊断输出，不访问 I2C、GPIO、STC 寄存器或 BoardDevices。
 */

#ifndef __AHRS_H__
#define __AHRS_H__

#include "type_def.h"

#define AHRS_IMU_PERIOD_MS               17U
#define AHRS_MAG_PERIOD_MS               100U
#define AHRS_DT_MAX_MS                   50U

#define AHRS_GYRO_LSB_PER_DPS            128L
#define AHRS_GYRO_STILL_DPS100           200L
#define AHRS_GYRO_BIAS_TRACK_SHIFT       6U
#define AHRS_GYRO_DEADBAND_DPS100        12L
#define AHRS_GYRO_LPF_SHIFT              2U

#define AHRS_ACC_REF_SAMPLE_COUNT        32U
#define AHRS_ACC_NORM_TOLERANCE_PERCENT  35U

#define AHRS_GYRO_BIAS_SAMPLE_COUNT      128U

#ifndef AHRS_MAG_ENABLE
#define AHRS_MAG_ENABLE                  1
#endif
#define AHRS_MAG_LPF_SHIFT               3U
#define AHRS_MAG_MIN_NORM                50U

#define AHRS_MAHONY_KP_ACC               1.00f
#define AHRS_MAHONY_KP_MAG               0.15f
#define AHRS_MAHONY_KI                   0.00f

#define AHRS_AXIS_RAW_X                  0U
#define AHRS_AXIS_RAW_Y                  1U
#define AHRS_AXIS_RAW_Z                  2U

#define AHRS_IMU_BODY_X_FROM             AHRS_AXIS_RAW_X
#define AHRS_IMU_BODY_X_SIGN             1
#define AHRS_IMU_BODY_Y_FROM             AHRS_AXIS_RAW_Y
#define AHRS_IMU_BODY_Y_SIGN             1
#define AHRS_IMU_BODY_Z_FROM             AHRS_AXIS_RAW_Z
#define AHRS_IMU_BODY_Z_SIGN             1

#define AHRS_MAG_BODY_X_FROM             AHRS_AXIS_RAW_X
#define AHRS_MAG_BODY_X_SIGN             1
#define AHRS_MAG_BODY_Y_FROM             AHRS_AXIS_RAW_Y
#define AHRS_MAG_BODY_Y_SIGN             1
#define AHRS_MAG_BODY_Z_FROM             AHRS_AXIS_RAW_Z
#define AHRS_MAG_BODY_Z_SIGN             1

#define AHRS_FLAG_READY                  0x01U
#define AHRS_FLAG_ACC_VALID              0x02U
#define AHRS_FLAG_MAG_VALID              0x04U
#define AHRS_FLAG_GYRO_BIAS_READY        0x08U
#define AHRS_FLAG_ACC_REF_READY          0x10U
#define AHRS_FLAG_DT_CLAMPED             0x20U

typedef struct
{
    int16 roll_deg100;
    int16 pitch_deg100;
    int16 yaw_deg100;
    int16 yaw_gyro_deg100;
    int16 yaw_mag_deg100;

    int16 gyro_x_dps100;
    int16 gyro_y_dps100;
    int16 gyro_z_dps100;

    u16   acc_norm;
    u16   dt_ms;
    u16   update_count;
    u8    flags;
} AHRS_State_t;

/**
 * @brief 复位 AHRS 内部状态。
 *
 * 清除四元数、陀螺零偏、加速度参考、磁力计缓存和输出快照。该函数不访问硬件，
 * 不阻塞，可在启动或传感器异常恢复后调用。
 */
void AHRS_Reset(void);

/**
 * @brief 使用机体系六轴数据更新姿态。
 *
 * 输入单位为加速度原始 LSB、陀螺原始 LSB，`dt_ms` 单位为毫秒。函数会在内部
 * 限制过大的 dt、学习静止零偏并更新 `AHRS_State_t` 快照；不访问硬件。
 *
 * @param ax 机体系 X 轴加速度原始值。
 * @param ay 机体系 Y 轴加速度原始值。
 * @param az 机体系 Z 轴加速度原始值。
 * @param gx 机体系 X 轴陀螺原始值。
 * @param gy 机体系 Y 轴陀螺原始值。
 * @param gz 机体系 Z 轴陀螺原始值。
 * @param dt_ms 本次更新间隔，单位 ms，不能为 0。
 * @return 0 更新成功。
 * @return -1 参数或当前加速度参考不足，姿态尚未完成有效更新。
 */
int8 AHRS_Update6Axis(int16 ax, int16 ay, int16 az,
                      int16 gx, int16 gy, int16 gz,
                      u16 dt_ms);

/**
 * @brief 使用传感器原始轴六轴数据更新姿态。
 *
 * 先按 `AHRS_IMU_BODY_*` 轴映射转换到机体系，再调用 `AHRS_Update6Axis()`。
 *
 * @param raw_ax 传感器原始 X 轴加速度。
 * @param raw_ay 传感器原始 Y 轴加速度。
 * @param raw_az 传感器原始 Z 轴加速度。
 * @param raw_gx 传感器原始 X 轴陀螺。
 * @param raw_gy 传感器原始 Y 轴陀螺。
 * @param raw_gz 传感器原始 Z 轴陀螺。
 * @param dt_ms 本次更新间隔，单位 ms，不能为 0。
 * @return 0 更新成功。
 * @return -1 姿态尚未完成有效更新。
 */
int8 AHRS_UpdateRaw6Axis(int16 raw_ax, int16 raw_ay, int16 raw_az,
                         int16 raw_gx, int16 raw_gy, int16 raw_gz,
                         u16 dt_ms);

/**
 * @brief 输入机体系磁力计数据。
 *
 * 该函数只更新磁力计归一化缓存和磁航向诊断标志，不直接读取磁力计硬件。
 *
 * @param mx 机体系 X 轴磁场原始值。
 * @param my 机体系 Y 轴磁场原始值。
 * @param mz 机体系 Z 轴磁场原始值。
 * @return 0 磁力计数据有效。
 * @return -1 磁场模长过小或归一化失败。
 */
int8 AHRS_UpdateMag(int16 mx, int16 my, int16 mz);

/**
 * @brief 使用传感器原始轴磁力计数据更新磁力计缓存。
 *
 * 先按 `AHRS_MAG_BODY_*` 轴映射转换到机体系，再调用 `AHRS_UpdateMag()`。
 *
 * @return 0 磁力计数据有效。
 * @return -1 磁场数据无效。
 */
int8 AHRS_UpdateRawMag(int16 raw_mx, int16 raw_my, int16 raw_mz);

/**
 * @brief 获取最近一次姿态输出快照。
 *
 * 返回指针由 AHRS 组件持有，调用方只读，不需要释放。角度单位均为 deg*100。
 *
 * @return 指向内部 `AHRS_State_t` 的只读指针。
 */
const AHRS_State_t *AHRS_GetState(void);

/**
 * @brief 查询 AHRS 是否已经完成初始姿态建立。
 *
 * @return 1 已 ready。
 * @return 0 尚未 ready。
 */
u8 AHRS_IsReady(void);

/**
 * @brief 将 IMU 原始轴映射为机体系轴。
 *
 * 输出指针为 NULL 时函数直接返回。该接口用于集中维护安装方向，不访问硬件。
 */
void AHRS_MapRawToBody(int16 raw_x, int16 raw_y, int16 raw_z,
                       int16 *body_x, int16 *body_y, int16 *body_z);

/**
 * @brief 将磁力计原始轴映射为机体系轴。
 *
 * 输出指针为 NULL 时函数直接返回。该接口由 AHRS 和罗盘组件共享安装方向配置。
 */
void AHRS_MapRawMagToBody(int16 raw_x, int16 raw_y, int16 raw_z,
                          int16 *body_x, int16 *body_y, int16 *body_z);

#endif
