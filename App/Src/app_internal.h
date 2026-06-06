/**
 * @file app_internal.h
 * @brief App 主循环内部拆分接口。
 *
 * 该头文件只给 App/Src 下的应用编排文件使用，用于共享 AHRS 航向快照、
 * SPI-PS 资源状态和拆分后的轮询函数。外部模块仍只包含 `app.h`。
 */

#ifndef __APP_INTERNAL_H__
#define __APP_INTERNAL_H__

#include "app.h"
#include "app_config.h"
#include "board_mag.h"
#include "HeadingEstimator.h"

#define APP_AHRS_LOG_DECIMATION          32U
#define APP_MAG_COMPASS_STATIC_SETTLE_MS 3000UL
#define APP_SPI_PS_RX_EVENT_MAX          32U
#define APP_IMU_RAW_LOG_DECIMATION       64U

extern HeadingEstimator_t app_heading;
extern u8 app_heading_ready;
extern u16 app_raw_heading_deg100;
extern u16 app_heading_deg100;
extern int16 app_heading_rel_deg100;
extern u8 app_ahrs_started;
extern u8 app_spi_ps_ready;
extern int16 app_last_mag_x_raw;
extern int16 app_last_mag_y_raw;
extern int16 app_last_mag_z_raw;
extern u8 app_last_mag_valid;
extern u32 app_last_mag_log_ms;
extern u8 app_mag_error_latched;

/** @brief 计算 32 位有符号整数绝对值。 */
u32 app_abs32(int32 value);

/** @brief 返回角度日志的正负号字符。 */
char app_cd_sign(int32 value);

/** @brief 返回 deg*100 角度的整数度部分。 */
u16 app_cd_abs_whole(int32 value);

/** @brief 返回 deg*100 角度的小数部分。 */
u16 app_cd_abs_frac(int32 value);

/** @brief 规整有符号角度到 -18000..17999。 */
int16 app_wrap_signed_deg100(int32 angle);

/** @brief 规整航向角到 0..35999。 */
u16 app_wrap_heading_deg100(int32 angle);

/** @brief 将 float 度数转换成 deg*100 整数。 */
int32 app_float_to_deg100(float value);

/** @brief 判定当前船体是否满足静态磁航向融合条件。 */
u8 app_ahrs_is_static(const AHRS_State_t *att);

/** @brief 重置 AHRS、航向估计和磁力计滤波状态。 */
void app_ahrs_reset(void);

/** @brief 记录并输出磁力计读取失败诊断。 */
void app_log_mag_read_fail(int8 ret);

/** @brief 缓存最近一次磁力计样本并按节流输出观测日志。 */
void app_record_mag_sample(const board_mag_sample_t *sample, u32 now_ms);

/** @brief 按旧工程迁移顺序初始化板级设备。 */
void app_bring_up_devices(void);

/** @brief 轮询 IMU/MAG 并更新 AHRS/Heading 快照。 */
void app_ahrs_poll(void);

/** @brief 独立观测磁力计，便于 AHRS 未 ready 时排障。 */
void app_mag_observe_poll(void);

/** @brief 轮询 SPI-PS 并发布协议观察事件。 */
void app_spi_ps_poll(void);

/** @brief 分发协议事件到日志和外包扩展回调。 */
void app_ship_event_poll(void);

#endif
