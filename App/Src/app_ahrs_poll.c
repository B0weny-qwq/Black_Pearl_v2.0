#include "app_internal.h"
#include "board_imu.h"
#include "Filter.h"
#include "logger.h"
#include "MagCompass.h"
#include "north_calib.h"
#include "platform_scheduler.h"

static void app_ahrs_log(const AHRS_State_t *att,
                         const mag_compass_state_t *mag_state,
                         u8 heading_static,
                         u8 heading_mag_settled)
{
    int32 heading_err_cd;
    int32 heading_pred_cd;
    int32 mag_yaw_cd;
    u32 mag_norm;

    if (att == 0) {
        return;
    }

    heading_err_cd = app_float_to_deg100(app_heading.heading_err_deg);
    heading_pred_cd = app_float_to_deg100(app_heading.heading_pred_deg);
    LOGI("AHRS", "rpy=%d,%d,%d flg=0x%02X",
         att->roll_deg100, att->pitch_deg100, att->yaw_deg100, (u16)att->flags);
    if (app_last_mag_valid != 0U) {
        mag_yaw_cd = (mag_state != 0) ? (int32)mag_state->heading_deg100 :
                     (int32)app_heading_deg100;
        mag_norm = app_abs32((int32)app_last_mag_x_raw) +
                   app_abs32((int32)app_last_mag_y_raw) +
                   app_abs32((int32)app_last_mag_z_raw);
        LOGI("MAG", "raw=%d %d %d norm=%lu yaw=%c%u.%02u self=%u",
             app_last_mag_x_raw,
             app_last_mag_y_raw,
             app_last_mag_z_raw,
             mag_norm,
             app_cd_sign(mag_yaw_cd),
             app_cd_abs_whole(mag_yaw_cd),
             app_cd_abs_frac(mag_yaw_cd),
             (u16)(((heading_static != 0U) && (heading_mag_settled != 0U)) ? 1U : 0U));
    }
    LOGI("HDG", "abs=%u rel=%d mag=%u rdy=%u st=%u set=%u err=%ld pred=%ld",
         app_heading_deg100,
         app_heading_rel_deg100,
         (mag_state != 0) ? mag_state->heading_deg100 : 0U,
         (mag_state != 0) ? (u16)mag_state->ready : 0U,
         (u16)heading_static,
         (u16)heading_mag_settled,
         heading_err_cd,
         heading_pred_cd);
}

/* AHRS 主链路：IMU 原始值和 MAG 原始值最终更新给 ShipControl 使用的航向快照。 */
void app_ahrs_poll(void)
{
    static u8 timing_started = 0U;
    static u32 last_imu_ms = 0UL;
    static u32 last_mag_ms = 0UL;
    static u32 heading_static_start_ms = 0UL;
    static u16 sample_div = 0U;
    static u8 imu_raw_log_div = 0U;
    static u8 read_error_latched = 0U;
    static u8 heading_seeded = 0U;
    static u8 last_heading_static = 0U;
    u32 now_ms;
    u32 elapsed_ms;
    u16 dt_ms;
    u16 stable_mag_heading_cd;
    u8 stable_mag_valid;
    u8 heading_static;
    u8 heading_mag_settled;
    u8 log_due;
    board_imu_sample_t imu_sample;
    board_mag_sample_t mag_sample;
    const AHRS_State_t *att;
    const mag_compass_state_t *mag_state;
    int16 mag_x;
    int16 mag_y;
    int16 mag_z;
    int8 ret;
    float heading_seed_deg;

    if ((board_imu_is_ready() == 0U) || (app_ahrs_started == 0U)) {
        return;
    }

    now_ms = platform_scheduler_get_tick_ms();
    if (timing_started == 0U) {
        last_imu_ms = now_ms;
        last_mag_ms = now_ms;
        timing_started = 1U;
        return;
    }

    elapsed_ms = now_ms - last_imu_ms;
    if (elapsed_ms < AHRS_IMU_PERIOD_MS) {
        return;
    }
    last_imu_ms = now_ms;
    dt_ms = (elapsed_ms > AHRS_DT_MAX_MS) ? AHRS_DT_MAX_MS : (u16)elapsed_ms;

    (void)board_imu_service();
    ret = board_imu_read(&imu_sample);
    if (ret != BOARD_IMU_OK) {
        if (read_error_latched == 0U) {
            LOGW("AHRS", "imu read fail rc=%d state=%u", ret, (u16)board_imu_get_state());
            read_error_latched = 1U;
        }
        return;
    }
    read_error_latched = 0U;

#if (SHIP_IMU_RAW_LOG_ENABLE != 0U)
    if (imu_raw_log_div < APP_IMU_RAW_LOG_DECIMATION) {
        imu_raw_log_div++;
    } else {
        imu_raw_log_div = 0U;
        LOGI("IMU", "raw a=%d,%d,%d",
             imu_sample.acc_x_raw, imu_sample.acc_y_raw, imu_sample.acc_z_raw);
    }
#endif

    if (AHRS_UpdateRaw6Axis(imu_sample.acc_x_raw,
                            imu_sample.acc_y_raw,
                            imu_sample.acc_z_raw,
                            imu_sample.gyro_x_raw,
                            imu_sample.gyro_y_raw,
                            imu_sample.gyro_z_raw,
                            dt_ms) != 0) {
        return;
    }

    att = AHRS_GetState();
    heading_static = ((att->flags & AHRS_FLAG_READY) != 0U) ? app_ahrs_is_static(att) : 0U;
    heading_mag_settled = 0U;
    if (heading_static != 0U) {
        if (last_heading_static == 0U) {
            heading_static_start_ms = now_ms;
            MagCompass_Reset();
        } else if ((now_ms - heading_static_start_ms) >= APP_MAG_COMPASS_STATIC_SETTLE_MS) {
            heading_mag_settled = 1U;
        }
    } else {
        heading_seeded = ((att->flags & AHRS_FLAG_READY) != 0U) ? heading_seeded : 0U;
        heading_static_start_ms = 0UL;
    }
    last_heading_static = heading_static;
    if ((att->flags & AHRS_FLAG_READY) == 0U) {
        app_heading_ready = 0U;
        app_raw_heading_deg100 = 0U;
        app_heading_deg100 = 0U;
        app_heading_rel_deg100 = 0;
        Heading_Init(&app_heading);
        return;
    }

    stable_mag_valid = 0U;
    log_due = 0U;
    mag_state = MagCompass_GetState();
    stable_mag_heading_cd = (mag_state != 0) ? mag_state->heading_deg100 : 0U;
    if ((board_mag_is_ready() != 0U) &&
        ((now_ms - last_mag_ms) >= AHRS_MAG_PERIOD_MS)) {
        last_mag_ms = now_ms;
        ret = board_mag_read(&mag_sample);
        if (ret == BOARD_MAG_OK) {
            app_record_mag_sample(&mag_sample, now_ms);
            if (Filter_MagLowPass(mag_sample.mag_x_raw,
                                  mag_sample.mag_y_raw,
                                  mag_sample.mag_z_raw,
                                  &mag_x,
                                  &mag_y,
                                  &mag_z) == 0) {
                (void)AHRS_UpdateRawMag(mag_x, mag_y, mag_z);
                if ((heading_mag_settled != 0U) &&
                    (MagCompass_Update(mag_x, mag_y, mag_z, &stable_mag_heading_cd) != 0U)) {
                    stable_mag_valid = 1U;
                }
            }
        } else if (app_mag_error_latched == 0U) {
            app_log_mag_read_fail(ret);
            app_mag_error_latched = 1U;
        }
    }

    if (sample_div < APP_AHRS_LOG_DECIMATION) {
        sample_div++;
    } else {
        sample_div = 0U;
        log_due = 1U;
    }

    if (heading_seeded == 0U) {
        mag_state = MagCompass_GetState();
        if ((mag_state == 0) || (mag_state->ready == 0U) || (heading_mag_settled == 0U)) {
            app_heading_ready = 0U;
            app_raw_heading_deg100 = 0U;
            app_heading_deg100 = 0U;
            app_heading_rel_deg100 = 0;
            if (log_due != 0U) {
                app_ahrs_log(att, mag_state, heading_static, heading_mag_settled);
            }
            return;
        }
        heading_seed_deg = (float)mag_state->heading_deg100 * 0.01f;
        Heading_SetHeadingDeg(&app_heading, heading_seed_deg);
        Heading_ResetZero(&app_heading);
        heading_seeded = 1U;
        app_heading_ready = 1U;
    }

    Heading_Update(&app_heading,
                   (float)att->gyro_z_dps100 * 0.01f,
                   (float)stable_mag_heading_cd * 0.01f,
                   stable_mag_valid,
                   heading_static,
                   (float)dt_ms * 0.001f);
    app_raw_heading_deg100 = app_wrap_heading_deg100(Heading_GetDeg100(&app_heading));
    app_heading_deg100 = app_wrap_heading_deg100((int32)app_raw_heading_deg100 +
                                                 (int32)NorthCalib_GetHeadingOffsetCd());
    app_heading_rel_deg100 = app_wrap_signed_deg100(Heading_GetRelativeDeg100(&app_heading));
    app_heading_ready = 1U;

    if (log_due != 0U) {
        app_ahrs_log(att, MagCompass_GetState(), heading_static, heading_mag_settled);
    }
}
