#include "HeadingEstimator.h"

#define HEADING_FLOAT_ABS(value) \
    (((value) < 0.0f) ? (-(value)) : (value))

#define HEADING_WRAP360_INPLACE(value) \
    do { \
        while ((value) >= 360.0f) { \
            (value) -= 360.0f; \
        } \
        while ((value) < 0.0f) { \
            (value) += 360.0f; \
        } \
    } while (0)

#define HEADING_WRAP180_INPLACE(value) \
    do { \
        while ((value) >= 180.0f) { \
            (value) -= 360.0f; \
        } \
        while ((value) < -180.0f) { \
            (value) += 360.0f; \
        } \
    } while (0)

#define HEADING_CLAMP_BIAS_INPLACE(value) \
    do { \
        if ((value) > HEADING_BIAS_LIMIT_DPS) { \
            (value) = HEADING_BIAS_LIMIT_DPS; \
        } else if ((value) < -HEADING_BIAS_LIMIT_DPS) { \
            (value) = -HEADING_BIAS_LIMIT_DPS; \
        } \
    } while (0)

static int32 Heading_RoundDeg100(float angle_deg)
{
    float scaled;

    scaled = angle_deg * 100.0f;
    if (scaled >= 0.0f) {
        return (int32)(scaled + 0.5f);
    }
    return (int32)(scaled - 0.5f);
}

void Heading_Init(HeadingEstimator_t *h)
{
    if (h == 0) {
        return;
    }

    h->heading_deg = 0.0f;
    h->heading_zero_deg = 0.0f;
    h->gyro_z_bias_dps = 0.0f;
    h->yaw_gyro_deg = 0.0f;
    h->yaw_mag_deg = 0.0f;
    h->heading_pred_deg = 0.0f;
    h->heading_err_deg = 0.0f;
    h->static_mag_ref_deg = 0.0f;
    h->mag_confidence = 0.0f;
    h->last_yaw_mag_deg = 0.0f;
    h->mag_valid = 0U;
    h->mag_used = 0U;
    h->static_flag = 0U;
    h->static_mag_ref_valid = 0U;
    h->raw_mag_valid = 0U;
    h->last_mag_sample_valid = 0U;
}

void Heading_ResetZero(HeadingEstimator_t *h)
{
    if (h == 0) {
        return;
    }

    HEADING_WRAP360_INPLACE(h->heading_deg);
    h->heading_zero_deg = h->heading_deg;
}

void Heading_SetHeadingDeg(HeadingEstimator_t *h, float heading_deg)
{
    if (h == 0) {
        return;
    }

    HEADING_WRAP360_INPLACE(heading_deg);
    h->heading_deg = heading_deg;
    h->yaw_gyro_deg = heading_deg;
    h->yaw_mag_deg = heading_deg;
    h->heading_pred_deg = heading_deg;
    h->heading_err_deg = 0.0f;
    h->static_mag_ref_deg = heading_deg;
    h->mag_valid = 0U;
    h->mag_used = 0U;
    h->static_mag_ref_valid = 0U;
    h->raw_mag_valid = 0U;
    h->last_mag_sample_valid = 0U;
#if HEADING_USE_INTERNAL_BIAS
    h->gyro_z_bias_dps = 0.0f;
#else
    h->gyro_z_bias_dps = 0.0f;
#endif
}

void Heading_Update(HeadingEstimator_t *h,
                    float gyro_z_dps,
                    float yaw_mag_deg,
                    u8 mag_valid,
                    u8 static_flag,
                    float dt)
{
    float value;
    float heading_pred;
    float gz_corr;
    float err;
    float static_err;
    float kmag;
    u8 raw_mag_valid;
    u8 mag_sample_valid;
    u8 mag_used;

    if ((h == 0) || (dt <= 0.0f)) {
        return;
    }

    h->static_flag = static_flag;

    value = yaw_mag_deg;
    HEADING_WRAP360_INPLACE(value);
    h->yaw_mag_deg = value;
    h->heading_err_deg = 0.0f;

#if HEADING_USE_INTERNAL_BIAS
    gz_corr = gyro_z_dps - h->gyro_z_bias_dps;
#else
    h->gyro_z_bias_dps = 0.0f;
    gz_corr = gyro_z_dps;
#endif
    if (static_flag && (HEADING_FLOAT_ABS(gz_corr) < HEADING_GYRO_DEADZONE_DPS)) {
        gz_corr = 0.0f;
    }

    heading_pred = h->heading_deg + (gz_corr * dt);
    HEADING_WRAP360_INPLACE(heading_pred);
    h->heading_deg = heading_pred;
    h->heading_pred_deg = heading_pred;

    h->yaw_gyro_deg += (gz_corr * dt);
    HEADING_WRAP360_INPLACE(h->yaw_gyro_deg);

    raw_mag_valid = mag_valid ? 1U : 0U;
    mag_used = 0U;
    mag_sample_valid = 0U;
    h->raw_mag_valid = raw_mag_valid;
    h->mag_valid = raw_mag_valid;
    h->mag_used = 0U;

    if (!static_flag) {
        h->static_mag_ref_valid = 0U;
        h->last_mag_sample_valid = 0U;
    }

    if (raw_mag_valid) {
        if (h->last_mag_sample_valid) {
            err = value - h->last_yaw_mag_deg;
            HEADING_WRAP180_INPLACE(err);
            if (HEADING_FLOAT_ABS(err) > HEADING_MAG_JUMP_GATE_DEG) {
                raw_mag_valid = 0U;
            } else {
                mag_sample_valid = 1U;
            }
        } else {
            mag_sample_valid = 1U;
        }
    } else {
        h->last_mag_sample_valid = 0U;
        h->static_mag_ref_valid = 0U;
    }

    if (mag_sample_valid) {
        err = value - heading_pred;
        HEADING_WRAP180_INPLACE(err);
        h->heading_err_deg = err;

        if (static_flag) {
            if (!h->static_mag_ref_valid) {
                h->static_mag_ref_deg = value;
                h->static_mag_ref_valid = 1U;
            }

            static_err = value - h->static_mag_ref_deg;
            HEADING_WRAP180_INPLACE(static_err);
            if ((HEADING_FLOAT_ABS(static_err) <= HEADING_STATIC_MAG_STABLE_DEG) &&
                (HEADING_FLOAT_ABS(err) <= HEADING_MAG_ERR_GATE_DEG) &&
                !HEADING_DBG_FORCE_MAG_OFF) {
                kmag = HEADING_KMAG_STATIC;
                h->heading_deg = heading_pred + (kmag * err);
                HEADING_WRAP360_INPLACE(h->heading_deg);
                mag_used = 1U;
            }
        } else {
            /* While moving, keep the north reference by gyro integration only.
             * GPS updates steer the target course; magnetometer samples are
             * observed but not fused until the boat is stationary again. */
            h->static_mag_ref_valid = 0U;
        }

        if (mag_used) {
            h->last_yaw_mag_deg = value;
            h->last_mag_sample_valid = 1U;
        }
    }

    h->mag_used = mag_used;
    h->mag_confidence = mag_used ? 1.0f : 0.0f;
}

float Heading_GetDeg(const HeadingEstimator_t *h)
{
    float angle_deg;

    if (h == 0) {
        return 0.0f;
    }

    angle_deg = h->heading_deg;
    HEADING_WRAP360_INPLACE(angle_deg);
    return angle_deg;
}

int32 Heading_GetDeg100(const HeadingEstimator_t *h)
{
    float angle_deg;

    if (h == 0) {
        return 0L;
    }

    angle_deg = h->heading_deg;
    HEADING_WRAP360_INPLACE(angle_deg);
    return Heading_RoundDeg100(angle_deg);
}

int32 Heading_GetRelativeDeg100(const HeadingEstimator_t *h)
{
    float angle_deg;

    if (h == 0) {
        return 0L;
    }

    angle_deg = h->heading_deg;
    HEADING_WRAP360_INPLACE(angle_deg);
    angle_deg -= h->heading_zero_deg;
    HEADING_WRAP180_INPLACE(angle_deg);
    return Heading_RoundDeg100(angle_deg);
}

int32 Heading_GetGyroDeg100(const HeadingEstimator_t *h)
{
    float angle_deg;

    if (h == 0) {
        return 0L;
    }

    angle_deg = h->yaw_gyro_deg;
    HEADING_WRAP360_INPLACE(angle_deg);
    return Heading_RoundDeg100(angle_deg);
}

int32 Heading_GetMagDeg100(const HeadingEstimator_t *h)
{
    float angle_deg;

    if (h == 0) {
        return 0L;
    }

    angle_deg = h->yaw_mag_deg;
    HEADING_WRAP360_INPLACE(angle_deg);
    return Heading_RoundDeg100(angle_deg);
}

int32 Heading_GetBiasDps100(const HeadingEstimator_t *h)
{
    if (h == 0) {
        return 0L;
    }

#if !HEADING_USE_INTERNAL_BIAS
    return 0L;
#else
    return Heading_RoundDeg100(h->gyro_z_bias_dps);
#endif
}
