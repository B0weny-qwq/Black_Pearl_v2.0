/**
 * @file gnss_nmea.h
 * @brief GNSS NMEA0183 parser component.
 *
 * This file belongs to the chip/parser layer. It only parses NMEA0183 byte
 * streams and maintains a fixed-point navigation state snapshot. UART port
 * selection, pin routing, and board scheduling belong outside this module.
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

void gnss_nmea_init(gnss_nmea_t *parser);
void gnss_nmea_reset(gnss_nmea_t *parser);
int8 gnss_nmea_feed_byte(gnss_nmea_t *parser, u8 dat);
int8 gnss_nmea_feed(gnss_nmea_t *parser, const u8 *buf, u8 len);
void gnss_nmea_note_source_overflow(gnss_nmea_t *parser);
const gnss_nmea_state_t *gnss_nmea_get_state(const gnss_nmea_t *parser);

#endif
