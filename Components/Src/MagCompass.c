#include "MagCompass.h"
#include "AHRS.h"

#define MAG_ANGLE_90_CD     9000L
#define MAG_ANGLE_180_CD    18000L
#define MAG_ANGLE_360_CD    36000L

static mag_compass_state_t mag_compass_state;
static u8 mag_filter_started = 0U;
static u32 mag_norm_base = 0UL;
static int32 mag_heading_iir_cd = 0L;

static u32 MagCompass_Abs16ToU32(int16 value)
{
    int32 v;

    v = (int32)value;
    if (v < 0) {
        return (u32)(-v);
    }
    return (u32)v;
}

static u32 MagCompass_Abs32ToU32(int32 value)
{
    if (value < 0L) {
        return (u32)(-value);
    }
    return (u32)value;
}

static u16 MagCompass_WrapDeg100(int32 angle_cd)
{
    while (angle_cd >= MAG_ANGLE_360_CD) {
        angle_cd -= MAG_ANGLE_360_CD;
    }
    while (angle_cd < 0L) {
        angle_cd += MAG_ANGLE_360_CD;
    }
    return (u16)angle_cd;
}

static int32 MagCompass_WrapDiffDeg100(int32 diff_cd)
{
    while (diff_cd >= MAG_ANGLE_180_CD) {
        diff_cd -= MAG_ANGLE_360_CD;
    }
    while (diff_cd < -MAG_ANGLE_180_CD) {
        diff_cd += MAG_ANGLE_360_CD;
    }
    return diff_cd;
}

static u16 MagCompass_Atan01Deg100(u16 z_q10)
{
    int32 z;
    int32 curve;
    int32 angle;

    if (z_q10 > 1024U) {
        z_q10 = 1024U;
    }

    z = (int32)z_q10;
    curve = 4500L + ((1564L * (1024L - z)) / 1024L);
    angle = (z * curve) / 1024L;
    return (u16)angle;
}

static u16 MagCompass_Atan2Deg100(int32 y, int32 x)
{
    u32 ax;
    u32 ay;
    u16 z_q10;
    int32 base;
    int32 angle;

    ax = (x < 0L) ? (u32)(-x) : (u32)x;
    ay = (y < 0L) ? (u32)(-y) : (u32)y;

    if ((ax == 0UL) && (ay == 0UL)) {
        return 0U;
    }

    if (ax >= ay) {
        z_q10 = (u16)((ay * 1024UL) / ax);
        base = (int32)MagCompass_Atan01Deg100(z_q10);
        angle = (x >= 0L) ? base : (MAG_ANGLE_180_CD - base);
    } else {
        z_q10 = (u16)((ax * 1024UL) / ay);
        base = (int32)MagCompass_Atan01Deg100(z_q10);
        angle = (x >= 0L) ? (MAG_ANGLE_90_CD - base) : (MAG_ANGLE_90_CD + base);
    }

    if (y < 0L) {
        angle = -angle;
    }

    return MagCompass_WrapDeg100(angle);
}

void MagCompass_Reset(void)
{
    mag_compass_state.ready = 0U;
    mag_compass_state.heading_deg100 = 0U;
    mag_compass_state.norm1 = 0UL;
    mag_compass_state.horiz_sum = 0UL;
    mag_compass_state.stable_count = 0U;
    mag_filter_started = 0U;
    mag_norm_base = 0UL;
    mag_heading_iir_cd = 0L;
}

u8 MagCompass_ComputeRawHeading(int16 raw_x, int16 raw_y, int16 raw_z,
                                u16 *heading_out,
                                u32 *norm1_out,
                                u32 *horiz_sum_out)
{
    int16 body_x;
    int16 body_y;
    int16 body_z;
    int32 compass_cd;
    u32 norm1;
    u32 horiz_sum;

    if ((heading_out == 0) || (norm1_out == 0) || (horiz_sum_out == 0)) {
        return 0U;
    }

    body_x = 0;
    body_y = 0;
    body_z = 0;
    AHRS_MapRawMagToBody(raw_x, raw_y, raw_z, &body_x, &body_y, &body_z);
    norm1 = MagCompass_Abs16ToU32(body_x) +
            MagCompass_Abs16ToU32(body_y) +
            MagCompass_Abs16ToU32(body_z);
    horiz_sum = MagCompass_Abs16ToU32(body_x) + MagCompass_Abs16ToU32(body_y);
    *norm1_out = norm1;
    *horiz_sum_out = horiz_sum;

    if ((norm1 == 0UL) || (horiz_sum < (u32)MAG_COMPASS_HORIZ_MIN_SUM)) {
        return 0U;
    }

    compass_cd = (int32)MagCompass_Atan2Deg100((int32)(-body_y), (int32)body_x);
    compass_cd += (int32)MAG_COMPASS_RAW_OFFSET_CD;
    compass_cd *= (int32)MAG_COMPASS_DIRECTION_SIGN;
    compass_cd += (int32)MAG_COMPASS_INSTALL_OFFSET_CD;
    compass_cd += (int32)MAG_COMPASS_DECLINATION_CD;
    *heading_out = MagCompass_WrapDeg100(compass_cd);
    return 1U;
}

u8 MagCompass_Update(int16 raw_x, int16 raw_y, int16 raw_z, u16 *heading_out)
{
    u16 raw_heading_cd;
    u32 norm1;
    u32 horiz_sum;
    u32 norm_diff;
    int32 heading_diff_cd;

    if (MagCompass_ComputeRawHeading(raw_x, raw_y, raw_z,
                                     &raw_heading_cd,
                                     &norm1,
                                     &horiz_sum) == 0U) {
        return 0U;
    }

    mag_compass_state.norm1 = norm1;
    mag_compass_state.horiz_sum = horiz_sum;

    if (mag_norm_base == 0UL) {
        mag_norm_base = norm1;
    }
    if (norm1 > mag_norm_base) {
        norm_diff = norm1 - mag_norm_base;
    } else {
        norm_diff = mag_norm_base - norm1;
    }

    if ((mag_norm_base > 0UL) &&
        ((norm_diff * 100UL) > (mag_norm_base * (u32)MAG_COMPASS_NORM_REJECT_PCT))) {
        if (mag_compass_state.stable_count > 0U) {
            mag_compass_state.stable_count--;
        }
        return 0U;
    }

    if ((mag_norm_base > 0UL) &&
        ((norm_diff * 100UL) <= (mag_norm_base * (u32)MAG_COMPASS_NORM_TRACK_PCT))) {
        mag_norm_base += ((int32)norm1 - (int32)mag_norm_base) / 8L;
    }

    if (mag_filter_started == 0U) {
        mag_heading_iir_cd = (int32)raw_heading_cd;
        mag_compass_state.heading_deg100 = raw_heading_cd;
        mag_filter_started = 1U;
        mag_compass_state.stable_count = 1U;
        return 0U;
    }

    heading_diff_cd = MagCompass_WrapDiffDeg100((int32)raw_heading_cd - mag_heading_iir_cd);
    if (MagCompass_Abs32ToU32(heading_diff_cd) > (u32)MAG_COMPASS_JUMP_GATE_CD) {
        mag_compass_state.stable_count = 0U;
        return 0U;
    }

    if (MAG_COMPASS_IIR_DIV > 1L) {
        mag_heading_iir_cd += heading_diff_cd / (int32)MAG_COMPASS_IIR_DIV;
    } else {
        mag_heading_iir_cd = (int32)raw_heading_cd;
    }
    mag_heading_iir_cd = (int32)MagCompass_WrapDeg100(mag_heading_iir_cd);

    if (mag_compass_state.stable_count < 255U) {
        mag_compass_state.stable_count++;
    }
    if (mag_compass_state.stable_count >= (u8)MAG_COMPASS_READY_COUNT) {
        mag_compass_state.ready = 1U;
        mag_compass_state.heading_deg100 = (u16)mag_heading_iir_cd;
        if (heading_out != 0) {
            *heading_out = mag_compass_state.heading_deg100;
        }
        return 1U;
    }

    return 0U;
}

const mag_compass_state_t *MagCompass_GetState(void)
{
    return &mag_compass_state;
}
