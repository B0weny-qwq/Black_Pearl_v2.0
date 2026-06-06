/**
 * @file ship_protocol_internal.h
 * @brief 船端旧无线协议 App 内部拆分接口。
 *
 * 本头文件只给 `ship_protocol*.c` 使用，集中保存旧协议状态、配对参数、
 * 解析状态和内部函数声明。外部对接仍只使用 `ship_protocol.h`。
 */

#ifndef __SHIP_PROTOCOL_INTERNAL_H__
#define __SHIP_PROTOCOL_INTERNAL_H__

#include "ship_protocol.h"
#include "app_config.h"
#include "autodrive.h"
#include "board_power.h"
#include "board_wireless.h"

#define SHIP_TAG                         "SHIP"
#define SHIP_KEY_NULL                    SHIP_PROTOCOL_KEY_NONE
#define SHIP_GPS_REPORT_PAYLOAD_LEN      15U
#define SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN  36U
#define SHIP_POWER_LEVEL_0               0U
#define SHIP_POWER_LEVEL_1               1U
#define SHIP_POWER_LEVEL_2               2U
#define SHIP_POWER_LEVEL_3               3U
#define SHIP_POWER_LEVEL_4               4U
#define SHIP_LOWPOWER_CHECK_TICKS        600U
#define SHIP_LOWPOWER_ACCEL_MAX          10U
#define SHIP_CRUISE_KEY_START_INPUT      60
#define SHIP_CRUISE_KEY_STOP_INPUT       (-40)
#define SHIP_CRUISE_KEY_SPEED            760
#define SHIP_CRUISE_STEER_START_MAX      8
#define SHIP_CRUISE_GYRO_START_MAX_DPS   8
#define SHIP_AUTODRIVE_DIAG_PERIOD_MS    1000UL
#define SHIP_AUTODRIVE_DIAG_MIN_GAP_MS   200UL
#define SHIP_NORTH_CALIB_DOUBLE_CLICK_MS 1000UL

/** @brief 旧电量采样缓存，兼容 0x12 payload[13] 的 0..4 等级。 */
typedef struct
{
    u16 raw;
    u16 adc_mv;
    u32 bat_mv;
    u8 report;
    u8 valid;
    u8 sampled;
    int8 status;
} ship_protocol_power_sample_t;

/** @brief 旧遥控配对 seed 派生后的工作信道和同步字参数。 */
typedef struct
{
    u8 seed[4];
    u8 work_rx_channel;
    u8 work_tx_channel;
    u8 key0;
    u8 key1;
    u16 reg36;
    u16 reg39;
} ship_protocol_pair_params_t;

/** @brief AA len cmd payload xor BB 字节流解析状态。 */
typedef enum
{
    SHIP_PARSE_WAIT_HEAD = 0,
    SHIP_PARSE_READ_LEN,
    SHIP_PARSE_READ_BODY,
    SHIP_PARSE_DISPATCH
} ship_protocol_parse_state_t;

/** @brief 船端协议运行态，保持旧工程单状态机语义。 */
typedef struct
{
    u8 lr;
    u8 ud;
    u8 key;
    u8 last_key;
    u8 valid;
    u8 paired;
    u8 work_rx_configured;
    u8 work_state_logged;
    ship_protocol_state_t state;
    u8 rf_channel[3];
    u8 rf_send_key[2];
    u16 pair_wait_rsp_ticks;
    u16 wait_ticks;
    u16 pair_left;
    u16 pair_retry_count;
    u16 work_rx_reopen_ticks;
    u16 work_rx_reopen_total;
    u32 tick_ms;
    u32 pair_start_ms;
    u32 pair_wait_start_ms;
    u32 last_proto_rx_ms;
    u32 last_throttle_rx_ms;
    u8 pair_rsp_timeout_logged;
    u8 rx_idle_warned;
    u8 remote_online;
    u8 throttle_online;
    u8 throttle_recover_done;
    u8 manual_boot_block_logged;
    u8 manual_boot_ready_logged;
    u8 power_level;
    u8 power_adc_ready;
    u8 power_first_valid_logged;
    u8 lowpower_return_latched;
    u8 d_key_pressed;
    u8 d_key_click_waiting;
    u32 d_key_first_click_ms;
    u16 power_sample_divider_count;
    u16 lowpower_check_ticks;
    u32 power_sample_period_ms;
    ship_protocol_power_sample_t power_sample;
    ship_protocol_event_snapshot_t event;
    ship_protocol_event_snapshot_t event_queue[SHIP_PROTOCOL_EVENT_QUEUE_DEPTH];
    u8 event_queue_head;
    u8 event_queue_tail;
    u8 event_queue_count;
    u16 event_queue_dropped;
} ship_protocol_runtime_t;

extern ship_protocol_runtime_t ship_protocol_rt;
extern u8 ship_protocol_initialized;
extern u32 ship_protocol_last_tick_ms;
extern u8 ship_protocol_parse_buffer[SHIP_PROTO_MAX_FRAME_LEN];
extern u8 ship_protocol_tx_frame[SHIP_PROTO_MAX_FRAME_LEN];
extern u8 ship_protocol_rx_payload[BOARD_WIRELESS_MAX_PAYLOAD_LEN];
extern u8 ship_protocol_gps_payload[SHIP_GPS_REPORT_PAYLOAD_LEN];
extern u8 ship_protocol_diag_payload[SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN];
extern u8 ship_protocol_parse_index;
extern u8 ship_protocol_parse_expected_len;
extern ship_protocol_parse_state_t ship_protocol_parse_state;
extern ship_protocol_pair_params_t ship_protocol_pair_params;

u8 ship_protocol_xor(const u8 *buf, u8 len);
void ship_protocol_get_pair_seed(u8 *seed);
void ship_protocol_calc_pair_params(ship_protocol_pair_params_t *params);
void ship_protocol_apply_default_rf(void);
void ship_protocol_put_u16_be(u8 *buf, u16 value);
u16 ship_protocol_read_u16_be(const u8 *buf);
u32 ship_protocol_abs_int32(int32 value);
void ship_protocol_to_legacy_nmea_coord(u32 abs_deg1e7, u16 *coord1, u16 *coord2);

void ship_protocol_clear_spi_ps_event(void);
void ship_protocol_clear_event_payload(void);
void ship_protocol_fill_power_event(const ship_protocol_power_sample_t *sample);
void ship_protocol_event_queue_reset(void);
void ship_protocol_publish_event(ship_protocol_event_type_t type,
                                 ship_protocol_event_state_t state,
                                 u8 cmd,
                                 u8 payload_len);
void ship_protocol_publish_error_event(u8 cmd, u8 payload_len);
void ship_protocol_parse_point_payload(const u8 *payload, ship_protocol_point_t *point);
void ship_protocol_clear_point_event(void);
void ship_protocol_log_point(const char *label, const ship_protocol_point_t *point);
int16 ship_protocol_raw_ud_to_input(u8 front_back);
int16 ship_protocol_raw_lr_to_input(u8 left_right);
ship_protocol_key_action_t ship_protocol_key_to_action(u8 key);

void ship_protocol_power_init(void);
void ship_protocol_read_power_sample(ship_protocol_power_sample_t *sample);
void ship_protocol_low_power_check(void);
void ship_protocol_log_power_sample(const ship_protocol_power_sample_t *sample, u8 force_log);

u8 ship_protocol_get_autodrive_status(void);
int8 ship_protocol_send_frame(u8 channel, u8 cmd, const u8 *payload, u8 payload_len);
void ship_protocol_send_gps_once(void);
void ship_protocol_service_autodrive_diag(u32 now_ms);

int8 ship_protocol_apply_work_sync_idle(void);
int8 ship_protocol_apply_work_rx(void);
int8 ship_protocol_try_pair_send(u16 left_after_send);
int8 ship_protocol_arm_pair_rsp_window(void);
void ship_protocol_mark_proto_activity(u8 cmd);
void ship_protocol_step_pair_send(void);
void ship_protocol_step_work_rx(void);
void ship_protocol_check_timeouts(void);

void ship_protocol_handle_pair_rsp(const u8 *payload, u8 payload_len);
void ship_protocol_handle_throttle(const u8 *payload, u8 payload_len);
void ship_protocol_handle_return_home(const u8 *payload, u8 payload_len);
u8 ship_protocol_handle_fish_point(const u8 *payload,
                                   u8 payload_len,
                                   const u8 *frame,
                                   u8 frame_len,
                                   u8 xor_calc,
                                   u8 xor_recv);
void ship_protocol_handle_return_switch(const u8 *payload, u8 payload_len);

int8 ship_protocol_parse_frame(const u8 *frame, u8 frame_len);
void ship_protocol_poll_rx_frames(void);

#endif
