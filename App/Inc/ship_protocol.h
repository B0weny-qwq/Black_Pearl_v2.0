/**
 * @file ship_protocol.h
 * @brief 船端旧无线业务协议状态机接口。
 *
 * 本模块属于 App 层，负责编排无线配对、协议截帧、旧命令分发、
 * 0x12 GPS 状态回包和 0x16 AutoDrive 诊断上报。硬件访问全部通过
 * BoardDevices API 完成。
 */

#ifndef __SHIP_PROTOCOL_H__
#define __SHIP_PROTOCOL_H__

#include "type_def.h"

#define SHIP_PROTO_HEAD          0xAAU
#define SHIP_PROTO_TAIL          0xBBU
#define SHIP_PROTO_MAX_FRAME_LEN 64U

#define SHIP_CMD_PAIR_RSP        0x0FU
#define SHIP_CMD_PAIR            0x10U
#define SHIP_CMD_THROTTLE        0x11U
#define SHIP_CMD_GPS_REPORT      0x12U
#define SHIP_CMD_RETURN_HOME     0x13U
#define SHIP_CMD_GOTO_POINT      0x14U
#define SHIP_CMD_RETURN_SWITCH   0x15U
#define SHIP_CMD_AUTODRIVE_DIAG  0x16U

#define SHIP_PROTOCOL_POINT_PAYLOAD_LEN 10U

#define SHIP_PROTOCOL_RETURN_SWITCH_DEFAULT 0x30U

#define SHIP_PROTOCOL_KEY_NONE          0xA0U
#define SHIP_PROTOCOL_KEY_E_RESERVED    0xA1U
#define SHIP_PROTOCOL_KEY_A_LIGHT       0xA3U
#define SHIP_PROTOCOL_KEY_B_UNUSED      0xA5U
#define SHIP_PROTOCOL_KEY_C_UNUSED      0xA7U
#define SHIP_PROTOCOL_KEY_D_UNUSED      0xA9U

#define SHIP_PROTOCOL_SPI_PS_EVENT_DATA_MAX 16U
#define SHIP_PROTOCOL_EVENT_QUEUE_DEPTH     8U

typedef enum
{
    SHIP_PROTOCOL_STATE_BOOT_WAIT = 0,
    SHIP_PROTOCOL_STATE_PAIR_SEND,
    SHIP_PROTOCOL_STATE_PAIR_WAIT_RSP,
    SHIP_PROTOCOL_STATE_WORK_RX
} ship_protocol_state_t;

typedef enum
{
    SHIP_PROTOCOL_EVENT_STATE_IDLE = 0,
    SHIP_PROTOCOL_EVENT_STATE_THROTTLE_ACTIVE,
    SHIP_PROTOCOL_EVENT_STATE_KEY_EDGE,
    SHIP_PROTOCOL_EVENT_STATE_KEY_ACTION,
    SHIP_PROTOCOL_EVENT_STATE_RETURN_HOME_PENDING,
    SHIP_PROTOCOL_EVENT_STATE_FISH_POINT_PENDING,
    SHIP_PROTOCOL_EVENT_STATE_RETURN_SWITCH_PENDING,
    SHIP_PROTOCOL_EVENT_STATE_POWER,
    SHIP_PROTOCOL_EVENT_STATE_SPI_PS,
    SHIP_PROTOCOL_EVENT_STATE_ERROR
} ship_protocol_event_state_t;

typedef enum
{
    SHIP_PROTOCOL_EVENT_NONE = 0,
    SHIP_PROTOCOL_EVENT_THROTTLE,
    SHIP_PROTOCOL_EVENT_KEY_EDGE,
    SHIP_PROTOCOL_EVENT_RETURN_HOME,
    SHIP_PROTOCOL_EVENT_FISH_POINT,
    SHIP_PROTOCOL_EVENT_RETURN_SWITCH,
    SHIP_PROTOCOL_EVENT_KEY_ACTION,
    SHIP_PROTOCOL_EVENT_POWER_SAMPLE,
    SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED,
    SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED,
    SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX,
    SHIP_PROTOCOL_EVENT_FRAME_ERROR
} ship_protocol_event_type_t;

typedef enum
{
    SHIP_PROTOCOL_KEY_ACTION_NONE = 0,
    SHIP_PROTOCOL_KEY_ACTION_B_NOOP,
    SHIP_PROTOCOL_KEY_ACTION_C_NOOP,
    SHIP_PROTOCOL_KEY_ACTION_D_NOOP
} ship_protocol_key_action_t;

typedef struct
{
    u8 lon_ew;
    u16 lon_whole;
    u16 lon_frac;
    u8 lat_ns;
    u16 lat_whole;
    u16 lat_frac;
} ship_protocol_point_t;

typedef struct
{
    u8 lr;
    u8 ud;
    u8 key;
    u8 key_changed;
    u8 key_event;
    int16 throttle_input;
    int16 steering_input;
} ship_protocol_throttle_event_t;

typedef struct
{
    u16 raw;
    u16 adc_mv;
    u32 bat_mv;
    u8 level;
    u8 valid;
} ship_protocol_power_event_t;

typedef struct
{
    int8 status;
    u8 len;
    u8 stored_len;
    u8 bytes[SHIP_PROTOCOL_SPI_PS_EVENT_DATA_MAX];
} ship_protocol_spi_ps_event_t;

typedef struct
{
    ship_protocol_event_type_t type;
    ship_protocol_event_state_t state;
    u8 cmd;
    u8 payload_len;
    u8 switch_state;
    u8 point_valid;
    ship_protocol_key_action_t key_action;
    u8 pending;
    u16 sequence;
    u32 tick_ms;
    ship_protocol_throttle_event_t throttle;
    ship_protocol_point_t point;
    ship_protocol_power_event_t power;
    ship_protocol_spi_ps_event_t spi_ps;
} ship_protocol_event_snapshot_t;

/**
 * @brief 初始化船端协议状态机。
 *
 * 初始化只重置本地状态和派生配对参数，不直接访问 GPIO 或寄存器。
 * 无线硬件初始化应由 board_wireless_init() 完成。
 */
void ship_protocol_init(void);

/**
 * @brief 运行船端协议周期调度。
 *
 * 调用者可在主循环中高频调用；函数内部按约 10 ms 节拍执行配对、
 * 工作信道恢复、在线超时检查和回包逻辑。
 */
void ship_protocol_run_scheduler(void);

/**
 * @brief 查询是否已经完成旧遥控器配对。
 * @return 1 已配对，0 未配对。
 */
u8 ship_protocol_is_paired(void);

/**
 * @brief 向船端协议层发布一帧 SPI-PS 接收事件。
 * @param buffer 接收帧缓冲区；len 非 0 时不能为 NULL。
 * @param len 接收长度，单位字节。
 * @param status BoardDevices SPI-PS 读取结果，用于诊断是否截断或溢出。
 *
 * 该接口只缓存事件供协议/日志消费，不直接访问 SPI 寄存器或改变无线链路状态。
 */
void ship_protocol_publish_spi_ps_frame(const u8 *buffer, u8 len, int8 status);

/**
 * @brief 获取最近一次协议事件快照。
 * @param[out] snapshot 事件快照输出。
 * @return 1 有待消费事件，0 当前无待消费事件或参数为空。
 *
 * 该接口暴露最近一次解析后的业务事件快照，供日志、联调或上层观察使用。
 * 真实控制动作已在协议分发路径中提交给 ShipControl/AutoDrive 状态机。
 */
u8 ship_protocol_get_event_snapshot(ship_protocol_event_snapshot_t *snapshot);

/**
 * @brief 读取并清除最近一次待消费协议事件。
 * @param[out] snapshot 事件快照输出。
 * @return 1 成功取出事件，0 当前无事件或参数为空。
 */
u8 ship_protocol_take_event(ship_protocol_event_snapshot_t *snapshot);

#endif
