/**
 * @file board_gps.h
 * @brief Black Pearl 板级 GPS 设备接口。
 *
 * 本文件属于 BoardDevices 层，隐藏 UART2 端口、引脚复用和 GNSS NMEA 解析器绑定。
 * App 层只通过初始化、轮询和状态查询接口使用 GPS，不直接接触 UART 驱动或解析器内部缓冲。
 */

#ifndef __BOARD_GPS_H__
#define __BOARD_GPS_H__

#include "type_def.h"

#define BOARD_GPS_OK               0
#define BOARD_GPS_ERR_PARAM       -1
#define BOARD_GPS_ERR_DRIVER      -2
#define BOARD_GPS_ERR_NOT_INIT    -3

typedef struct
{
    char  talker[3];
    u8    talker_id;
    u8    fix_valid;
    u8    fix_quality;
    u8    rmc_status;
    u8    fix_mode;

    u8    utc_hour;
    u8    utc_minute;
    u8    utc_second;
    u16   utc_millisecond;

    u8    date_day;
    u8    date_month;
    u8    date_year;

    int32 lat_deg1e7;
    int32 lon_deg1e7;
    u8    legacy_coord_valid;
    u8    legacy_lat_dir;
    u8    legacy_lon_dir;
    u16   legacy_lat1;
    u16   legacy_lat2;
    u16   legacy_lon1;
    u16   legacy_lon2;

    u32   speed_knots_x100;
    u32   speed_kmh_x100;
    u16   course_deg_x100;

    u8    satellites_used;
    u8    satellites_used_gsa;
    u8    satellites_view;
    u16   hdop_x100;
    u16   pdop_x100;
    u16   vdop_x100;
    int32 altitude_cm;
    u8    max_snr;

    u32   update_sequence;

    u16   sentence_ok_count;
    u16   checksum_error_count;
    u16   parse_error_count;
    u16   uart_overflow_count;
    u16   fifo_overflow_count;
    u16   sentence_overflow_count;
} board_gps_state_t;

/**
 * @brief 初始化板级 GPS 串口和 NMEA 解析状态。
 * @return BOARD_GPS_OK 初始化成功。
 * @return BOARD_GPS_ERR_DRIVER 底层 UART 初始化失败。
 */
int8 board_gps_init(void);

/**
 * @brief 清空 GPS 解析状态和板级统计计数。
 *
 * 不重新配置 UART 引脚或波特率；通常用于通信异常后的软件复位。
 */
void board_gps_reset(void);

/**
 * @brief 轮询 UART 接收视图并投喂 NMEA 解析器。
 *
 * 调用者应在主循环中高频调用。函数不阻塞等待新字节，只处理当前已缓存数据。
 */
void board_gps_poll(void);

/**
 * @brief 查询 GPS 板级接口是否已初始化。
 * @return 1 已初始化，0 未初始化。
 */
u8 board_gps_is_ready(void);

/**
 * @brief 获取最近一次 GPS 解析状态快照。
 * @return 指向板级内部只读状态的指针；未初始化时仍返回当前缓存地址。
 */
const board_gps_state_t *board_gps_get_state(void);

#endif
