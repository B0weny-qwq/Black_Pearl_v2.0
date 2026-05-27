#include "app_internal.h"
#include "board_imu.h"
#include "board_motor.h"
#include "Filter.h"
#include "logger.h"
#include "MagCompass.h"
#include "ship_control.h"

/* AHRS/MAG 公共日志和静态判定，供轮询文件复用。 */
void app_log_mag_read_fail(int8 ret)
{
#if (SHIP_MAG_STANDALONE_LOG_ENABLE != 0U)
    board_mag_diag_t diag;

    if (board_mag_get_diag(&diag) == BOARD_MAG_OK) {
        LOGW("MAG", "read fail rc=%d addr=0x%02X id=0x%02X c1=0x%02X c2=0x%02X",
             ret,
             (u16)diag.addr,
             (u16)diag.chip_id,
             (u16)diag.control_1,
             (u16)diag.control_2);
    } else {
        LOGW("MAG", "read fail rc=%d", ret);
    }
#else
    (void)ret;
#endif
}

static void app_log_mag_sample(const board_mag_sample_t *sample, u32 now_ms)
{
#if (SHIP_MAG_STANDALONE_LOG_ENABLE != 0U)
    if (sample == 0) {
        return;
    }
    if ((now_ms - app_last_mag_log_ms) < SHIP_MAG_STANDALONE_LOG_PERIOD_MS) {
        return;
    }
    app_last_mag_log_ms = now_ms;
    LOGI("MAG", "test raw=%d %d %d norm1=%lu",
         sample->mag_x_raw,
         sample->mag_y_raw,
         sample->mag_z_raw,
         app_abs32((int32)sample->mag_x_raw) +
         app_abs32((int32)sample->mag_y_raw) +
         app_abs32((int32)sample->mag_z_raw));
#else
    (void)sample;
    (void)now_ms;
#endif
}

void app_record_mag_sample(const board_mag_sample_t *sample, u32 now_ms)
{
    if (sample == 0) {
        return;
    }
    app_last_mag_x_raw = sample->mag_x_raw;
    app_last_mag_y_raw = sample->mag_y_raw;
    app_last_mag_z_raw = sample->mag_z_raw;
    app_last_mag_valid = 1U;
    app_mag_error_latched = 0U;
    app_log_mag_sample(sample, now_ms);
}

u8 app_ahrs_is_static(const AHRS_State_t *att)
{
    int16 left_speed;
    int16 right_speed;

    if (att == 0) {
        return 0U;
    }
    if (ShipControl_GetMode() != SHIP_CONTROL_MODE_STOP) {
        return 0U;
    }
    if (board_motor_get_speed(BOARD_MOTOR_LEFT, &left_speed) != BOARD_MOTOR_OK) {
        return 0U;
    }
    if (board_motor_get_speed(BOARD_MOTOR_RIGHT, &right_speed) != BOARD_MOTOR_OK) {
        return 0U;
    }
    if ((left_speed != 0) || (right_speed != 0)) {
        return 0U;
    }
    if ((att->flags & AHRS_FLAG_GYRO_BIAS_READY) == 0U) {
        return 0U;
    }
    if ((att->flags & AHRS_FLAG_ACC_VALID) == 0U) {
        return 0U;
    }
    if (app_abs32((int32)att->gyro_x_dps100) > AHRS_GYRO_STILL_DPS100) {
        return 0U;
    }
    if (app_abs32((int32)att->gyro_y_dps100) > AHRS_GYRO_STILL_DPS100) {
        return 0U;
    }
    if (app_abs32((int32)att->gyro_z_dps100) > AHRS_GYRO_STILL_DPS100) {
        return 0U;
    }
    return 1U;
}

void app_ahrs_reset(void)
{
    AHRS_Reset();
    Heading_Init(&app_heading);
    MagCompass_Reset();
    Filter_ResetMagLowPass();
    Filter_ResetGyroLowPass();
    app_heading_ready = 0U;
    app_heading_deg100 = 0U;
    app_heading_rel_deg100 = 0;
    app_ahrs_started = 1U;
    app_last_mag_valid = 0U;
}
