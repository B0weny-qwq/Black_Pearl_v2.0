/**
 * @file    GPS.h
 * @brief   GPS/GNSS NMEA0183 解析模块接口。
 * @author  boweny
 * @date    2026-05-07
 * @version v1.1
 *
 * @details
 * 基于 UART2 接收 GPS、北斗或多模 GNSS 模块输出的 NMEA0183 语句，
 * 在模块内部完成 FIFO 缓冲、逐字符状态机解析和状态快照维护。
 * 当前主链路用于给无线 `0x12` 状态回传提供卫星数、航向和经纬度字段。
 *
 * @hardware
 * - UART2: RX=P1.0 / TX=P1.1
 * - 波特率发生器: Timer2
 * - 系统时钟: Fosc = 24MHz
 *
 * @note
 * 本模块不修改 Driver 层 UART2 ISR，接收链路保持为
 * `RX2_Buffer -> GPS FIFO -> NMEA Parser`。
 *
 * @note
 * 调用顺序：`GPS_Init()` -> 主循环高频调用 `GPS_Poll()` -> `GPS_GetState()`。
 *
 * @see     Code_boweny/Device/GPS/GPS.c
 */

#ifndef __GPS_H__
#define __GPS_H__

#include "config.h"

#define GPS_BAUDRATE               GPS_UART_BAUDRATE  /**< GPS UART 默认波特率。 */
#define GPS_RAW_ECHO_ENABLE        0U                 /**< 原始 NMEA 字节回显开关，1=开启。 */
#define GPS_UART_FIFO_SIZE         256U               /**< GPS 模块内部 UART FIFO 大小。 */
#define GPS_SENTENCE_BUFFER_SIZE   96U                /**< 单条 NMEA 语句缓冲区大小。 */
#define GPS_MAX_FIELDS             24U                /**< 单条 NMEA 语句最大字段数量。 */

#define GPS_TALKER_UNKNOWN         0U  /**< 未识别 talker。 */
#define GPS_TALKER_GP              1U  /**< GPS talker，语句前缀 `GP`。 */
#define GPS_TALKER_BD              2U  /**< 北斗 talker，语句前缀 `BD`。 */
#define GPS_TALKER_GN              3U  /**< 多模 GNSS talker，语句前缀 `GN`。 */

/**
 * @brief   GPS/GNSS 解析状态快照。
 *
 * @details
 * 所有位置、速度、航向和精度字段均使用定点整数保存，不使用浮点。
 */
typedef struct
{
    char  talker[3];              /**< 最近有效语句的 talker 字符串，含结尾 `0`。 */
    u8    talker_id;              /**< 最近有效语句的 talker 类型，取值见 `GPS_TALKER_*`。 */
    u8    fix_valid;              /**< 定位有效标志，1=定位有效。 */
    u8    fix_quality;            /**< GGA 定位质量字段。 */
    u8    rmc_status;             /**< RMC 状态字段，通常 `A`=有效，`V`=无效。 */
    u8    fix_mode;               /**< GSA 定位模式字段。 */

    u8    utc_hour;               /**< UTC 小时。 */
    u8    utc_minute;             /**< UTC 分钟。 */
    u8    utc_second;             /**< UTC 秒。 */
    u16   utc_millisecond;        /**< UTC 毫秒。 */

    u8    date_day;               /**< UTC 日期中的日。 */
    u8    date_month;             /**< UTC 日期中的月。 */
    u8    date_year;              /**< UTC 日期中的年，范围 00~99。 */

    int32 lat_deg1e7;             /**< 纬度，单位 `deg * 1e7`，北纬为正。 */
    int32 lon_deg1e7;             /**< 经度，单位 `deg * 1e7`，东经为正。 */
    u8    legacy_coord_valid;     /**< 老版 `0x12` 坐标字段有效标志，来自 RMC 原始 NMEA 字符串。 */
    u8    legacy_lat_dir;         /**< RMC 纬度半球字符，通常为 `N` 或 `S`。 */
    u8    legacy_lon_dir;         /**< RMC 经度半球字符，通常为 `E` 或 `W`。 */
    u16   legacy_lat1;            /**< 老版纬度整数段，格式为 `ddmm`。 */
    u16   legacy_lat2;            /**< 老版纬度小数段，格式为小数点后 4 位。 */
    u16   legacy_lon1;            /**< 老版经度整数段，格式为 `dddmm`。 */
    u16   legacy_lon2;            /**< 老版经度小数段，格式为小数点后 4 位。 */

    u32   speed_knots_x100;       /**< 地速，单位 `knot * 100`。 */
    u32   speed_kmh_x100;         /**< 地速，单位 `km/h * 100`。 */
    u16   course_deg_x100;        /**< 航向角，单位 `deg * 100`。 */

    u8    satellites_used;        /**< 参与定位的卫星数，来自 GGA 第 7 字段。 */
    u8    satellites_used_gsa;    /**< 老版 `0x12` 优先使用的 GSA PRN 计数，为 0 时回退到 GGA 卫星数。 */
    u8    satellites_view;        /**< 可见卫星数。 */
    u16   hdop_x100;              /**< 水平精度因子，单位 `HDOP * 100`。 */
    u16   pdop_x100;              /**< 三维精度因子，单位 `PDOP * 100`。 */
    u16   vdop_x100;              /**< 垂直精度因子，单位 `VDOP * 100`。 */
    int32 altitude_cm;            /**< 海拔高度，单位 `cm`。 */
    u8    max_snr;                /**< GSV 语句中观测到的最大信噪比。 */

    u32   update_sequence;        /**< 状态更新序号，每次提交有效主状态后递增。 */

    u16   sentence_ok_count;       /**< 成功解析的 NMEA 语句计数。 */
    u16   checksum_error_count;    /**< NMEA 校验失败计数。 */
    u16   parse_error_count;       /**< 字段解析失败计数。 */
    u16   uart_overflow_count;     /**< Driver 层 UART 接收溢出计数。 */
    u16   fifo_overflow_count;     /**< GPS 模块内部 FIFO 溢出计数。 */
    u16   sentence_overflow_count; /**< 单条语句缓冲区溢出计数。 */
} GPS_State_t;

/**
 * @brief   初始化 GPS 模块。
 * @return  SUCCESS=成功，其他值表示 UART 或模块初始化失败。
 *
 * @details
 * 将 UART2 路由到 `P1.0/P1.1`，按 `GPS_BAUDRATE` 初始化 8N1 接收模式，
 * 并清空内部 FIFO、解析状态和 `GPS_State_t` 快照。
 */
s8 GPS_Init(void);

/**
 * @brief   复位 GPS 模块运行状态。
 * @return  无。
 *
 * @details
 * 清空 FIFO、语句缓冲、解析状态与 `GPS_State_t`，不重新初始化 UART2 外设。
 */
void GPS_Reset(void);

/**
 * @brief   轮询处理 GPS 接收数据。
 * @return  无。
 *
 * @details
 * 从 Driver 层 `RX2_Buffer` 拉取增量字节写入模块 FIFO，再逐字节驱动
 * NMEA 状态机解析。
 */
void GPS_Poll(void);

/**
 * @brief   获取当前 GPS 状态快照。
 * @return  指向模块内部只读 `GPS_State_t` 的指针。
 *
 * @note
 * 调用者不得修改返回指针指向的数据。
 */
const GPS_State_t *GPS_GetState(void);

#endif
