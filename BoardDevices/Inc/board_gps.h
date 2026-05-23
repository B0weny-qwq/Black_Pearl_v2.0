/**
 * @file board_gps.h
 * @brief Black Pearl 板级 GPS 设备接口。
 *
 * 本文件属于 BoardDevices 层。负责隐藏 UART2 端口、引脚复用和 GNSS 解析器
 * 绑定细节，对上提供简单的初始化、轮询和状态查询接口。
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

int8 board_gps_init(void);
void board_gps_reset(void);
void board_gps_poll(void);
u8 board_gps_is_ready(void);
const board_gps_state_t *board_gps_get_state(void);

#endif
