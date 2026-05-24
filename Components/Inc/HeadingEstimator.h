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

void Heading_Init(HeadingEstimator_t *h);
void Heading_ResetZero(HeadingEstimator_t *h);
void Heading_SetHeadingDeg(HeadingEstimator_t *h, float heading_deg);

void Heading_Update(HeadingEstimator_t *h,
                    float gyro_z_dps,
                    float yaw_mag_deg,
                    u8 mag_valid,
                    u8 static_flag,
                    float dt);

float Heading_GetDeg(const HeadingEstimator_t *h);
int32 Heading_GetDeg100(const HeadingEstimator_t *h);
int32 Heading_GetRelativeDeg100(const HeadingEstimator_t *h);
int32 Heading_GetGyroDeg100(const HeadingEstimator_t *h);
int32 Heading_GetMagDeg100(const HeadingEstimator_t *h);
int32 Heading_GetBiasDps100(const HeadingEstimator_t *h);

#endif
