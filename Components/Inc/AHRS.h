/**
 * @file    AHRS.h
 * @brief   AHRS public interface
 * @author  boweny
 * @date    2026-05-11
 * @version v1.2
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

void AHRS_Reset(void);
int8 AHRS_Update6Axis(int16 ax, int16 ay, int16 az,
                      int16 gx, int16 gy, int16 gz,
                      u16 dt_ms);
int8 AHRS_UpdateRaw6Axis(int16 raw_ax, int16 raw_ay, int16 raw_az,
                         int16 raw_gx, int16 raw_gy, int16 raw_gz,
                         u16 dt_ms);
int8 AHRS_UpdateMag(int16 mx, int16 my, int16 mz);
int8 AHRS_UpdateRawMag(int16 raw_mx, int16 raw_my, int16 raw_mz);
const AHRS_State_t *AHRS_GetState(void);
u8 AHRS_IsReady(void);
void AHRS_MapRawToBody(int16 raw_x, int16 raw_y, int16 raw_z,
                       int16 *body_x, int16 *body_y, int16 *body_z);
void AHRS_MapRawMagToBody(int16 raw_x, int16 raw_y, int16 raw_z,
                          int16 *body_x, int16 *body_y, int16 *body_z);

#endif
