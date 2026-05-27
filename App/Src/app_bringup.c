#include "app_internal.h"
#include "board_gps.h"
#include "board_imu.h"
#include "board_motor.h"
#include "board_power.h"
#include "board_spi_ps.h"
#include "board_wireless.h"
#include "logger.h"
#include "ship_protocol.h"

/* 按 v1.1 迁移后的启动顺序拉起板级设备和 App 状态机。 */
static void app_log_imu_diag(void)
{
#if (SHIP_APP_BRINGUP_VERBOSE_LOG_ENABLE != 0U)
    board_imu_diag_t imu_diag;

    if (board_imu_get_diag(&imu_diag) != BOARD_IMU_OK) {
        return;
    }

    LOGE("IMU", "diag chip_rc=%d addr=0x%02X who=0x%02X s0=0x%02X",
         imu_diag.chip_error,
         (u16)imu_diag.i2c_addr,
         (u16)imu_diag.who_am_i,
         (u16)imu_diag.status0);
    LOGE("IMU", "cfg retry=%u reg=0x%02X wr=0x%02X rb=0x%02X ret=%d",
         (u16)imu_diag.cfg_retry,
         (u16)imu_diag.cfg_reg,
         (u16)imu_diag.cfg_write,
         (u16)imu_diag.cfg_read,
         imu_diag.cfg_ret);
    LOGE("IMU", "i2c op=%u st=%u rc=%d b=%02X/%02X rec=%d ms=%02X/%02X",
         (u16)imu_diag.i2c_op,
         (u16)imu_diag.i2c_stage,
         imu_diag.i2c_ret,
         (u16)imu_diag.i2c_state_before,
         (u16)imu_diag.i2c_state_after,
         imu_diag.i2c_recover_ret,
         (u16)imu_diag.i2c_msst,
         (u16)imu_diag.i2c_mscr);
#endif
}

static void app_try_imu_after_bringup(void)
{
    int8 ret;

    ret = board_imu_init();
    if (ret == BOARD_IMU_OK) {
#if (SHIP_APP_BRINGUP_VERBOSE_LOG_ENABLE != 0U)
        LOGI("IMU", "QMI8658 init ok");
#endif
    } else {
        LOGE("IMU", "QMI8658 init fail rc=%d", ret);
        app_log_imu_diag();
    }
}

static void app_read_imu_once(void)
{
#if (SHIP_APP_BRINGUP_VERBOSE_LOG_ENABLE != 0U)
    int8 ret;
    board_imu_sample_t imu_sample;

    if ((board_imu_is_ready() == 0U) ||
        (board_imu_service() != BOARD_IMU_OK) ||
        (board_imu_has_data_ready() == 0U)) {
        return;
    }

    ret = board_imu_read(&imu_sample);
    if (ret == BOARD_IMU_OK) {
        LOGI("IMU", "sample a=%d,%d,%d t=%d",
             imu_sample.acc_x_raw,
             imu_sample.acc_y_raw,
             imu_sample.acc_z_raw,
             imu_sample.temp_raw);
    } else {
        LOGW("IMU", "sample not ready rc=%d", ret);
    }
#endif
}

void app_bring_up_devices(void)
{
    int8 ret;

    ret = board_gps_init();
    if (ret != BOARD_GPS_OK) {
        LOGE("GPS", "init fail rc=%d", ret);
    }

    app_try_imu_after_bringup();

    ret = board_mag_init();
    if (ret != BOARD_MAG_OK) {
        LOGE("MAG", "QMC6309 init fail rc=%d", ret);
    }

    ret = board_power_init();
    if (ret == BOARD_POWER_OK) {
#if BOARD_POWER_BAT_MV_UNCALIBRATED
        LOGW("POWER", "bat_mv uncal");
#endif
    } else {
        LOGE("POWER", "init fail rc=%d", ret);
    }

    ret = board_wireless_init();
    if (ret == BOARD_WIRELESS_OK) {
        ship_protocol_init();
    } else {
        LOGE("WL", "init fail rc=%d", ret);
    }

    ret = board_spi_ps_init();
    if (ret == BOARD_SPI_PS_OK) {
        app_spi_ps_ready = 1U;
    } else {
        app_spi_ps_ready = 0U;
        if (ret == BOARD_SPI_PS_ERR_RESOURCE) {
            LOGW("SPI-PS", "disabled shared SPI");
        }
    }

    ret = board_motor_init();
    if (ret != BOARD_MOTOR_OK) {
        LOGE("MOTOR", "init fail rc=%d", ret);
    }

    app_read_imu_once();
    app_ahrs_reset();
}
