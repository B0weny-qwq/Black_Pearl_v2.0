/**
 * @file app_config.h
 * @brief Black Pearl v2.0 应用运行档位配置。
 *
 * 本文件集中保存从 v1.1 FeatureSwitch.h 迁移来的业务调参值。App 状态机只引用
 * 这些配置宏，不在具体业务文件里散落重复常量，便于现场联调时确认当前固件烧录后的
 * 遥控映射、yaw 自稳、配对节拍和日志节流行为。
 */

#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

/**
 * @name 手动控制与 yaw 自稳
 * @{
 */
#define SHIP_AXIS_CENTER                  100U
#define SHIP_MANUAL_CONTROL_PERIOD_MS     10UL
#define SHIP_YAW_HOLD_PERIOD_MS           50UL
#define SHIP_MOT_LOG_PERIOD_MS            200UL
#define SHIP_AXIS_FILTER_SHIFT            1U
#define SHIP_RC_AXIS_MAX_DELTA            60
#define SHIP_THROTTLE_DEADBAND            4
#define SHIP_STEERING_DEADBAND            8
#define SHIP_THROTTLE_MIN_COMMAND         180
#define SHIP_THROTTLE_MAX_COMMAND         850
#define SHIP_MOTOR_OUTPUT_MAX_COMMAND     SHIP_THROTTLE_MAX_COMMAND
#define SHIP_STEERING_MAX_COMMAND         700
#define SHIP_CRUISE_BASE_SPEED            SHIP_THROTTLE_MAX_COMMAND
#define SHIP_CRUISE_RAMP_MS               1800UL
#define SHIP_CRUISE_RAMP_MIN_BASE         520
#define SHIP_YAW_HOLD_FULL_ERROR_CD       1000
#define SHIP_YAW_HOLD_DIFF_LIMIT_PERMILLE 320
#define SHIP_MANUAL_YAW_HOLD_DIFF_PERCENT 20U
#define SHIP_YAW_HOLD_OUTPUT_SIGN         1
#define SHIP_YAW_HOLD_DERATE_START_CD     1000
#define SHIP_YAW_HOLD_DERATE_FULL_CD      2000
#define SHIP_YAW_HOLD_DERATE_MIN_BASE     500
#define SHIP_YAW_HOLD_GYRO_DAMP_Q10       4096
#define SHIP_YAW_HOLD_DIFF_SLEW_PER_STEP  20
#define SHIP_YAW_HOLD_STEER_STABLE_FRAMES 2U
#define SHIP_YAW_HOLD_OUTPUT_LIMIT        1000
#define SHIP_YAW_HOLD_DEADBAND_CD         50
#define SHIP_YAW_HOLD_KP_Q10              384
#define SHIP_YAW_HOLD_KI_Q10              0
#define SHIP_YAW_HOLD_KD_Q10              96
#define SHIP_GPS_ALIGN_KP_Q10             384
#define SHIP_GPS_ALIGN_KI_Q10             0
#define SHIP_GPS_ALIGN_KD_Q10             0
#define SHIP_GPS_ALIGN_DIFF_PERCENT       18U
/** @} */

/**
 * @name 旧遥控/上位机协议档位
 * @{
 *
 * 配对信道、seed 和工作信道派生算法保持旧协议兼容。发送 burst、等待 tick 和
 * work-rx reopen 使用当前 v2 实测兼容档位，以保证上位机/遥控在 10 ms 调度下稳定收包。
 */
#define SHIP_PAIR_CHANNEL_DEFAULT        0x7FU
#define SHIP_PAIR_SEED0                  0x65U
#define SHIP_PAIR_SEED1                  0x65U
#define SHIP_PAIR_SEED2                  0xA0U
#define SHIP_PAIR_SEED3                  0x65U
#define SHIP_PAIR_SEND_TIMES             10U
#define SHIP_WAIT_TICKS_DEFAULT          30U
#define SHIP_PAIR_WAIT_RSP_TICKS         500U
#define SHIP_PAIR_RSP_EXPIRE_LOG_MS      5000UL
#define SHIP_PAIR_FORCE_WORK_MS          60000UL
#define SHIP_WORK_RX_REOPEN_TICKS        200U
#define SHIP_RX_IDLE_WARN_MS             3000UL
#define SHIP_THROTTLE_TIMEOUT_MS         1500UL
#define SHIP_THROTTLE_RECOVER_MS         3000UL
#define SHIP_MANUAL_BOOT_BLOCK_MS        3000UL
#define SHIP_MANUAL_BOOT_WAIT_HEADING    1U
/** @} */

/**
 * @name 电源采样与日志节流
 * @{
 */
#define SHIP_POWER_SAMPLE_DIVIDER        100U
#define SHIP_POWER_LOG_PERIOD_MS         10000UL
#define SHIP_ADC_LOG_ENABLE              1
#define SHIP_CONTROL_LOG_ENABLE          0U
/** @} */

#endif
