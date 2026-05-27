/**
 * @file gnss_nmea.h
 * @brief GNSS NMEA0183 字节流解析器。
 *
 * 本文件属于 ChipDrivers/解析器层，只解析 NMEA0183 字节流并维护定点导航状态快照。
 * UART 端口、引脚路由、DMA/中断接收和调度节拍由 BoardDevices 层负责。
 */

#ifndef __GNSS_NMEA_H__
#define __GNSS_NMEA_H__

#include "type_def.h"

#define GNSS_NMEA_OK                    0
#define GNSS_NMEA_ERR_PARAM            -1

#define GNSS_NMEA_FIFO_SIZE           256U
#define GNSS_NMEA_SENTENCE_BUFFER_SIZE 96U
#define GNSS_NMEA_MAX_FIELDS           24U

#define GNSS_NMEA_TALKER_UNKNOWN        0U
#define GNSS_NMEA_TALKER_GP             1U
#define GNSS_NMEA_TALKER_BD             2U
#define GNSS_NMEA_TALKER_GN             3U

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
    u16   source_overflow_count;
    u16   fifo_overflow_count;
    u16   sentence_overflow_count;
} gnss_nmea_state_t;

typedef struct
{
    gnss_nmea_state_t state;
    u8 initialized;
    u8 capture_active;
    u8 sentence_len;
    char sentence_buf[GNSS_NMEA_SENTENCE_BUFFER_SIZE];
    u8 fifo[GNSS_NMEA_FIFO_SIZE];
    u16 fifo_head;
    u16 fifo_tail;
    u16 fifo_count;
} gnss_nmea_t;

/**
 * @brief 初始化 NMEA 解析器实例。
 * @param parser 解析器对象，不能为 NULL。
 *
 * 函数清空运行缓冲和导航状态；不访问 UART 或任何硬件资源。
 */
void gnss_nmea_init(gnss_nmea_t *parser);

/**
 * @brief 复位 NMEA 解析器运行状态。
 * @param parser 解析器对象，不能为 NULL。
 *
 * 会清空句子缓冲、FIFO、计数器和导航状态，但保持实例可继续使用。
 */
void gnss_nmea_reset(gnss_nmea_t *parser);

/**
 * @brief 输入 1 字节 NMEA 数据并推进解析。
 * @param parser 解析器对象，必须已初始化。
 * @param dat 输入字节。
 * @return GNSS_NMEA_OK 投喂并解析完成。
 * @return GNSS_NMEA_ERR_PARAM 参数非法、未初始化或内部 FIFO 溢出。
 */
int8 gnss_nmea_feed_byte(gnss_nmea_t *parser, u8 dat);

/**
 * @brief 输入一段连续 NMEA 数据。
 * @param parser 解析器对象，必须已初始化。
 * @param buf 输入缓冲区，不能为 NULL。
 * @param len 输入长度，单位字节，不能为 0。
 * @return GNSS_NMEA_OK 全部投喂完成；参数非法时返回 GNSS_NMEA_ERR_PARAM。
 */
int8 gnss_nmea_feed(gnss_nmea_t *parser, const u8 *buf, u8 len);

/**
 * @brief 记录上游 UART/板级接收源溢出。
 * @param parser 解析器对象；为 NULL 时直接返回。
 */
void gnss_nmea_note_source_overflow(gnss_nmea_t *parser);

/**
 * @brief 获取当前 NMEA 解析状态。
 * @param parser 解析器对象。
 * @return 指向内部只读状态的指针；parser 为 NULL 时返回 NULL。
 */
const gnss_nmea_state_t *gnss_nmea_get_state(const gnss_nmea_t *parser);

#endif
