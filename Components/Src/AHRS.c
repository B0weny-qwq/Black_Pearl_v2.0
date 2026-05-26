/**
 * @file    AHRS.c
 * @brief   Quaternion AHRS implementation
 * @author  boweny
 * @date    2026-05-11
 * @version v1.2
 *
 * Internal state uses a float quaternion. External outputs stay on the
 * existing deg*100 / deg/s*100 interface so MainLoop logs and the host
 * viewer do not need to change.
 */

#include "AHRS.h"

#define AHRS_ANGLE_FULL_DEG100          36000L
#define AHRS_ANGLE_HALF_DEG100          18000L
#define AHRS_ANGLE_QUARTER_DEG100       9000L
#define AHRS_ATAN_Q                     1024L
#define AHRS_ACC_MIN_BOOT_NORM          512U
#define AHRS_ACC_REF_RELEARN_COUNT      64U
#define AHRS_FLOAT_EPSILON              0.000001f
#define AHRS_UNIT_SCALE                 10000.0f
#define AHRS_GYRO_RAW_TO_RAD            0.000136353848f

typedef struct
{
    int32 x;
    int32 y;
    int32 z;
    u8 initialized;
} AHRS_Lpf3_t;

typedef struct
{
    float q0;
    float q1;
    float q2;
    float q3;

    float integral_x;
    float integral_y;
    float integral_z;

    float mag_x;
    float mag_y;
    float mag_z;
    float yaw_gyro_deg100;
    int16 yaw_mag_deg100;

    u32 acc_ref_sum;
    u16 acc_ref_count;
    u16 acc_1g_ref;
    u16 acc_ref_invalid_still_count;

    int32 gyro_bias_sum_x;
    int32 gyro_bias_sum_y;
    int32 gyro_bias_sum_z;
    int32 gyro_bias_q8_x;
    int32 gyro_bias_q8_y;
    int32 gyro_bias_q8_z;
    u16 gyro_bias_count;
    int16 gyro_bias_x;
    int16 gyro_bias_y;
    int16 gyro_bias_z;
    u8 gyro_bias_ready;

    AHRS_Lpf3_t gyro_lpf;
    AHRS_Lpf3_t mag_lpf;

    AHRS_State_t state;
    u8 mag_valid;
    u8 ready;
} AHRS_Context_t;

static EF_LARGE_DATA AHRS_Context_t ahrs_ctx;

static u32 AHRS_Abs16(int16 value)
{
    if (value < 0) {
        return (u32)(-(int32)value);
    }
    return (u32)value;
}

static int32 AHRS_Abs32(int32 value)
{
    if (value < 0) {
        return -value;
    }
    return value;
}

static float AHRS_AbsFloat(float value)
{
    if (value < 0.0f) {
        return -value;
    }
    return value;
}

static float AHRS_ClampFloat(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int16 AHRS_WrapDeg100(int32 angle)
{
    while (angle >= AHRS_ANGLE_HALF_DEG100) {
        angle -= AHRS_ANGLE_FULL_DEG100;
    }
    while (angle < -AHRS_ANGLE_HALF_DEG100) {
        angle += AHRS_ANGLE_FULL_DEG100;
    }
    return (int16)angle;
}

static int16 AHRS_SelectMappedAxis(int16 raw_x, int16 raw_y, int16 raw_z,
                                   u8 from_axis, int8 sign)
{
    int16 value;

    value = raw_x;
    if (from_axis == AHRS_AXIS_RAW_Y) {
        value = raw_y;
    } else if (from_axis == AHRS_AXIS_RAW_Z) {
        value = raw_z;
    }

    if (sign < 0) {
        value = (int16)(-value);
    }
    return value;
}

static u32 AHRS_Norm3Approx(int16 x, int16 y, int16 z)
{
    u32 a;
    u32 b;
    u32 c;
    u32 t;

    a = AHRS_Abs16(x);
    b = AHRS_Abs16(y);
    c = AHRS_Abs16(z);

    if (a < b) {
        t = a; a = b; b = t;
    }
    if (a < c) {
        t = a; a = c; c = t;
    }
    if (b < c) {
        t = b; b = c; c = t;
    }

    return a + ((b * 3U) >> 3) + (c >> 2);
}

static int16 AHRS_Atan01Q10ToDeg100(u16 z_q10)
{
    int32 z;
    int32 curve;
    int32 angle;

    if (z_q10 > AHRS_ATAN_Q) {
        z_q10 = (u16)AHRS_ATAN_Q;
    }

    z = (int32)z_q10;
    curve = 4500L + ((1564L * (AHRS_ATAN_Q - z)) / AHRS_ATAN_Q);
    angle = (z * curve) / AHRS_ATAN_Q;
    return (int16)angle;
}

static int16 AHRS_Atan2Deg100(int32 y, int32 x)
{
    u32 ax;
    u32 ay;
    u16 z_q10;
    int32 base;
    int32 angle;

    ax = (u32)AHRS_Abs32(x);
    ay = (u32)AHRS_Abs32(y);

    if ((ax == 0U) && (ay == 0U)) {
        return 0;
    }

    if (ax >= ay) {
        z_q10 = (u16)((ay * AHRS_ATAN_Q) / ax);
        base = AHRS_Atan01Q10ToDeg100(z_q10);
        if (x >= 0) {
            angle = base;
        } else {
            angle = AHRS_ANGLE_HALF_DEG100 - base;
        }
    } else {
        z_q10 = (u16)((ax * AHRS_ATAN_Q) / ay);
        base = AHRS_Atan01Q10ToDeg100(z_q10);
        if (x >= 0) {
            angle = AHRS_ANGLE_QUARTER_DEG100 - base;
        } else {
            angle = AHRS_ANGLE_QUARTER_DEG100 + base;
        }
    }

    if (y < 0) {
        angle = -angle;
    }

    return AHRS_WrapDeg100(angle);
}

static float AHRS_SqrtFloat(float value)
{
    float x;
    u8 i;

    if (value <= 0.0f) {
        return 0.0f;
    }

    x = value;
    if (x < 1.0f) {
        x = 1.0f;
    }

    for (i = 0U; i < 6U; i++) {
        x = 0.5f * (x + (value / x));
    }
    return x;
}

static u8 AHRS_NormalizeVector3(float *x, float *y, float *z)
{
    float norm;
    float inv_norm;

    norm = (*x * *x) + (*y * *y) + (*z * *z);
    if (norm <= AHRS_FLOAT_EPSILON) {
        return 0U;
    }

    inv_norm = 1.0f / AHRS_SqrtFloat(norm);
    *x *= inv_norm;
    *y *= inv_norm;
    *z *= inv_norm;
    return 1U;
}

static int16 AHRS_LpfAxisUpdate(int32 *state, int16 input, u8 shift)
{
    int32 target;

    target = (int32)input << 8;
    if (shift == 0U) {
        *state = target;
    } else {
        *state += ((target - *state) >> shift);
    }
    return (int16)(*state >> 8);
}

static void AHRS_Lpf3Reset(AHRS_Lpf3_t *lpf)
{
    if (lpf == 0) {
        return;
    }

    lpf->x = 0;
    lpf->y = 0;
    lpf->z = 0;
    lpf->initialized = 0;
}

static void AHRS_Lpf3Apply(AHRS_Lpf3_t *lpf, u8 shift,
                           int16 in_x, int16 in_y, int16 in_z,
                           int16 *out_x, int16 *out_y, int16 *out_z)
{
    if (!lpf->initialized) {
        lpf->x = (int32)in_x << 8;
        lpf->y = (int32)in_y << 8;
        lpf->z = (int32)in_z << 8;
        lpf->initialized = 1;
        *out_x = in_x;
        *out_y = in_y;
        *out_z = in_z;
        return;
    }

    *out_x = AHRS_LpfAxisUpdate(&lpf->x, in_x, shift);
    *out_y = AHRS_LpfAxisUpdate(&lpf->y, in_y, shift);
    *out_z = AHRS_LpfAxisUpdate(&lpf->z, in_z, shift);
}

static void AHRS_UpdateAccReference(u32 acc_norm, u8 gyro_still)
{
    int32 diff;
    int32 tolerance;

    if ((acc_norm < AHRS_ACC_MIN_BOOT_NORM) || (!gyro_still)) {
        ahrs_ctx.acc_ref_invalid_still_count = 0U;
        return;
    }

    if (ahrs_ctx.acc_ref_count < AHRS_ACC_REF_SAMPLE_COUNT) {
        ahrs_ctx.acc_ref_sum += acc_norm;
        ahrs_ctx.acc_ref_count++;
        if (ahrs_ctx.acc_ref_count >= AHRS_ACC_REF_SAMPLE_COUNT) {
            ahrs_ctx.acc_1g_ref =
                (u16)(ahrs_ctx.acc_ref_sum / AHRS_ACC_REF_SAMPLE_COUNT);
        }
        return;
    }

    if (ahrs_ctx.acc_1g_ref == 0U) {
        return;
    }

    diff = (int32)acc_norm - (int32)ahrs_ctx.acc_1g_ref;
    tolerance =
        ((int32)ahrs_ctx.acc_1g_ref * AHRS_ACC_NORM_TOLERANCE_PERCENT) / 100L;
    if (AHRS_Abs32(diff) < tolerance) {
        ahrs_ctx.acc_1g_ref =
            (u16)((((u32)ahrs_ctx.acc_1g_ref * 255U) + acc_norm) >> 8);
        ahrs_ctx.acc_ref_invalid_still_count = 0U;
        return;
    }

    if (ahrs_ctx.acc_ref_invalid_still_count < 65535U) {
        ahrs_ctx.acc_ref_invalid_still_count++;
    }

    if (ahrs_ctx.acc_ref_invalid_still_count >= AHRS_ACC_REF_RELEARN_COUNT) {
        ahrs_ctx.acc_ref_sum = acc_norm;
        ahrs_ctx.acc_ref_count = 1U;
        ahrs_ctx.acc_1g_ref = 0U;
        ahrs_ctx.acc_ref_invalid_still_count = 0U;
    }
}

static u8 AHRS_IsAccNormValid(u32 acc_norm)
{
    u32 min_norm;
    u32 max_norm;

    if (acc_norm < AHRS_ACC_MIN_BOOT_NORM) {
        return 0U;
    }

    if (ahrs_ctx.acc_1g_ref == 0U) {
        return 1U;
    }

    min_norm =
        ((u32)ahrs_ctx.acc_1g_ref * (100U - AHRS_ACC_NORM_TOLERANCE_PERCENT)) /
        100U;
    max_norm =
        ((u32)ahrs_ctx.acc_1g_ref * (100U + AHRS_ACC_NORM_TOLERANCE_PERCENT)) /
        100U;

    if ((acc_norm < min_norm) || (acc_norm > max_norm)) {
        return 0U;
    }
    return 1U;
}

static u8 AHRS_IsGyroStill(int16 gx, int16 gy, int16 gz)
{
    int32 threshold;

    threshold =
        (AHRS_GYRO_LSB_PER_DPS * AHRS_GYRO_STILL_DPS100) / 100L;

    if (AHRS_Abs16(gx) > (u32)threshold) {
        return 0U;
    }
    if (AHRS_Abs16(gy) > (u32)threshold) {
        return 0U;
    }
    if (AHRS_Abs16(gz) > (u32)threshold) {
        return 0U;
    }
    return 1U;
}

static void AHRS_ResetGyroBiasAccum(void)
{
    ahrs_ctx.gyro_bias_sum_x = 0;
    ahrs_ctx.gyro_bias_sum_y = 0;
    ahrs_ctx.gyro_bias_sum_z = 0;
    ahrs_ctx.gyro_bias_count = 0;
}

static void AHRS_UpdateGyroBias(int16 gx, int16 gy, int16 gz, u8 gyro_still)
{
    if (ahrs_ctx.gyro_bias_ready) {
        if (!gyro_still) {
            return;
        }

        ahrs_ctx.gyro_bias_x =
            AHRS_LpfAxisUpdate(&ahrs_ctx.gyro_bias_q8_x,
                               gx, AHRS_GYRO_BIAS_TRACK_SHIFT);
        ahrs_ctx.gyro_bias_y =
            AHRS_LpfAxisUpdate(&ahrs_ctx.gyro_bias_q8_y,
                               gy, AHRS_GYRO_BIAS_TRACK_SHIFT);
        ahrs_ctx.gyro_bias_z =
            AHRS_LpfAxisUpdate(&ahrs_ctx.gyro_bias_q8_z,
                               gz, AHRS_GYRO_BIAS_TRACK_SHIFT);
        return;
    }

    if (!gyro_still) {
        AHRS_ResetGyroBiasAccum();
        return;
    }

    ahrs_ctx.gyro_bias_sum_x += gx;
    ahrs_ctx.gyro_bias_sum_y += gy;
    ahrs_ctx.gyro_bias_sum_z += gz;
    ahrs_ctx.gyro_bias_count++;

    if (ahrs_ctx.gyro_bias_count >= AHRS_GYRO_BIAS_SAMPLE_COUNT) {
        ahrs_ctx.gyro_bias_x =
            (int16)(ahrs_ctx.gyro_bias_sum_x / AHRS_GYRO_BIAS_SAMPLE_COUNT);
        ahrs_ctx.gyro_bias_y =
            (int16)(ahrs_ctx.gyro_bias_sum_y / AHRS_GYRO_BIAS_SAMPLE_COUNT);
        ahrs_ctx.gyro_bias_z =
            (int16)(ahrs_ctx.gyro_bias_sum_z / AHRS_GYRO_BIAS_SAMPLE_COUNT);
        ahrs_ctx.gyro_bias_q8_x = (int32)ahrs_ctx.gyro_bias_x << 8;
        ahrs_ctx.gyro_bias_q8_y = (int32)ahrs_ctx.gyro_bias_y << 8;
        ahrs_ctx.gyro_bias_q8_z = (int32)ahrs_ctx.gyro_bias_z << 8;
        ahrs_ctx.gyro_bias_ready = 1;
    }
}

static int16 AHRS_ApplyGyroDeadband(int16 gyro)
{
    int32 deadband;
    int32 value;

    deadband =
        (AHRS_GYRO_LSB_PER_DPS * AHRS_GYRO_DEADBAND_DPS100) / 100L;
    if (deadband < 1L) {
        deadband = 1L;
    }

    value = gyro;
    if (AHRS_Abs32(value) <= deadband) {
        return 0;
    }

    if (value > 0) {
        value -= deadband;
    } else {
        value += deadband;
    }

    return (int16)value;
}

static int16 AHRS_GyroToDps100(int16 gyro)
{
    return (int16)(((int32)gyro * 100L) / AHRS_GYRO_LSB_PER_DPS);
}

static void AHRS_NormalizeQuaternion(void)
{
    float norm;
    float inv_norm;

    norm = (ahrs_ctx.q0 * ahrs_ctx.q0) +
           (ahrs_ctx.q1 * ahrs_ctx.q1) +
           (ahrs_ctx.q2 * ahrs_ctx.q2) +
           (ahrs_ctx.q3 * ahrs_ctx.q3);
    if (norm <= AHRS_FLOAT_EPSILON) {
        ahrs_ctx.q0 = 1.0f;
        ahrs_ctx.q1 = 0.0f;
        ahrs_ctx.q2 = 0.0f;
        ahrs_ctx.q3 = 0.0f;
        return;
    }

    inv_norm = 1.0f / AHRS_SqrtFloat(norm);
    ahrs_ctx.q0 *= inv_norm;
    ahrs_ctx.q1 *= inv_norm;
    ahrs_ctx.q2 *= inv_norm;
    ahrs_ctx.q3 *= inv_norm;
}

static void AHRS_InitQuaternionFromAccel(float ax, float ay, float az)
{
    ahrs_ctx.q0 = 1.0f + az;
    ahrs_ctx.q1 = -ay;
    ahrs_ctx.q2 = ax;
    ahrs_ctx.q3 = 0.0f;

    if ((AHRS_AbsFloat(ahrs_ctx.q0) <= AHRS_FLOAT_EPSILON) &&
        (AHRS_AbsFloat(ahrs_ctx.q1) <= AHRS_FLOAT_EPSILON) &&
        (AHRS_AbsFloat(ahrs_ctx.q2) <= AHRS_FLOAT_EPSILON)) {
        ahrs_ctx.q0 = 0.0f;
        ahrs_ctx.q1 = 1.0f;
        ahrs_ctx.q2 = 0.0f;
        ahrs_ctx.q3 = 0.0f;
    }

    AHRS_NormalizeQuaternion();
}

static int16 AHRS_Atan2Deg100FromFloat(float y, float x)
{
    int32 sy;
    int32 sx;
    float scaled;

    scaled = AHRS_ClampFloat(y, -3.2f, 3.2f) * AHRS_UNIT_SCALE;
    if (scaled >= 0.0f) {
        scaled += 0.5f;
    } else {
        scaled -= 0.5f;
    }
    sy = (int32)scaled;

    scaled = AHRS_ClampFloat(x, -3.2f, 3.2f) * AHRS_UNIT_SCALE;
    if (scaled >= 0.0f) {
        scaled += 0.5f;
    } else {
        scaled -= 0.5f;
    }
    sx = (int32)scaled;

    return AHRS_Atan2Deg100(sy, sx);
}

static void AHRS_QuaternionToEulerDeg100(int16 *roll, int16 *pitch, int16 *yaw)
{
    float sinr_cosp;
    float cosr_cosp;
    float sinp;
    float pitch_den;
    float siny_cosp;
    float cosy_cosp;

    sinr_cosp = (2.0f * ahrs_ctx.q0 * ahrs_ctx.q1) +
                (2.0f * ahrs_ctx.q2 * ahrs_ctx.q3);
    cosr_cosp = 1.0f -
                (2.0f * ((ahrs_ctx.q1 * ahrs_ctx.q1) +
                         (ahrs_ctx.q2 * ahrs_ctx.q2)));

    sinp = (2.0f * ahrs_ctx.q0 * ahrs_ctx.q2) -
           (2.0f * ahrs_ctx.q3 * ahrs_ctx.q1);
    sinp = AHRS_ClampFloat(sinp, -1.0f, 1.0f);
    pitch_den = 1.0f - (sinp * sinp);
    if (pitch_den < 0.0f) {
        pitch_den = 0.0f;
    }

    siny_cosp = (2.0f * ahrs_ctx.q0 * ahrs_ctx.q3) +
                (2.0f * ahrs_ctx.q1 * ahrs_ctx.q2);
    cosy_cosp = 1.0f -
                (2.0f * ((ahrs_ctx.q2 * ahrs_ctx.q2) +
                         (ahrs_ctx.q3 * ahrs_ctx.q3)));

    *roll = AHRS_Atan2Deg100FromFloat(sinr_cosp, cosr_cosp);
    *pitch = AHRS_Atan2Deg100FromFloat(sinp, AHRS_SqrtFloat(pitch_den));
    *yaw = AHRS_Atan2Deg100FromFloat(siny_cosp, cosy_cosp);
}

static void AHRS_UpdateYawMagDiagnostic(void)
{
    float up_x;
    float up_y;
    float up_z;
    float dot_mu;
    float north_x;
    float north_y;
    float north_z;

    if (!ahrs_ctx.mag_valid) {
        ahrs_ctx.yaw_mag_deg100 = 0;
        return;
    }

    up_x = (2.0f * ahrs_ctx.q1 * ahrs_ctx.q3) -
           (2.0f * ahrs_ctx.q0 * ahrs_ctx.q2);
    up_y = (2.0f * ahrs_ctx.q0 * ahrs_ctx.q1) +
           (2.0f * ahrs_ctx.q2 * ahrs_ctx.q3);
    up_z = (ahrs_ctx.q0 * ahrs_ctx.q0) -
           (ahrs_ctx.q1 * ahrs_ctx.q1) -
           (ahrs_ctx.q2 * ahrs_ctx.q2) +
           (ahrs_ctx.q3 * ahrs_ctx.q3);

    dot_mu = (ahrs_ctx.mag_x * up_x) +
             (ahrs_ctx.mag_y * up_y) +
             (ahrs_ctx.mag_z * up_z);
    north_x = ahrs_ctx.mag_x - (dot_mu * up_x);
    north_y = ahrs_ctx.mag_y - (dot_mu * up_y);
    north_z = ahrs_ctx.mag_z - (dot_mu * up_z);

    if (!AHRS_NormalizeVector3(&north_x, &north_y, &north_z)) {
        ahrs_ctx.yaw_mag_deg100 = 0;
        return;
    }

    ahrs_ctx.yaw_mag_deg100 = AHRS_Atan2Deg100FromFloat(-north_y, north_x);
}

static void AHRS_UpdateOutput(u16 dt_ms, u32 acc_norm, u8 flags)
{
    int16 roll;
    int16 pitch;
    int16 yaw;

    if (ahrs_ctx.mag_valid) {
        flags |= AHRS_FLAG_MAG_VALID;
    }

    AHRS_QuaternionToEulerDeg100(&roll, &pitch, &yaw);
    AHRS_UpdateYawMagDiagnostic();
    ahrs_ctx.state.roll_deg100 = AHRS_WrapDeg100(roll);
    ahrs_ctx.state.pitch_deg100 = AHRS_WrapDeg100(pitch);
    ahrs_ctx.state.yaw_deg100 = AHRS_WrapDeg100(yaw);
    ahrs_ctx.state.yaw_gyro_deg100 =
        AHRS_WrapDeg100((int32)ahrs_ctx.yaw_gyro_deg100);
    ahrs_ctx.state.yaw_mag_deg100 = AHRS_WrapDeg100(ahrs_ctx.yaw_mag_deg100);

    ahrs_ctx.state.acc_norm = (acc_norm > 65535U) ? 65535U : (u16)acc_norm;
    ahrs_ctx.state.dt_ms = dt_ms;
    ahrs_ctx.state.flags = flags;
    ahrs_ctx.state.update_count++;
}

static void AHRS_MahonyUpdate(float gx, float gy, float gz,
                              float ax, float ay, float az,
                              u8 acc_valid, float dt_s)
{
    float q0;
    float q1;
    float q2;
    float q3;
    float q0q0;
    float q0q1;
    float q0q2;
    float q0q3;
    float q1q1;
    float q1q2;
    float q1q3;
    float q2q2;
    float q2q3;
    float q3q3;
    float halfvx;
    float halfvy;
    float halfvz;
    float halfwx;
    float halfwy;
    float halfwz;
    float ex_acc;
    float ey_acc;
    float ez_acc;
    float ex_mag;
    float ey_mag;
    float ez_mag;
    float corr_x;
    float corr_y;
    float corr_z;
    float hx;
    float hy;
    float bx;
    float bz;
    float half_dt;
    float qa;
    float qb;
    float qc;
    u8 use_mag;

    q0 = ahrs_ctx.q0;
    q1 = ahrs_ctx.q1;
    q2 = ahrs_ctx.q2;
    q3 = ahrs_ctx.q3;

    q0q0 = q0 * q0;
    q0q1 = q0 * q1;
    q0q2 = q0 * q2;
    q0q3 = q0 * q3;
    q1q1 = q1 * q1;
    q1q2 = q1 * q2;
    q1q3 = q1 * q3;
    q2q2 = q2 * q2;
    q2q3 = q2 * q3;
    q3q3 = q3 * q3;

    ex_acc = 0.0f;
    ey_acc = 0.0f;
    ez_acc = 0.0f;
    ex_mag = 0.0f;
    ey_mag = 0.0f;
    ez_mag = 0.0f;
    use_mag = 0U;

    if (acc_valid) {
        halfvx = q1q3 - q0q2;
        halfvy = q0q1 + q2q3;
        halfvz = q0q0 - 0.5f + q3q3;

        ex_acc = (ay * halfvz) - (az * halfvy);
        ey_acc = (az * halfvx) - (ax * halfvz);
        ez_acc = (ax * halfvy) - (ay * halfvx);
    }

#if AHRS_MAG_ENABLE
    if (acc_valid && ahrs_ctx.mag_valid) {
        use_mag = 1U;

        hx = (2.0f * ahrs_ctx.mag_x * (0.5f - q2q2 - q3q3)) +
             (2.0f * ahrs_ctx.mag_y * (q1q2 - q0q3)) +
             (2.0f * ahrs_ctx.mag_z * (q1q3 + q0q2));
        hy = (2.0f * ahrs_ctx.mag_x * (q1q2 + q0q3)) +
             (2.0f * ahrs_ctx.mag_y * (0.5f - q1q1 - q3q3)) +
             (2.0f * ahrs_ctx.mag_z * (q2q3 - q0q1));
        bx = AHRS_SqrtFloat((hx * hx) + (hy * hy));
        bz = (2.0f * ahrs_ctx.mag_x * (q1q3 - q0q2)) +
             (2.0f * ahrs_ctx.mag_y * (q2q3 + q0q1)) +
             (2.0f * ahrs_ctx.mag_z * (0.5f - q1q1 - q2q2));

        halfwx = (bx * (0.5f - q2q2 - q3q3)) + (bz * (q1q3 - q0q2));
        halfwy = (bx * (q1q2 - q0q3)) + (bz * (q0q1 + q2q3));
        halfwz = (bx * (q0q2 + q1q3)) + (bz * (0.5f - q1q1 - q2q2));

        ex_mag = (ahrs_ctx.mag_y * halfwz) - (ahrs_ctx.mag_z * halfwy);
        ey_mag = (ahrs_ctx.mag_z * halfwx) - (ahrs_ctx.mag_x * halfwz);
        ez_mag = (ahrs_ctx.mag_x * halfwy) - (ahrs_ctx.mag_y * halfwx);
    }
#endif

    corr_x = (AHRS_MAHONY_KP_ACC * ex_acc) + (AHRS_MAHONY_KP_MAG * ex_mag);
    corr_y = (AHRS_MAHONY_KP_ACC * ey_acc) + (AHRS_MAHONY_KP_MAG * ey_mag);
    corr_z = (AHRS_MAHONY_KP_ACC * ez_acc) + (AHRS_MAHONY_KP_MAG * ez_mag);

    if ((AHRS_MAHONY_KI > 0.0f) && (acc_valid || use_mag)) {
        ahrs_ctx.integral_x += AHRS_MAHONY_KI * corr_x * dt_s;
        ahrs_ctx.integral_y += AHRS_MAHONY_KI * corr_y * dt_s;
        ahrs_ctx.integral_z += AHRS_MAHONY_KI * corr_z * dt_s;
    }

    gx += corr_x + ahrs_ctx.integral_x;
    gy += corr_y + ahrs_ctx.integral_y;
    gz += corr_z + ahrs_ctx.integral_z;

    half_dt = 0.5f * dt_s;
    gx *= half_dt;
    gy *= half_dt;
    gz *= half_dt;

    qa = q0;
    qb = q1;
    qc = q2;

    ahrs_ctx.q0 += (-qb * gx) - (qc * gy) - (q3 * gz);
    ahrs_ctx.q1 += (qa * gx) + (qc * gz) - (q3 * gy);
    ahrs_ctx.q2 += (qa * gy) - (qb * gz) + (q3 * gx);
    ahrs_ctx.q3 += (qa * gz) + (qb * gy) - (qc * gx);

    AHRS_NormalizeQuaternion();
}

void AHRS_Reset(void)
{
    ahrs_ctx.q0 = 1.0f;
    ahrs_ctx.q1 = 0.0f;
    ahrs_ctx.q2 = 0.0f;
    ahrs_ctx.q3 = 0.0f;
    ahrs_ctx.integral_x = 0.0f;
    ahrs_ctx.integral_y = 0.0f;
    ahrs_ctx.integral_z = 0.0f;
    ahrs_ctx.mag_x = 0.0f;
    ahrs_ctx.mag_y = 0.0f;
    ahrs_ctx.mag_z = 0.0f;
    ahrs_ctx.yaw_gyro_deg100 = 0.0f;
    ahrs_ctx.yaw_mag_deg100 = 0;
    ahrs_ctx.acc_ref_sum = 0U;
    ahrs_ctx.acc_ref_count = 0U;
    ahrs_ctx.acc_1g_ref = 0U;
    ahrs_ctx.acc_ref_invalid_still_count = 0U;
    ahrs_ctx.gyro_bias_q8_x = 0;
    ahrs_ctx.gyro_bias_q8_y = 0;
    ahrs_ctx.gyro_bias_q8_z = 0;
    ahrs_ctx.gyro_bias_x = 0;
    ahrs_ctx.gyro_bias_y = 0;
    ahrs_ctx.gyro_bias_z = 0;
    ahrs_ctx.gyro_bias_ready = 0U;
    AHRS_ResetGyroBiasAccum();
    AHRS_Lpf3Reset(&ahrs_ctx.gyro_lpf);
    AHRS_Lpf3Reset(&ahrs_ctx.mag_lpf);
    ahrs_ctx.state.roll_deg100 = 0;
    ahrs_ctx.state.pitch_deg100 = 0;
    ahrs_ctx.state.yaw_deg100 = 0;
    ahrs_ctx.state.yaw_gyro_deg100 = 0;
    ahrs_ctx.state.yaw_mag_deg100 = 0;
    ahrs_ctx.state.gyro_x_dps100 = 0;
    ahrs_ctx.state.gyro_y_dps100 = 0;
    ahrs_ctx.state.gyro_z_dps100 = 0;
    ahrs_ctx.state.acc_norm = 0U;
    ahrs_ctx.state.dt_ms = 0U;
    ahrs_ctx.state.update_count = 0U;
    ahrs_ctx.state.flags = 0U;
    ahrs_ctx.mag_valid = 0U;
    ahrs_ctx.ready = 0U;
}

int8 AHRS_Update6Axis(int16 ax, int16 ay, int16 az,
                      int16 gx, int16 gy, int16 gz,
                      u16 dt_ms)
{
    u32 acc_norm;
    int16 fgx;
    int16 fgy;
    int16 fgz;
    u8 flags;
    u8 acc_valid;
    u8 gyro_still;
    float fax;
    float fay;
    float faz;
    float fgx_rad;
    float fgy_rad;
    float fgz_rad;
    float dt_s;

    if (dt_ms == 0U) {
        return -1;
    }

    flags = 0U;
    if (dt_ms > AHRS_DT_MAX_MS) {
        dt_ms = AHRS_DT_MAX_MS;
        flags |= AHRS_FLAG_DT_CLAMPED;
    }

    acc_norm = AHRS_Norm3Approx(ax, ay, az);
    gyro_still = AHRS_IsGyroStill(gx, gy, gz);
    AHRS_UpdateAccReference(acc_norm, gyro_still);
    acc_valid = AHRS_IsAccNormValid(acc_norm);
    if (acc_valid) {
        flags |= AHRS_FLAG_ACC_VALID;
    }
    if (ahrs_ctx.acc_1g_ref != 0U) {
        flags |= AHRS_FLAG_ACC_REF_READY;
    }

    AHRS_UpdateGyroBias(gx, gy, gz, gyro_still);
    if (ahrs_ctx.gyro_bias_ready) {
        flags |= AHRS_FLAG_GYRO_BIAS_READY;
    }

    gx = (int16)(gx - ahrs_ctx.gyro_bias_x);
    gy = (int16)(gy - ahrs_ctx.gyro_bias_y);
    gz = (int16)(gz - ahrs_ctx.gyro_bias_z);

    gx = AHRS_ApplyGyroDeadband(gx);
    gy = AHRS_ApplyGyroDeadband(gy);
    gz = AHRS_ApplyGyroDeadband(gz);

    AHRS_Lpf3Apply(&ahrs_ctx.gyro_lpf, AHRS_GYRO_LPF_SHIFT,
                   gx, gy, gz, &fgx, &fgy, &fgz);

    ahrs_ctx.state.gyro_x_dps100 = AHRS_GyroToDps100(fgx);
    ahrs_ctx.state.gyro_y_dps100 = AHRS_GyroToDps100(fgy);
    ahrs_ctx.state.gyro_z_dps100 = AHRS_GyroToDps100(fgz);

    fax = (float)ax;
    fay = (float)ay;
    faz = (float)az;
    if (acc_valid && !AHRS_NormalizeVector3(&fax, &fay, &faz)) {
        acc_valid = 0U;
        flags &= (u8)(~AHRS_FLAG_ACC_VALID);
    }

    if (!ahrs_ctx.ready) {
        if (!acc_valid) {
            AHRS_UpdateOutput(dt_ms, acc_norm, flags);
            return -1;
        }

        AHRS_InitQuaternionFromAccel(fax, fay, faz);
        ahrs_ctx.yaw_gyro_deg100 = 0.0f;
        ahrs_ctx.ready = 1U;
        flags |= AHRS_FLAG_READY;
        AHRS_UpdateOutput(dt_ms, acc_norm, flags);
        return 0;
    }

    dt_s = (float)dt_ms * 0.001f;
    fgx_rad = (float)fgx * AHRS_GYRO_RAW_TO_RAD;
    fgy_rad = (float)fgy * AHRS_GYRO_RAW_TO_RAD;
    fgz_rad = (float)fgz * AHRS_GYRO_RAW_TO_RAD;
    ahrs_ctx.yaw_gyro_deg100 += ((float)ahrs_ctx.state.gyro_z_dps100) * dt_s;
    while (ahrs_ctx.yaw_gyro_deg100 >= 18000.0f) {
        ahrs_ctx.yaw_gyro_deg100 -= 36000.0f;
    }
    while (ahrs_ctx.yaw_gyro_deg100 < -18000.0f) {
        ahrs_ctx.yaw_gyro_deg100 += 36000.0f;
    }

    AHRS_MahonyUpdate(fgx_rad, fgy_rad, fgz_rad,
                      fax, fay, faz, acc_valid, dt_s);

    flags |= AHRS_FLAG_READY;
    AHRS_UpdateOutput(dt_ms, acc_norm, flags);
    return 0;
}

int8 AHRS_UpdateRaw6Axis(int16 raw_ax, int16 raw_ay, int16 raw_az,
                         int16 raw_gx, int16 raw_gy, int16 raw_gz,
                         u16 dt_ms)
{
    int16 ax;
    int16 ay;
    int16 az;
    int16 gx;
    int16 gy;
    int16 gz;

    AHRS_MapRawToBody(raw_ax, raw_ay, raw_az, &ax, &ay, &az);
    AHRS_MapRawToBody(raw_gx, raw_gy, raw_gz, &gx, &gy, &gz);

    return AHRS_Update6Axis(ax, ay, az, gx, gy, gz, dt_ms);
}

int8 AHRS_UpdateMag(int16 mx, int16 my, int16 mz)
{
    u32 mag_norm;
    int16 fmx;
    int16 fmy;
    int16 fmz;
    float mxf;
    float myf;
    float mzf;

    mag_norm = AHRS_Norm3Approx(mx, my, mz);
    if (mag_norm < AHRS_MAG_MIN_NORM) {
        ahrs_ctx.mag_valid = 0U;
        ahrs_ctx.state.flags &= (u8)(~AHRS_FLAG_MAG_VALID);
        return -1;
    }

    AHRS_Lpf3Apply(&ahrs_ctx.mag_lpf, AHRS_MAG_LPF_SHIFT,
                   mx, my, mz, &fmx, &fmy, &fmz);

    mxf = (float)fmx;
    myf = (float)fmy;
    mzf = (float)fmz;
    if (!AHRS_NormalizeVector3(&mxf, &myf, &mzf)) {
        ahrs_ctx.mag_valid = 0U;
        ahrs_ctx.state.flags &= (u8)(~AHRS_FLAG_MAG_VALID);
        return -1;
    }

    ahrs_ctx.mag_x = mxf;
    ahrs_ctx.mag_y = myf;
    ahrs_ctx.mag_z = mzf;
    ahrs_ctx.mag_valid = 1U;
    ahrs_ctx.state.flags |= AHRS_FLAG_MAG_VALID;
    return 0;
}

int8 AHRS_UpdateRawMag(int16 raw_mx, int16 raw_my, int16 raw_mz)
{
    int16 mx;
    int16 my;
    int16 mz;

    AHRS_MapRawMagToBody(raw_mx, raw_my, raw_mz, &mx, &my, &mz);
    return AHRS_UpdateMag(mx, my, mz);
}

const AHRS_State_t *AHRS_GetState(void)
{
    return &ahrs_ctx.state;
}

u8 AHRS_IsReady(void)
{
    return ahrs_ctx.ready;
}

void AHRS_MapRawToBody(int16 raw_x, int16 raw_y, int16 raw_z,
                       int16 *body_x, int16 *body_y, int16 *body_z)
{
    if ((body_x == 0) || (body_y == 0) || (body_z == 0)) {
        return;
    }

    *body_x = AHRS_SelectMappedAxis(raw_x, raw_y, raw_z,
                                    AHRS_IMU_BODY_X_FROM,
                                    AHRS_IMU_BODY_X_SIGN);
    *body_y = AHRS_SelectMappedAxis(raw_x, raw_y, raw_z,
                                    AHRS_IMU_BODY_Y_FROM,
                                    AHRS_IMU_BODY_Y_SIGN);
    *body_z = AHRS_SelectMappedAxis(raw_x, raw_y, raw_z,
                                    AHRS_IMU_BODY_Z_FROM,
                                    AHRS_IMU_BODY_Z_SIGN);
}

void AHRS_MapRawMagToBody(int16 raw_x, int16 raw_y, int16 raw_z,
                          int16 *body_x, int16 *body_y, int16 *body_z)
{
    if ((body_x == 0) || (body_y == 0) || (body_z == 0)) {
        return;
    }

    *body_x = AHRS_SelectMappedAxis(raw_x, raw_y, raw_z,
                                    AHRS_MAG_BODY_X_FROM,
                                    AHRS_MAG_BODY_X_SIGN);
    *body_y = AHRS_SelectMappedAxis(raw_x, raw_y, raw_z,
                                    AHRS_MAG_BODY_Y_FROM,
                                    AHRS_MAG_BODY_Y_SIGN);
    *body_z = AHRS_SelectMappedAxis(raw_x, raw_y, raw_z,
                                    AHRS_MAG_BODY_Z_FROM,
                                    AHRS_MAG_BODY_Z_SIGN);
}
