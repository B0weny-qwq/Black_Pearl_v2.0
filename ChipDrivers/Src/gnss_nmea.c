#include "gnss_nmea.h"

#define GNSS_DISPATCH_IGNORE      0U
#define GNSS_DISPATCH_OK          1U
#define GNSS_DISPATCH_PARSE_ERROR 2U

static void gnss_nmea_clear_state(gnss_nmea_t *parser);
static void gnss_nmea_clear_runtime(gnss_nmea_t *parser);
static u8 gnss_nmea_fifo_push(gnss_nmea_t *parser, u8 dat);
static u8 gnss_nmea_fifo_pop(gnss_nmea_t *parser, u8 *dat);
static void gnss_nmea_parse_byte(gnss_nmea_t *parser, u8 dat);
static void gnss_nmea_process_sentence(gnss_nmea_t *parser, char *sentence);
static u8 gnss_nmea_split_fields(char *payload, char **fields, u8 max_fields);
static u8 gnss_nmea_dispatch_sentence(gnss_nmea_t *parser, char **fields, u8 field_count);
static u8 gnss_nmea_parse_rmc(gnss_nmea_t *parser, char **fields, u8 field_count);
static u8 gnss_nmea_parse_gga(gnss_nmea_t *parser, char **fields, u8 field_count);
static u8 gnss_nmea_parse_gsa(gnss_nmea_t *parser, char **fields, u8 field_count);
static u8 gnss_nmea_parse_gsv(gnss_nmea_t *parser, char **fields, u8 field_count);
static u8 gnss_nmea_parse_vtg(gnss_nmea_t *parser, char **fields, u8 field_count);
static void gnss_nmea_update_talker(gnss_nmea_t *parser, const char *sentence_id);
static u8 gnss_nmea_parse_talker_id(const char *sentence_id);
static u8 gnss_nmea_field_present(const char *text);
static u8 gnss_nmea_hex_to_nibble(char ch, u8 *value);
static u8 gnss_nmea_parse_hex_byte(const char *text, u8 *value);
static u8 gnss_nmea_parse_u32(const char *text, u32 *value);
static u8 gnss_nmea_parse_u8(const char *text, u8 *value);
static u8 gnss_nmea_parse_decimal_scaled(const char *text, u8 frac_digits, int32 *value);
static u8 gnss_nmea_parse_utc(const char *text, u8 *hour, u8 *minute, u8 *second, u16 *msec);
static u8 gnss_nmea_parse_date(const char *text, u8 *day, u8 *month, u8 *year);
static u8 gnss_nmea_parse_coordinate1e7(const char *text, char hemi, int32 *value);
static u8 gnss_nmea_parse_legacy_coord_parts(const char *text, u8 whole_len, u16 *coord1, u16 *coord2);
static u8 gnss_nmea_is_new_rmc_timestamp(const gnss_nmea_state_t *state,
                                         u8 hour, u8 minute, u8 second, u16 msec,
                                         u8 day, u8 month, u8 year);
static u32 gnss_nmea_knots_x100_to_kmh_x100(u32 speed_knots_x100);
static u16 gnss_nmea_to_u16_non_negative(int32 value);
static u32 gnss_nmea_to_u32_non_negative(int32 value);

static void gnss_nmea_clear_state(gnss_nmea_t *parser)
{
    u16 i;
    u8 *raw;

    raw = (u8 *)&parser->state;
    for (i = 0U; i < (u16)sizeof(parser->state); i++) {
        raw[i] = 0U;
    }

    parser->state.talker[0] = '-';
    parser->state.talker[1] = '-';
    parser->state.talker[2] = 0;
    parser->state.rmc_status = 'V';
}

static void gnss_nmea_clear_runtime(gnss_nmea_t *parser)
{
    u16 i;

    parser->capture_active = 0U;
    parser->sentence_len = 0U;
    parser->fifo_head = 0U;
    parser->fifo_tail = 0U;
    parser->fifo_count = 0U;

    for (i = 0U; i < (u16)GNSS_NMEA_SENTENCE_BUFFER_SIZE; i++) {
        parser->sentence_buf[i] = 0;
    }
    for (i = 0U; i < (u16)GNSS_NMEA_FIFO_SIZE; i++) {
        parser->fifo[i] = 0U;
    }
}

void gnss_nmea_init(gnss_nmea_t *parser)
{
    if (parser == 0) {
        return;
    }

    gnss_nmea_clear_state(parser);
    gnss_nmea_clear_runtime(parser);
    parser->initialized = 1U;
}

void gnss_nmea_reset(gnss_nmea_t *parser)
{
    if (parser == 0) {
        return;
    }

    gnss_nmea_clear_state(parser);
    gnss_nmea_clear_runtime(parser);
}

static u8 gnss_nmea_fifo_push(gnss_nmea_t *parser, u8 dat)
{
    if (parser->fifo_count >= (u16)GNSS_NMEA_FIFO_SIZE) {
        parser->state.fifo_overflow_count++;
        return 1U;
    }

    parser->fifo[parser->fifo_head] = dat;
    parser->fifo_head++;
    if (parser->fifo_head >= (u16)GNSS_NMEA_FIFO_SIZE) {
        parser->fifo_head = 0U;
    }
    parser->fifo_count++;
    return 0U;
}

static u8 gnss_nmea_fifo_pop(gnss_nmea_t *parser, u8 *dat)
{
    if ((dat == 0) || (parser->fifo_count == 0U)) {
        return 1U;
    }

    *dat = parser->fifo[parser->fifo_tail];
    parser->fifo_tail++;
    if (parser->fifo_tail >= (u16)GNSS_NMEA_FIFO_SIZE) {
        parser->fifo_tail = 0U;
    }
    parser->fifo_count--;
    return 0U;
}

int8 gnss_nmea_feed_byte(gnss_nmea_t *parser, u8 dat)
{
    u8 queued;
    u8 fifo_dat;

    if ((parser == 0) || (parser->initialized == 0U)) {
        return GNSS_NMEA_ERR_PARAM;
    }

    queued = gnss_nmea_fifo_push(parser, dat);
    while (gnss_nmea_fifo_pop(parser, &fifo_dat) == 0U) {
        gnss_nmea_parse_byte(parser, fifo_dat);
    }

    return (queued == 0U) ? GNSS_NMEA_OK : GNSS_NMEA_ERR_PARAM;
}

int8 gnss_nmea_feed(gnss_nmea_t *parser, const u8 *buf, u8 len)
{
    u8 i;

    if ((parser == 0) || (buf == 0) || (len == 0U) || (parser->initialized == 0U)) {
        return GNSS_NMEA_ERR_PARAM;
    }

    for (i = 0U; i < len; i++) {
        (void)gnss_nmea_feed_byte(parser, buf[i]);
    }

    return GNSS_NMEA_OK;
}

void gnss_nmea_note_source_overflow(gnss_nmea_t *parser)
{
    if (parser != 0) {
        parser->state.source_overflow_count++;
    }
}

const gnss_nmea_state_t *gnss_nmea_get_state(const gnss_nmea_t *parser)
{
    if (parser == 0) {
        return 0;
    }
    return &parser->state;
}

static void gnss_nmea_parse_byte(gnss_nmea_t *parser, u8 dat)
{
    if (dat == '$') {
        parser->capture_active = 1U;
        parser->sentence_len = 0U;
        parser->sentence_buf[parser->sentence_len++] = '$';
        parser->sentence_buf[parser->sentence_len] = 0;
        return;
    }

    if (parser->capture_active == 0U) {
        return;
    }

    if (parser->sentence_len >= (u8)(GNSS_NMEA_SENTENCE_BUFFER_SIZE - 1U)) {
        parser->capture_active = 0U;
        parser->sentence_len = 0U;
        parser->state.sentence_overflow_count++;
        return;
    }

    parser->sentence_buf[parser->sentence_len++] = (char)dat;
    parser->sentence_buf[parser->sentence_len] = 0;

    if (dat == '\n') {
        gnss_nmea_process_sentence(parser, parser->sentence_buf);
        parser->capture_active = 0U;
        parser->sentence_len = 0U;
    }
}

static void gnss_nmea_process_sentence(gnss_nmea_t *parser, char *sentence)
{
    char *scan;
    char *star;
    char *payload;
    char *fields[GNSS_NMEA_MAX_FIELDS];
    u8 checksum_calc;
    u8 checksum_recv;
    u8 field_count;
    u8 result;

    if ((sentence == 0) || (sentence[0] != '$')) {
        return;
    }

    checksum_calc = 0U;
    star = 0;
    scan = sentence + 1;
    while (*scan != 0) {
        if (*scan == '*') {
            star = scan;
            break;
        }
        if ((*scan == '\r') || (*scan == '\n')) {
            parser->state.parse_error_count++;
            return;
        }
        checksum_calc ^= (u8)(*scan);
        scan++;
    }

    if ((star == 0) || (gnss_nmea_parse_hex_byte(star + 1, &checksum_recv) == 0U)) {
        parser->state.checksum_error_count++;
        return;
    }
    if (checksum_recv != checksum_calc) {
        parser->state.checksum_error_count++;
        return;
    }

    *star = 0;
    payload = sentence + 1;
    field_count = gnss_nmea_split_fields(payload, fields, GNSS_NMEA_MAX_FIELDS);
    if ((field_count == 0U) || (fields[0] == 0) ||
        (fields[0][0] == 0) || (fields[0][1] == 0) ||
        (fields[0][2] == 0) || (fields[0][3] == 0) ||
        (fields[0][4] == 0)) {
        parser->state.parse_error_count++;
        return;
    }

    result = gnss_nmea_dispatch_sentence(parser, fields, field_count);
    if (result == GNSS_DISPATCH_OK) {
        parser->state.sentence_ok_count++;
    } else if (result == GNSS_DISPATCH_PARSE_ERROR) {
        parser->state.parse_error_count++;
    }
}

static u8 gnss_nmea_split_fields(char *payload, char **fields, u8 max_fields)
{
    u8 count;
    char *scan;

    if ((payload == 0) || (fields == 0) || (max_fields == 0U)) {
        return 0U;
    }

    count = 0U;
    fields[count++] = payload;

    scan = payload;
    while (*scan != 0) {
        if (*scan == ',') {
            *scan = 0;
            if (count < max_fields) {
                fields[count++] = scan + 1;
            }
        }
        scan++;
    }

    return count;
}

static u8 gnss_nmea_dispatch_sentence(gnss_nmea_t *parser, char **fields, u8 field_count)
{
    char *type;
    u8 parse_ok;

    if ((fields == 0) || (field_count == 0U) || (fields[0] == 0)) {
        return GNSS_DISPATCH_PARSE_ERROR;
    }

    type = fields[0] + 2;

    if ((type[0] == 'R') && (type[1] == 'M') && (type[2] == 'C') && (type[3] == 0)) {
        parse_ok = gnss_nmea_parse_rmc(parser, fields, field_count);
        if (parse_ok != 0U) {
            gnss_nmea_update_talker(parser, fields[0]);
            return GNSS_DISPATCH_OK;
        }
        return GNSS_DISPATCH_PARSE_ERROR;
    }
    if ((type[0] == 'G') && (type[1] == 'G') && (type[2] == 'A') && (type[3] == 0)) {
        parse_ok = gnss_nmea_parse_gga(parser, fields, field_count);
        if (parse_ok != 0U) {
            gnss_nmea_update_talker(parser, fields[0]);
            return GNSS_DISPATCH_OK;
        }
        return GNSS_DISPATCH_PARSE_ERROR;
    }
    if ((type[0] == 'G') && (type[1] == 'S') && (type[2] == 'A') && (type[3] == 0)) {
        parse_ok = gnss_nmea_parse_gsa(parser, fields, field_count);
        if (parse_ok != 0U) {
            gnss_nmea_update_talker(parser, fields[0]);
            return GNSS_DISPATCH_OK;
        }
        return GNSS_DISPATCH_PARSE_ERROR;
    }
    if ((type[0] == 'G') && (type[1] == 'S') && (type[2] == 'V') && (type[3] == 0)) {
        parse_ok = gnss_nmea_parse_gsv(parser, fields, field_count);
        if (parse_ok != 0U) {
            gnss_nmea_update_talker(parser, fields[0]);
            return GNSS_DISPATCH_OK;
        }
        return GNSS_DISPATCH_PARSE_ERROR;
    }
    if ((type[0] == 'V') && (type[1] == 'T') && (type[2] == 'G') && (type[3] == 0)) {
        parse_ok = gnss_nmea_parse_vtg(parser, fields, field_count);
        if (parse_ok != 0U) {
            gnss_nmea_update_talker(parser, fields[0]);
            return GNSS_DISPATCH_OK;
        }
        return GNSS_DISPATCH_PARSE_ERROR;
    }

    return GNSS_DISPATCH_IGNORE;
}

static u8 gnss_nmea_parse_rmc(gnss_nmea_t *parser, char **fields, u8 field_count)
{
    gnss_nmea_state_t *state;
    u8 hour;
    u8 minute;
    u8 second;
    u16 msec;
    u8 day;
    u8 month;
    u8 year;
    int32 lat_deg1e7;
    int32 lon_deg1e7;
    int32 speed_knots_x100;
    int32 course_deg_x100;
    u16 legacy_lat1;
    u16 legacy_lat2;
    u16 legacy_lon1;
    u16 legacy_lon2;
    u8 fix_valid;
    u8 has_speed;
    u8 has_course;
    char hemi_ns;
    char hemi_ew;
    char status;

    state = &parser->state;

    if (field_count < 10U) {
        return 0U;
    }
    if ((gnss_nmea_field_present(fields[1]) == 0U) ||
        (gnss_nmea_field_present(fields[2]) == 0U)) {
        return 0U;
    }
    if (gnss_nmea_parse_utc(fields[1], &hour, &minute, &second, &msec) == 0U) {
        return 0U;
    }
    day = 0U;
    month = 0U;
    year = 0U;
    if (gnss_nmea_field_present(fields[9]) != 0U) {
        if (gnss_nmea_parse_date(fields[9], &day, &month, &year) == 0U) {
            return 0U;
        }
    }

    status = fields[2][0];
    if ((status != 'A') && (status != 'V')) {
        return 0U;
    }
    fix_valid = (status == 'A') ? 1U : 0U;

    if (fix_valid != 0U) {
        if ((gnss_nmea_field_present(fields[3]) == 0U) ||
            (gnss_nmea_field_present(fields[4]) == 0U) ||
            (gnss_nmea_field_present(fields[5]) == 0U) ||
            (gnss_nmea_field_present(fields[6]) == 0U)) {
            return 0U;
        }

        hemi_ns = fields[4][0];
        hemi_ew = fields[6][0];
        if ((gnss_nmea_parse_coordinate1e7(fields[3], hemi_ns, &lat_deg1e7) == 0U) ||
            (gnss_nmea_parse_coordinate1e7(fields[5], hemi_ew, &lon_deg1e7) == 0U)) {
            return 0U;
        }
        if ((gnss_nmea_parse_legacy_coord_parts(fields[3], 4U, &legacy_lat1, &legacy_lat2) == 0U) ||
            (gnss_nmea_parse_legacy_coord_parts(fields[5], 5U, &legacy_lon1, &legacy_lon2) == 0U)) {
            return 0U;
        }
    }

    has_speed = 0U;
    if (gnss_nmea_field_present(fields[7]) != 0U) {
        if (gnss_nmea_parse_decimal_scaled(fields[7], 2U, &speed_knots_x100) == 0U) {
            return 0U;
        }
        has_speed = 1U;
    }

    has_course = 0U;
    if (gnss_nmea_field_present(fields[8]) != 0U) {
        if (gnss_nmea_parse_decimal_scaled(fields[8], 2U, &course_deg_x100) == 0U) {
            return 0U;
        }
        has_course = 1U;
    }

    if (gnss_nmea_is_new_rmc_timestamp(state, hour, minute, second, msec, day, month, year) != 0U) {
        state->update_sequence++;
    }

    state->rmc_status = (u8)status;
    state->fix_valid = fix_valid;
    state->utc_hour = hour;
    state->utc_minute = minute;
    state->utc_second = second;
    state->utc_millisecond = msec;
    state->date_day = day;
    state->date_month = month;
    state->date_year = year;

    if (fix_valid != 0U) {
        state->lat_deg1e7 = lat_deg1e7;
        state->lon_deg1e7 = lon_deg1e7;
        state->legacy_coord_valid = 1U;
        state->legacy_lat_dir = (u8)hemi_ns;
        state->legacy_lon_dir = (u8)hemi_ew;
        state->legacy_lat1 = legacy_lat1;
        state->legacy_lat2 = legacy_lat2;
        state->legacy_lon1 = legacy_lon1;
        state->legacy_lon2 = legacy_lon2;
    } else {
        state->legacy_coord_valid = 0U;
    }

    if (has_speed != 0U) {
        state->speed_knots_x100 = gnss_nmea_to_u32_non_negative(speed_knots_x100);
        state->speed_kmh_x100 = gnss_nmea_knots_x100_to_kmh_x100(state->speed_knots_x100);
    }
    if (has_course != 0U) {
        state->course_deg_x100 = gnss_nmea_to_u16_non_negative(course_deg_x100);
    }

    return 1U;
}

static u8 gnss_nmea_parse_gga(gnss_nmea_t *parser, char **fields, u8 field_count)
{
    gnss_nmea_state_t *state;
    u8 quality;
    u8 satellites;
    int32 hdop_x100;
    int32 altitude_cm;
    u8 has_satellites;
    u8 has_hdop;
    u8 has_altitude;

    state = &parser->state;

    if (field_count < 10U) {
        return 0U;
    }
    if (gnss_nmea_field_present(fields[6]) == 0U) {
        return 0U;
    }
    if (gnss_nmea_parse_u8(fields[6], &quality) == 0U) {
        return 0U;
    }

    has_satellites = 0U;
    if (gnss_nmea_field_present(fields[7]) != 0U) {
        if (gnss_nmea_parse_u8(fields[7], &satellites) == 0U) {
            return 0U;
        }
        has_satellites = 1U;
    }

    has_hdop = 0U;
    if (gnss_nmea_field_present(fields[8]) != 0U) {
        if (gnss_nmea_parse_decimal_scaled(fields[8], 2U, &hdop_x100) == 0U) {
            return 0U;
        }
        has_hdop = 1U;
    }

    has_altitude = 0U;
    if (gnss_nmea_field_present(fields[9]) != 0U) {
        if (gnss_nmea_parse_decimal_scaled(fields[9], 2U, &altitude_cm) == 0U) {
            return 0U;
        }
        has_altitude = 1U;
    }

    state->fix_quality = quality;
    state->satellites_used_gsa = 0U;
    if (quality == 0U) {
        state->fix_valid = 0U;
    }
    if (has_satellites != 0U) {
        state->satellites_used = satellites;
    }
    if (has_hdop != 0U) {
        state->hdop_x100 = gnss_nmea_to_u16_non_negative(hdop_x100);
    }
    if (has_altitude != 0U) {
        state->altitude_cm = altitude_cm;
    }

    return 1U;
}

static u8 gnss_nmea_parse_gsa(gnss_nmea_t *parser, char **fields, u8 field_count)
{
    gnss_nmea_state_t *state;
    u8 fix_mode;
    u8 prn_count;
    u8 prn_index;
    u8 prn_complete;
    int32 pdop_x100;
    int32 hdop_x100;
    int32 vdop_x100;
    u8 has_pdop;
    u8 has_hdop;
    u8 has_vdop;

    state = &parser->state;

    if (field_count < 18U) {
        return 0U;
    }
    if (gnss_nmea_field_present(fields[2]) == 0U) {
        return 0U;
    }
    if (gnss_nmea_parse_u8(fields[2], &fix_mode) == 0U) {
        return 0U;
    }

    prn_count = 0U;
    prn_complete = 1U;
    for (prn_index = 3U; prn_index <= 14U; prn_index++) {
        if ((prn_index < field_count) && (gnss_nmea_field_present(fields[prn_index]) != 0U)) {
            prn_count++;
        } else {
            prn_complete = 0U;
        }
    }

    has_pdop = 0U;
    if (gnss_nmea_field_present(fields[15]) != 0U) {
        if (gnss_nmea_parse_decimal_scaled(fields[15], 2U, &pdop_x100) == 0U) {
            return 0U;
        }
        has_pdop = 1U;
    }

    has_hdop = 0U;
    if (gnss_nmea_field_present(fields[16]) != 0U) {
        if (gnss_nmea_parse_decimal_scaled(fields[16], 2U, &hdop_x100) == 0U) {
            return 0U;
        }
        has_hdop = 1U;
    }

    has_vdop = 0U;
    if (gnss_nmea_field_present(fields[17]) != 0U) {
        if (gnss_nmea_parse_decimal_scaled(fields[17], 2U, &vdop_x100) == 0U) {
            return 0U;
        }
        has_vdop = 1U;
    }

    state->fix_mode = fix_mode;
    state->satellites_used_gsa = (prn_complete != 0U) ? prn_count : 0U;
    if (has_pdop != 0U) {
        state->pdop_x100 = gnss_nmea_to_u16_non_negative(pdop_x100);
    }
    if (has_hdop != 0U) {
        state->hdop_x100 = gnss_nmea_to_u16_non_negative(hdop_x100);
    }
    if (has_vdop != 0U) {
        state->vdop_x100 = gnss_nmea_to_u16_non_negative(vdop_x100);
    }

    return 1U;
}

static u8 gnss_nmea_parse_gsv(gnss_nmea_t *parser, char **fields, u8 field_count)
{
    gnss_nmea_state_t *state;
    u8 sentence_index;
    u8 satellites_view;
    u8 max_snr;
    u8 snr;
    u8 field_index;

    state = &parser->state;

    if (field_count < 4U) {
        return 0U;
    }
    if ((gnss_nmea_field_present(fields[2]) == 0U) ||
        (gnss_nmea_field_present(fields[3]) == 0U)) {
        return 0U;
    }
    if ((gnss_nmea_parse_u8(fields[2], &sentence_index) == 0U) ||
        (gnss_nmea_parse_u8(fields[3], &satellites_view) == 0U) ||
        (sentence_index == 0U)) {
        return 0U;
    }

    max_snr = state->max_snr;
    if (sentence_index == 1U) {
        max_snr = 0U;
    }

    field_index = 7U;
    while (field_index < field_count) {
        if (gnss_nmea_field_present(fields[field_index]) != 0U) {
            if (gnss_nmea_parse_u8(fields[field_index], &snr) == 0U) {
                return 0U;
            }
            if (snr > max_snr) {
                max_snr = snr;
            }
        }
        field_index = (u8)(field_index + 4U);
    }

    state->satellites_view = satellites_view;
    state->max_snr = max_snr;
    return 1U;
}

static u8 gnss_nmea_parse_vtg(gnss_nmea_t *parser, char **fields, u8 field_count)
{
    gnss_nmea_state_t *state;
    int32 course_deg_x100;
    int32 speed_knots_x100;
    int32 speed_kmh_x100;
    u8 has_course;
    u8 has_speed_knots;
    u8 has_speed_kmh;

    state = &parser->state;

    if (field_count < 8U) {
        return 0U;
    }

    has_course = 0U;
    if (gnss_nmea_field_present(fields[1]) != 0U) {
        if (gnss_nmea_parse_decimal_scaled(fields[1], 2U, &course_deg_x100) == 0U) {
            return 0U;
        }
        has_course = 1U;
    }

    has_speed_knots = 0U;
    if (gnss_nmea_field_present(fields[5]) != 0U) {
        if (gnss_nmea_parse_decimal_scaled(fields[5], 2U, &speed_knots_x100) == 0U) {
            return 0U;
        }
        has_speed_knots = 1U;
    }

    has_speed_kmh = 0U;
    if (gnss_nmea_field_present(fields[7]) != 0U) {
        if (gnss_nmea_parse_decimal_scaled(fields[7], 2U, &speed_kmh_x100) == 0U) {
            return 0U;
        }
        has_speed_kmh = 1U;
    }

    if ((has_course == 0U) && (has_speed_knots == 0U) && (has_speed_kmh == 0U)) {
        return 0U;
    }

    if (has_course != 0U) {
        state->course_deg_x100 = gnss_nmea_to_u16_non_negative(course_deg_x100);
    }
    if (has_speed_knots != 0U) {
        state->speed_knots_x100 = gnss_nmea_to_u32_non_negative(speed_knots_x100);
        if (has_speed_kmh == 0U) {
            state->speed_kmh_x100 = gnss_nmea_knots_x100_to_kmh_x100(state->speed_knots_x100);
        }
    }
    if (has_speed_kmh != 0U) {
        state->speed_kmh_x100 = gnss_nmea_to_u32_non_negative(speed_kmh_x100);
    }

    return 1U;
}

static void gnss_nmea_update_talker(gnss_nmea_t *parser, const char *sentence_id)
{
    if ((sentence_id == 0) || (sentence_id[0] == 0) || (sentence_id[1] == 0)) {
        parser->state.talker[0] = '-';
        parser->state.talker[1] = '-';
        parser->state.talker[2] = 0;
        parser->state.talker_id = GNSS_NMEA_TALKER_UNKNOWN;
        return;
    }

    parser->state.talker[0] = sentence_id[0];
    parser->state.talker[1] = sentence_id[1];
    parser->state.talker[2] = 0;
    parser->state.talker_id = gnss_nmea_parse_talker_id(sentence_id);
}

static u8 gnss_nmea_parse_talker_id(const char *sentence_id)
{
    if ((sentence_id[0] == 'G') && (sentence_id[1] == 'P')) {
        return GNSS_NMEA_TALKER_GP;
    }
    if ((sentence_id[0] == 'B') && (sentence_id[1] == 'D')) {
        return GNSS_NMEA_TALKER_BD;
    }
    if ((sentence_id[0] == 'G') && (sentence_id[1] == 'N')) {
        return GNSS_NMEA_TALKER_GN;
    }
    return GNSS_NMEA_TALKER_UNKNOWN;
}

static u8 gnss_nmea_field_present(const char *text)
{
    return (u8)((text != 0) && (text[0] != 0));
}

static u8 gnss_nmea_hex_to_nibble(char ch, u8 *value)
{
    if ((value == 0) || (ch == 0)) {
        return 0U;
    }

    if ((ch >= '0') && (ch <= '9')) {
        *value = (u8)(ch - '0');
        return 1U;
    }
    if ((ch >= 'A') && (ch <= 'F')) {
        *value = (u8)(ch - 'A' + 10);
        return 1U;
    }
    if ((ch >= 'a') && (ch <= 'f')) {
        *value = (u8)(ch - 'a' + 10);
        return 1U;
    }
    return 0U;
}

static u8 gnss_nmea_parse_hex_byte(const char *text, u8 *value)
{
    u8 high;
    u8 low;

    if ((text == 0) || (value == 0)) {
        return 0U;
    }
    if ((gnss_nmea_hex_to_nibble(text[0], &high) == 0U) ||
        (gnss_nmea_hex_to_nibble(text[1], &low) == 0U)) {
        return 0U;
    }

    *value = (u8)((high << 4) | low);
    return 1U;
}

static u8 gnss_nmea_parse_u32(const char *text, u32 *value)
{
    u32 result;
    u8 digit_found;
    char ch;

    if ((text == 0) || (value == 0) || (text[0] == 0)) {
        return 0U;
    }

    result = 0UL;
    digit_found = 0U;
    while (*text != 0) {
        ch = *text;
        if ((ch < '0') || (ch > '9')) {
            return 0U;
        }
        digit_found = 1U;
        if ((result > 429496729UL) ||
            ((result == 429496729UL) && ((u8)(ch - '0') > 5U))) {
            return 0U;
        }
        result = result * 10UL + (u32)(ch - '0');
        text++;
    }

    if (digit_found == 0U) {
        return 0U;
    }

    *value = result;
    return 1U;
}

static u8 gnss_nmea_parse_u8(const char *text, u8 *value)
{
    u32 temp;

    if ((gnss_nmea_parse_u32(text, &temp) == 0U) || (temp > 255UL)) {
        return 0U;
    }

    *value = (u8)temp;
    return 1U;
}

static u8 gnss_nmea_parse_decimal_scaled(const char *text, u8 frac_digits, int32 *value)
{
    u32 scale_base;
    u32 integer_part;
    u32 fractional_part;
    u32 combined_value;
    u8 digits_found;
    u8 dot_seen;
    u8 frac_count;
    u8 negative;
    u8 digit;
    char ch;

    if ((text == 0) || (value == 0) || (text[0] == 0)) {
        return 0U;
    }
    if (frac_digits > 4U) {
        return 0U;
    }

    scale_base = 1UL;
    while (frac_digits > 0U) {
        scale_base *= 10UL;
        frac_digits--;
    }

    integer_part = 0UL;
    fractional_part = 0UL;
    digits_found = 0U;
    dot_seen = 0U;
    frac_count = 0U;
    negative = 0U;

    if (*text == '-') {
        negative = 1U;
        text++;
    } else if (*text == '+') {
        text++;
    }

    while (*text != 0) {
        ch = *text;
        if ((ch >= '0') && (ch <= '9')) {
            digits_found = 1U;
            digit = (u8)(ch - '0');
            if (dot_seen == 0U) {
                if ((integer_part > 429496729UL) ||
                    ((integer_part == 429496729UL) && (digit > 5U))) {
                    return 0U;
                }
                integer_part = integer_part * 10UL + (u32)digit;
            } else {
                if (frac_count < 4U) {
                    fractional_part = fractional_part * 10UL + (u32)digit;
                    frac_count++;
                }
            }
        } else if ((ch == '.') && (dot_seen == 0U)) {
            dot_seen = 1U;
        } else {
            return 0U;
        }
        text++;
    }

    if (digits_found == 0U) {
        return 0U;
    }

    while (frac_count < 4U) {
        fractional_part *= 10UL;
        frac_count++;
    }

    if (scale_base == 1UL) {
        fractional_part = 0UL;
    } else if (scale_base == 10UL) {
        fractional_part = (fractional_part + 500UL) / 1000UL;
    } else if (scale_base == 100UL) {
        fractional_part = (fractional_part + 50UL) / 100UL;
    } else if (scale_base == 1000UL) {
        fractional_part = (fractional_part + 5UL) / 10UL;
    }

    if (fractional_part >= scale_base) {
        integer_part++;
        fractional_part -= scale_base;
    }

    if (integer_part > (2147483647UL / scale_base)) {
        return 0U;
    }
    combined_value = integer_part * scale_base + fractional_part;
    if (combined_value > 2147483647UL) {
        return 0U;
    }

    *value = (negative != 0U) ? -((int32)combined_value) : (int32)combined_value;
    return 1U;
}

static u8 gnss_nmea_parse_utc(const char *text, u8 *hour, u8 *minute, u8 *second, u16 *msec)
{
    u32 whole_part;
    u32 frac_part;
    char frac_buf[5];
    u8 frac_len;
    const char *dot;

    if ((text == 0) || (hour == 0) || (minute == 0) || (second == 0) || (msec == 0)) {
        return 0U;
    }
    if ((text[0] == 0) || (text[1] == 0) || (text[2] == 0) ||
        (text[3] == 0) || (text[4] == 0) || (text[5] == 0)) {
        return 0U;
    }

    dot = text;
    while ((*dot != 0) && (*dot != '.')) {
        dot++;
    }

    {
        char whole_buf[7];
        whole_buf[0] = text[0];
        whole_buf[1] = text[1];
        whole_buf[2] = text[2];
        whole_buf[3] = text[3];
        whole_buf[4] = text[4];
        whole_buf[5] = text[5];
        whole_buf[6] = 0;
        if (gnss_nmea_parse_u32(whole_buf, &whole_part) == 0U) {
            return 0U;
        }
    }

    *hour = (u8)(whole_part / 10000UL);
    *minute = (u8)((whole_part / 100UL) % 100UL);
    *second = (u8)(whole_part % 100UL);

    if ((*hour > 23U) || (*minute > 59U) || (*second > 59U)) {
        return 0U;
    }

    frac_part = 0UL;
    frac_len = 0U;
    if (*dot == '.') {
        dot++;
        while (*dot != 0) {
            if ((*dot < '0') || (*dot > '9')) {
                return 0U;
            }
            if (frac_len < 3U) {
                frac_buf[frac_len++] = *dot;
            }
            dot++;
        }
    }
    while (frac_len < 3U) {
        frac_buf[frac_len++] = '0';
    }
    frac_buf[3] = 0;
    if (gnss_nmea_parse_u32(frac_buf, &frac_part) == 0U) {
        return 0U;
    }

    *msec = (u16)frac_part;
    return 1U;
}

static u8 gnss_nmea_parse_date(const char *text, u8 *day, u8 *month, u8 *year)
{
    char buf[3];

    if ((text == 0) || (day == 0) || (month == 0) || (year == 0)) {
        return 0U;
    }
    if ((text[0] == 0) || (text[1] == 0) || (text[2] == 0) ||
        (text[3] == 0) || (text[4] == 0) || (text[5] == 0)) {
        return 0U;
    }

    buf[2] = 0;
    buf[0] = text[0];
    buf[1] = text[1];
    if (gnss_nmea_parse_u8(buf, day) == 0U) {
        return 0U;
    }
    buf[0] = text[2];
    buf[1] = text[3];
    if (gnss_nmea_parse_u8(buf, month) == 0U) {
        return 0U;
    }
    buf[0] = text[4];
    buf[1] = text[5];
    if (gnss_nmea_parse_u8(buf, year) == 0U) {
        return 0U;
    }

    if ((*day == 0U) || (*day > 31U) || (*month == 0U) || (*month > 12U)) {
        return 0U;
    }
    return 1U;
}

static u8 gnss_nmea_parse_coordinate1e7(const char *text, char hemi, int32 *value)
{
    u32 whole_part;
    u32 frac_part;
    u32 minutes_scaled1e4;
    u32 degrees;
    u32 deg1e7;
    const char *dot;
    char whole_buf[12];
    char frac_buf[5];
    u8 expected_whole_len;
    u8 whole_len;
    u8 frac_len;

    if ((text == 0) || (value == 0) || (text[0] == 0)) {
        return 0U;
    }
    if ((hemi != 'N') && (hemi != 'S') && (hemi != 'E') && (hemi != 'W')) {
        return 0U;
    }

    dot = text;
    while ((*dot != 0) && (*dot != '.')) {
        dot++;
    }

    expected_whole_len = (((hemi == 'N') || (hemi == 'S')) ? 4U : 5U);
    whole_len = (u8)(dot - text);
    if ((whole_len != expected_whole_len) || (whole_len >= (u8)sizeof(whole_buf))) {
        return 0U;
    }
    {
        u8 i;
        for (i = 0U; i < whole_len; i++) {
            whole_buf[i] = text[i];
        }
        whole_buf[whole_len] = 0;
    }
    if (gnss_nmea_parse_u32(whole_buf, &whole_part) == 0U) {
        return 0U;
    }

    frac_len = 0U;
    if (*dot == '.') {
        dot++;
        while (*dot != 0) {
            if ((*dot < '0') || (*dot > '9')) {
                return 0U;
            }
            if (frac_len < 4U) {
                frac_buf[frac_len++] = *dot;
            }
            dot++;
        }
    }
    while (frac_len < 4U) {
        frac_buf[frac_len++] = '0';
    }
    frac_buf[4] = 0;
    if (gnss_nmea_parse_u32(frac_buf, &frac_part) == 0U) {
        return 0U;
    }

    degrees = whole_part / 100UL;
    minutes_scaled1e4 = (whole_part % 100UL) * 10000UL + frac_part;
    if (minutes_scaled1e4 >= 600000UL) {
        return 0U;
    }
    if (((hemi == 'N') || (hemi == 'S')) &&
        ((degrees > 90UL) || ((degrees == 90UL) && (minutes_scaled1e4 != 0UL)))) {
        return 0U;
    }
    if (((hemi == 'E') || (hemi == 'W')) &&
        ((degrees > 180UL) || ((degrees == 180UL) && (minutes_scaled1e4 != 0UL)))) {
        return 0U;
    }
    deg1e7 = degrees * 10000000UL + (minutes_scaled1e4 * 100UL + 3UL) / 6UL;

    *value = ((hemi == 'S') || (hemi == 'W')) ? -((int32)deg1e7) : (int32)deg1e7;
    return 1U;
}

static u8 gnss_nmea_parse_legacy_coord_parts(const char *text, u8 whole_len, u16 *coord1, u16 *coord2)
{
    const char *dot;
    char whole_buf[6];
    char frac_buf[5];
    u8 i;
    u8 frac_len;
    u32 whole_part;
    u32 frac_part;

    if ((text == 0) || (coord1 == 0) || (coord2 == 0) ||
        ((whole_len != 4U) && (whole_len != 5U))) {
        return 0U;
    }

    dot = text;
    while ((*dot != 0) && (*dot != '.')) {
        dot++;
    }
    if ((u8)(dot - text) != whole_len) {
        return 0U;
    }

    for (i = 0U; i < whole_len; i++) {
        if ((text[i] < '0') || (text[i] > '9')) {
            return 0U;
        }
        whole_buf[i] = text[i];
    }
    whole_buf[whole_len] = 0;
    if (gnss_nmea_parse_u32(whole_buf, &whole_part) == 0U) {
        return 0U;
    }

    frac_len = 0U;
    if (*dot == '.') {
        dot++;
        while (*dot != 0) {
            if ((*dot < '0') || (*dot > '9')) {
                return 0U;
            }
            if (frac_len < 4U) {
                frac_buf[frac_len++] = *dot;
            }
            dot++;
        }
    }
    while (frac_len < 4U) {
        frac_buf[frac_len++] = '0';
    }
    frac_buf[4] = 0;
    if (gnss_nmea_parse_u32(frac_buf, &frac_part) == 0U) {
        return 0U;
    }

    *coord1 = (u16)whole_part;
    *coord2 = (u16)frac_part;
    return 1U;
}

static u8 gnss_nmea_is_new_rmc_timestamp(const gnss_nmea_state_t *state,
                                         u8 hour, u8 minute, u8 second, u16 msec,
                                         u8 day, u8 month, u8 year)
{
    if ((state->utc_hour != hour) ||
        (state->utc_minute != minute) ||
        (state->utc_second != second) ||
        (state->utc_millisecond != msec) ||
        (state->date_day != day) ||
        (state->date_month != month) ||
        (state->date_year != year)) {
        return 1U;
    }
    return 0U;
}

static u32 gnss_nmea_knots_x100_to_kmh_x100(u32 speed_knots_x100)
{
    if (speed_knots_x100 > ((0xFFFFFFFFUL - 500UL) / 1852UL)) {
        return 0xFFFFFFFFUL;
    }
    return (speed_knots_x100 * 1852UL + 500UL) / 1000UL;
}

static u16 gnss_nmea_to_u16_non_negative(int32 value)
{
    if (value <= 0) {
        return 0U;
    }
    if (value > 65535L) {
        return 65535U;
    }
    return (u16)value;
}

static u32 gnss_nmea_to_u32_non_negative(int32 value)
{
    if (value <= 0L) {
        return 0UL;
    }
    return (u32)value;
}
