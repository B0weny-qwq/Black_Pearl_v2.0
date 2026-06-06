#include "app_internal.h"

HeadingEstimator_t app_heading;
u8 app_heading_ready = 0U;
u16 app_raw_heading_deg100 = 0U;
u16 app_heading_deg100 = 0U;
int16 app_heading_rel_deg100 = 0;
u8 app_ahrs_started = 0U;
u8 app_spi_ps_ready = 0U;
int16 app_last_mag_x_raw = 0;
int16 app_last_mag_y_raw = 0;
int16 app_last_mag_z_raw = 0;
u8 app_last_mag_valid = 0U;
u32 app_last_mag_log_ms = 0UL;
u8 app_mag_error_latched = 0U;

/* App 内部数值工具：日志格式和航向规整统一走这里。 */
u32 app_abs32(int32 value)
{
    if (value < 0L) {
        return (u32)(-value);
    }
    return (u32)value;
}

char app_cd_sign(int32 value)
{
    return (value < 0L) ? '-' : '+';
}

u16 app_cd_abs_whole(int32 value)
{
    return (u16)(app_abs32(value) / 100UL);
}

u16 app_cd_abs_frac(int32 value)
{
    return (u16)(app_abs32(value) % 100UL);
}

int16 app_wrap_signed_deg100(int32 angle)
{
    while (angle >= 18000L) {
        angle -= 36000L;
    }
    while (angle < -18000L) {
        angle += 36000L;
    }
    return (int16)angle;
}

u16 app_wrap_heading_deg100(int32 angle)
{
    while (angle >= 36000L) {
        angle -= 36000L;
    }
    while (angle < 0L) {
        angle += 36000L;
    }
    return (u16)angle;
}

int32 app_float_to_deg100(float value)
{
    float scaled;

    scaled = value * 100.0f;
    if (scaled >= 0.0f) {
        return (int32)(scaled + 0.5f);
    }
    return (int32)(scaled - 0.5f);
}
