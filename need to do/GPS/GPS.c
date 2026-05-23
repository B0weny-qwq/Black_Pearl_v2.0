/**
 * @file    GPS.c
 * @brief   GPS NMEA0183 解析模块实现
 * @author  boweny
 * @date    2026-04-24
 * @version v1.0
 *
 * @details
 * - 基于 UART2 接收 NMEA0183 字节流，接入 Driver 层 RX2_Buffer
 * - 在模块内部实现二级 FIFO 与逐字符状态机解析
 * - 支持 GGA / RMC / GSA / GSV / VTG 五类语句
 * - 使用定点整数保存位置、速度、航向、海拔和 DOP 信息
 *
 * @hardware
 *   - UART2: RX=P1.0 / TX=P1.1
 *   - 波特率发生器: Timer2（UART2 固定占用）
 *   - 系统时钟: Fosc = 24MHz
 *
 * @note    本模块不修改 Driver 层 ISR，仅消费 RX2_Buffer 中的增量数据
 * @note    禁止浮点运算，所有计算使用整数完成
 *
 * @see     Code_boweny/Device/GPS/GPS.h
 */

#include "GPS.h"
#include "STC32G_NVIC.h"
#include "STC32G_UART.h"
#include "..\..\Function\Log\Log.h"
#include "..\..\..\User\Task.h"

#define GPS_DISPATCH_IGNORE        0U
#define GPS_DISPATCH_OK            1U
#define GPS_DISPATCH_PARSE_ERROR   2U
#ifndef GPS_DIAG_LOG_ENABLE
#define GPS_DIAG_LOG_ENABLE        1U
#endif
#define GPS_DIAG_LOG_MS            1000UL

static GPS_State_t g_gps_state;

static u8 g_gps_initialized = 0;
static u8 g_gps_capture_active = 0;
static u8 g_gps_sentence_len = 0;
static u8 g_gps_uart_read_index = 0;

static char g_gps_sentence_buf[GPS_SENTENCE_BUFFER_SIZE];
static u8   g_gps_fifo[GPS_UART_FIFO_SIZE];
static u16  g_gps_fifo_head = 0;
static u16  g_gps_fifo_tail = 0;
static u16  g_gps_fifo_count = 0;
#if (GPS_DIAG_LOG_ENABLE != 0U)
static u32  g_gps_diag_rx_bytes = 0UL;
static u32  g_gps_diag_last_ms = 0UL;
static u8   g_gps_diag_logged_once = 0U;
#endif

static void GPS_ClearState(void);
static void GPS_ClearParser(void);
static void GPS_RawEchoByte(u8 dat);
static u8   GPS_FifoPush(u8 dat);
static u8   GPS_FifoPop(u8 *dat);
static void GPS_DrainUart2Buffer(void);
static void GPS_ParseByte(u8 dat);
static void GPS_ProcessSentence(char *sentence);
static u8   GPS_SplitFields(char *payload, char **fields, u8 max_fields);
static u8   GPS_DispatchSentence(char **fields, u8 field_count);
static u8   GPS_ParseRMC(char **fields, u8 field_count);
static u8   GPS_ParseGGA(char **fields, u8 field_count);
static u8   GPS_ParseGSA(char **fields, u8 field_count);
static u8   GPS_ParseGSV(char **fields, u8 field_count);
static u8   GPS_ParseVTG(char **fields, u8 field_count);
static void GPS_UpdateTalker(const char *sentence_id);
static u8   GPS_ParseTalkerId(const char *sentence_id);
static u8   GPS_FieldPresent(const char *text);
static u8   GPS_HexToNibble(char ch, u8 *value);
static u8   GPS_ParseHexByte(const char *text, u8 *value);
static u8   GPS_ParseU32(const char *text, u32 *value);
static u8   GPS_ParseU8(const char *text, u8 *value);
static u8   GPS_ParseDecimalScaled(const char *text, u8 frac_digits, int32 *value);
static u8   GPS_ParseUtc(const char *text, u8 *hour, u8 *minute, u8 *second, u16 *msec);
static u8   GPS_ParseDate(const char *text, u8 *day, u8 *month, u8 *year);
static u8   GPS_ParseCoordinate1e7(const char *text, char hemi, int32 *value);
static u8   GPS_ParseLegacyCoordParts(const char *text, u8 whole_len, u16 *coord1, u16 *coord2);
static u8   GPS_IsNewRmcTimestamp(u8 hour, u8 minute, u8 second, u16 msec,
                                  u8 day, u8 month, u8 year);
static u32  GPS_KnotsX100ToKmhX100(u32 speed_knots_x100);
static u16  GPS_ToU16NonNegative(int32 value);
static u32  GPS_ToU32NonNegative(int32 value);
#if (GPS_DIAG_LOG_ENABLE != 0U)
static void GPS_DiagLogPoll(void);
static void GPS_DiagSentenceSummary(const char *sentence, char *out, u8 out_len);
static void GPS_DiagLogSentence(const char *sentence, u8 result);
static void GPS_DiagLogParseFail(char *sentence, const char *reason);
#endif

static void GPS_ClearState(void)
{
    u16 i;
    u8 *raw;

    raw = (u8 *)&g_gps_state;
    for (i = 0; i < (u16)sizeof(GPS_State_t); i++) {
        raw[i] = 0;
    }

    g_gps_state.talker[0] = '-';
    g_gps_state.talker[1] = '-';
    g_gps_state.talker[2] = 0;
    g_gps_state.rmc_status = 'V';
}

static void GPS_ClearParser(void)
{
    u16 i;

    g_gps_capture_active = 0;
    g_gps_sentence_len = 0;
    g_gps_uart_read_index = 0;
    g_gps_fifo_head = 0;
    g_gps_fifo_tail = 0;
    g_gps_fifo_count = 0;
#if (GPS_DIAG_LOG_ENABLE != 0U)
    g_gps_diag_rx_bytes = 0UL;
    g_gps_diag_last_ms = 0UL;
    g_gps_diag_logged_once = 0U;
#endif

    for (i = 0; i < (u16)GPS_SENTENCE_BUFFER_SIZE; i++) {
        g_gps_sentence_buf[i] = 0;
    }
    for (i = 0; i < (u16)GPS_UART_FIFO_SIZE; i++) {
        g_gps_fifo[i] = 0;
    }
}

static void GPS_RawEchoByte(u8 dat)
{
#if (GPS_RAW_ECHO_ENABLE != 0U)
    TX1_write2buff(dat);
#else
    dat = dat;
#endif
}

#if (GPS_DIAG_LOG_ENABLE != 0U)
static void GPS_DiagLogPoll(void)
{
    u32 now_ms;

    now_ms = Task_GetTickMs();
    if ((g_gps_diag_logged_once == 0U) ||
        ((now_ms - g_gps_diag_last_ms) >= GPS_DIAG_LOG_MS)) {
        g_gps_diag_logged_once = 1U;
        g_gps_diag_last_ms = now_ms;
        LOGI("GPS",
             "diag uart init=%u rx_cnt=%u read=%u fifo=%u bytes=%lu",
             (u16)g_gps_initialized,
             (u16)COM2.RX_Cnt,
             (u16)g_gps_uart_read_index,
             (u16)g_gps_fifo_count,
             (u32)g_gps_diag_rx_bytes);
        LOGI("GPS",
             "diag parse ok=%u chk=%u parse=%u uovf=%u fovf=%u sovf=%u",
             (u16)g_gps_state.sentence_ok_count,
             (u16)g_gps_state.checksum_error_count,
             (u16)g_gps_state.parse_error_count,
             (u16)g_gps_state.uart_overflow_count,
             (u16)g_gps_state.fifo_overflow_count,
             (u16)g_gps_state.sentence_overflow_count);
        LOGI("GPS",
             "diag state fix=%u sat=%u view=%u seq=%lu",
             (u16)g_gps_state.fix_valid,
             (u16)g_gps_state.satellites_used,
             (u16)g_gps_state.satellites_view,
             (u32)g_gps_state.update_sequence);
    }
}

static void GPS_DiagSentenceSummary(const char *sentence, char *out, u8 out_len)
{
    u8 i;
    char ch;

    if ((out == 0) || (out_len == 0U)) {
        return;
    }
    if (sentence == 0) {
        out[0] = 0;
        return;
    }

    i = 0U;
    while (i < (u8)(out_len - 1U)) {
        ch = sentence[i];
        if ((ch == 0) || (ch == '\r') || (ch == '\n')) {
            break;
        }
        out[i] = ch;
        i++;
    }
    out[i] = 0;
}

static void GPS_DiagLogSentence(const char *sentence, u8 result)
{
    char type0;
    char type1;
    char type2;
    char summary[72];

    if ((sentence == 0) || (sentence[0] != '$') ||
        (sentence[3] == 0) || (sentence[4] == 0) || (sentence[5] == 0)) {
        return;
    }

    type0 = sentence[3];
    type1 = sentence[4];
    type2 = sentence[5];
    if ((result == GPS_DISPATCH_PARSE_ERROR) ||
        ((type0 == 'R') && (type1 == 'M') && (type2 == 'C')) ||
        ((type0 == 'G') && (type1 == 'G') && (type2 == 'A'))) {
        GPS_DiagSentenceSummary(sentence, summary, (u8)sizeof(summary));
        LOGI("GPS",
             "sentence %c%c%c result=%u text=%s",
             type0,
             type1,
             type2,
             (u16)result,
             summary);
    }
}

static void GPS_DiagLogParseFail(char *sentence, const char *reason)
{
    char summary[72];

    if (sentence == 0) {
        LOGW("GPS", "parse fail reason=%s", reason);
    } else {
        GPS_DiagSentenceSummary(sentence, summary, (u8)sizeof(summary));
        LOGW("GPS", "parse fail reason=%s text=%s", reason, summary);
    }
}
#endif

static u8 GPS_FifoPush(u8 dat)
{
    if (g_gps_fifo_count >= (u16)GPS_UART_FIFO_SIZE) {
        g_gps_state.fifo_overflow_count++;
        return 1;
    }

    g_gps_fifo[g_gps_fifo_head] = dat;
    g_gps_fifo_head++;
    if (g_gps_fifo_head >= (u16)GPS_UART_FIFO_SIZE) {
        g_gps_fifo_head = 0;
    }
    g_gps_fifo_count++;
    return 0;
}

static u8 GPS_FifoPop(u8 *dat)
{
    if ((dat == 0) || (g_gps_fifo_count == 0)) {
        return 1;
    }

    *dat = g_gps_fifo[g_gps_fifo_tail];
    g_gps_fifo_tail++;
    if (g_gps_fifo_tail >= (u16)GPS_UART_FIFO_SIZE) {
        g_gps_fifo_tail = 0;
    }
    g_gps_fifo_count--;
    return 0;
}

static void GPS_DrainUart2Buffer(void)
{
    u8 write_index;

    if (!g_gps_initialized) {
        return;
    }

    write_index = COM2.RX_Cnt;
    if (write_index == g_gps_uart_read_index) {
        return;
    }

    if (write_index < g_gps_uart_read_index) {
        while (g_gps_uart_read_index < COM_RX2_Lenth) {
            GPS_RawEchoByte(RX2_Buffer[g_gps_uart_read_index]);
            GPS_FifoPush(RX2_Buffer[g_gps_uart_read_index]);
#if (GPS_DIAG_LOG_ENABLE != 0U)
            g_gps_diag_rx_bytes++;
#endif
            g_gps_uart_read_index++;
        }
        g_gps_uart_read_index = 0;
        g_gps_state.uart_overflow_count++;
    }

    while (g_gps_uart_read_index != write_index) {
        GPS_RawEchoByte(RX2_Buffer[g_gps_uart_read_index]);
        GPS_FifoPush(RX2_Buffer[g_gps_uart_read_index]);
#if (GPS_DIAG_LOG_ENABLE != 0U)
        g_gps_diag_rx_bytes++;
#endif
        g_gps_uart_read_index++;
    }
}

static void GPS_ParseByte(u8 dat)
{
    if (dat == '$') {
        g_gps_capture_active = 1;
        g_gps_sentence_len = 0;
        g_gps_sentence_buf[g_gps_sentence_len++] = '$';
        g_gps_sentence_buf[g_gps_sentence_len] = 0;
        return;
    }

    if (!g_gps_capture_active) {
        return;
    }

    if (g_gps_sentence_len >= (u8)(GPS_SENTENCE_BUFFER_SIZE - 1U)) {
        g_gps_capture_active = 0;
        g_gps_sentence_len = 0;
        g_gps_state.sentence_overflow_count++;
        return;
    }

    g_gps_sentence_buf[g_gps_sentence_len++] = (char)dat;
    g_gps_sentence_buf[g_gps_sentence_len] = 0;

    if (dat == '\n') {
        GPS_ProcessSentence(g_gps_sentence_buf);
        g_gps_capture_active = 0;
        g_gps_sentence_len = 0;
    }
}

static void GPS_ProcessSentence(char *sentence)
{
    char *scan;
    char *star;
    char *payload;
    char *fields[GPS_MAX_FIELDS];
    u8 checksum_calc;
    u8 checksum_recv;
    u8 field_count;
    u8 result;
#if (GPS_DIAG_LOG_ENABLE != 0U)
    char diag_sentence[72];
#endif

    if ((sentence == 0) || (sentence[0] != '$')) {
        return;
    }

    checksum_calc = 0;
    star = 0;
    scan = sentence + 1;
    while (*scan != 0) {
        if (*scan == '*') {
            star = scan;
            break;
        }
        if ((*scan == '\r') || (*scan == '\n')) {
            g_gps_state.parse_error_count++;
            return;
        }
        checksum_calc ^= (u8)(*scan);
        scan++;
    }

    if ((star == 0) || (GPS_ParseHexByte(star + 1, &checksum_recv) == 0)) {
        g_gps_state.checksum_error_count++;
#if (GPS_DIAG_LOG_ENABLE != 0U)
        GPS_DiagLogParseFail(sentence, "checksum-missing");
#endif
        return;
    }
    if (checksum_recv != checksum_calc) {
        g_gps_state.checksum_error_count++;
#if (GPS_DIAG_LOG_ENABLE != 0U)
        GPS_DiagLogParseFail(sentence, "checksum-bad");
#endif
        return;
    }

#if (GPS_DIAG_LOG_ENABLE != 0U)
    GPS_DiagSentenceSummary(sentence, diag_sentence, (u8)sizeof(diag_sentence));
#endif
    *star = 0;
    payload = sentence + 1;
    field_count = GPS_SplitFields(payload, fields, GPS_MAX_FIELDS);
    if ((field_count == 0) || (fields[0] == 0) ||
        (fields[0][0] == 0) || (fields[0][1] == 0) ||
        (fields[0][2] == 0) || (fields[0][3] == 0) ||
        (fields[0][4] == 0)) {
        g_gps_state.parse_error_count++;
        return;
    }

    result = GPS_DispatchSentence(fields, field_count);
#if (GPS_DIAG_LOG_ENABLE != 0U)
    GPS_DiagLogSentence(diag_sentence, result);
#endif
    if (result == GPS_DISPATCH_OK) {
        g_gps_state.sentence_ok_count++;
    } else if (result == GPS_DISPATCH_PARSE_ERROR) {
        g_gps_state.parse_error_count++;
    }
}

static u8 GPS_SplitFields(char *payload, char **fields, u8 max_fields)
{
    u8 count;
    char *scan;

    if ((payload == 0) || (fields == 0) || (max_fields == 0)) {
        return 0;
    }

    count = 0;
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

static u8 GPS_DispatchSentence(char **fields, u8 field_count)
{
    char *type;
    u8 parse_ok;

    if ((fields == 0) || (field_count == 0) || (fields[0] == 0)) {
        return GPS_DISPATCH_PARSE_ERROR;
    }

    type = fields[0] + 2;

    if ((type[0] == 'R') && (type[1] == 'M') && (type[2] == 'C') && (type[3] == 0)) {
        parse_ok = GPS_ParseRMC(fields, field_count);
        if (parse_ok) {
            GPS_UpdateTalker(fields[0]);
            return GPS_DISPATCH_OK;
        }
        return GPS_DISPATCH_PARSE_ERROR;
    }
    if ((type[0] == 'G') && (type[1] == 'G') && (type[2] == 'A') && (type[3] == 0)) {
        parse_ok = GPS_ParseGGA(fields, field_count);
        if (parse_ok) {
            GPS_UpdateTalker(fields[0]);
            return GPS_DISPATCH_OK;
        }
        return GPS_DISPATCH_PARSE_ERROR;
    }
    if ((type[0] == 'G') && (type[1] == 'S') && (type[2] == 'A') && (type[3] == 0)) {
        parse_ok = GPS_ParseGSA(fields, field_count);
        if (parse_ok) {
            GPS_UpdateTalker(fields[0]);
            return GPS_DISPATCH_OK;
        }
        return GPS_DISPATCH_PARSE_ERROR;
    }
    if ((type[0] == 'G') && (type[1] == 'S') && (type[2] == 'V') && (type[3] == 0)) {
        parse_ok = GPS_ParseGSV(fields, field_count);
        if (parse_ok) {
            GPS_UpdateTalker(fields[0]);
            return GPS_DISPATCH_OK;
        }
        return GPS_DISPATCH_PARSE_ERROR;
    }
    if ((type[0] == 'V') && (type[1] == 'T') && (type[2] == 'G') && (type[3] == 0)) {
        parse_ok = GPS_ParseVTG(fields, field_count);
        if (parse_ok) {
            GPS_UpdateTalker(fields[0]);
            return GPS_DISPATCH_OK;
        }
        return GPS_DISPATCH_PARSE_ERROR;
    }

    return GPS_DISPATCH_IGNORE;
}

static u8 GPS_ParseRMC(char **fields, u8 field_count)
{
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

    if (field_count < 10U) {
        return 0;
    }
    if ((!GPS_FieldPresent(fields[1])) ||
        (!GPS_FieldPresent(fields[2]))) {
        return 0;
    }
    if (GPS_ParseUtc(fields[1], &hour, &minute, &second, &msec) == 0) {
        return 0;
    }
    day = 0U;
    month = 0U;
    year = 0U;
    if (GPS_FieldPresent(fields[9])) {
        if (GPS_ParseDate(fields[9], &day, &month, &year) == 0) {
            return 0;
        }
    }

    status = fields[2][0];
    if ((status != 'A') && (status != 'V')) {
        return 0;
    }
    fix_valid = (status == 'A') ? 1U : 0U;

    if (fix_valid) {
        if ((!GPS_FieldPresent(fields[3])) ||
            (!GPS_FieldPresent(fields[4])) ||
            (!GPS_FieldPresent(fields[5])) ||
            (!GPS_FieldPresent(fields[6]))) {
            return 0;
        }

        hemi_ns = fields[4][0];
        hemi_ew = fields[6][0];
        if ((GPS_ParseCoordinate1e7(fields[3], hemi_ns, &lat_deg1e7) == 0) ||
            (GPS_ParseCoordinate1e7(fields[5], hemi_ew, &lon_deg1e7) == 0)) {
            return 0;
        }
        if ((GPS_ParseLegacyCoordParts(fields[3], 4U, &legacy_lat1, &legacy_lat2) == 0) ||
            (GPS_ParseLegacyCoordParts(fields[5], 5U, &legacy_lon1, &legacy_lon2) == 0)) {
            return 0;
        }
    }

    has_speed = 0U;
    if (GPS_FieldPresent(fields[7])) {
        if (GPS_ParseDecimalScaled(fields[7], 2U, &speed_knots_x100) == 0) {
            return 0;
        }
        has_speed = 1U;
    }

    has_course = 0U;
    if (GPS_FieldPresent(fields[8])) {
        if (GPS_ParseDecimalScaled(fields[8], 2U, &course_deg_x100) == 0) {
            return 0;
        }
        has_course = 1U;
    }

    if (GPS_IsNewRmcTimestamp(hour, minute, second, msec, day, month, year)) {
        g_gps_state.update_sequence++;
    }

    g_gps_state.rmc_status = (u8)status;
    g_gps_state.fix_valid = fix_valid;
    g_gps_state.utc_hour = hour;
    g_gps_state.utc_minute = minute;
    g_gps_state.utc_second = second;
    g_gps_state.utc_millisecond = msec;
    g_gps_state.date_day = day;
    g_gps_state.date_month = month;
    g_gps_state.date_year = year;

    if (fix_valid) {
        g_gps_state.lat_deg1e7 = lat_deg1e7;
        g_gps_state.lon_deg1e7 = lon_deg1e7;
        g_gps_state.legacy_coord_valid = 1U;
        g_gps_state.legacy_lat_dir = (u8)hemi_ns;
        g_gps_state.legacy_lon_dir = (u8)hemi_ew;
        g_gps_state.legacy_lat1 = legacy_lat1;
        g_gps_state.legacy_lat2 = legacy_lat2;
        g_gps_state.legacy_lon1 = legacy_lon1;
        g_gps_state.legacy_lon2 = legacy_lon2;
#if (GPS_DIAG_LOG_ENABLE != 0U)
        LOGI("GPS", "rmc valid lat=%ld lon=%ld",
             (long)lat_deg1e7,
             (long)lon_deg1e7);
        LOGI("GPS",
             "rmc oldfmt ew=%c lon1=%u lon2=%u ns=%c lat1=%u lat2=%u",
             hemi_ew,
             (u16)legacy_lon1,
             (u16)legacy_lon2,
             hemi_ns,
             (u16)legacy_lat1,
             (u16)legacy_lat2);
        LOGI("GPS", "rmc move spd=%ld course=%ld time=%u:%u:%u",
             (long)(has_speed ? speed_knots_x100 : 0L),
             (long)(has_course ? course_deg_x100 : 0L),
             (u16)hour,
             (u16)minute,
             (u16)second);
#endif
    } else {
#if (GPS_DIAG_LOG_ENABLE != 0U)
        LOGW("GPS",
             "rmc void status=%c time=%u:%u:%u date=%u-%u-%u",
             status,
             (u16)hour,
             (u16)minute,
             (u16)second,
             (u16)day,
             (u16)month,
             (u16)year);
#endif
        g_gps_state.legacy_coord_valid = 0U;
    }

    if (has_speed) {
        g_gps_state.speed_knots_x100 = GPS_ToU32NonNegative(speed_knots_x100);
        g_gps_state.speed_kmh_x100 = GPS_KnotsX100ToKmhX100(g_gps_state.speed_knots_x100);
    }
    if (has_course) {
        g_gps_state.course_deg_x100 = GPS_ToU16NonNegative(course_deg_x100);
    }

    return 1;
}

static u8 GPS_ParseGGA(char **fields, u8 field_count)
{
    u8 quality;
    u8 satellites;
    int32 hdop_x100;
    int32 altitude_cm;
    u8 has_satellites;
    u8 has_hdop;
    u8 has_altitude;

    if (field_count < 10U) {
        return 0;
    }
    if (!GPS_FieldPresent(fields[6])) {
        return 0;
    }
    if (GPS_ParseU8(fields[6], &quality) == 0) {
        return 0;
    }

    has_satellites = 0U;
    if (GPS_FieldPresent(fields[7])) {
        if (GPS_ParseU8(fields[7], &satellites) == 0) {
            return 0;
        }
        has_satellites = 1U;
    }

    has_hdop = 0U;
    if (GPS_FieldPresent(fields[8])) {
        if (GPS_ParseDecimalScaled(fields[8], 2U, &hdop_x100) == 0) {
            return 0;
        }
        has_hdop = 1U;
    }

    has_altitude = 0U;
    if (GPS_FieldPresent(fields[9])) {
        if (GPS_ParseDecimalScaled(fields[9], 2U, &altitude_cm) == 0) {
            return 0;
        }
        has_altitude = 1U;
    }

    g_gps_state.fix_quality = quality;
    g_gps_state.satellites_used_gsa = 0U;
    if (quality == 0U) {
        g_gps_state.fix_valid = 0U;
    }
    if (has_satellites) {
        g_gps_state.satellites_used = satellites;
    }
    if (has_hdop) {
        g_gps_state.hdop_x100 = GPS_ToU16NonNegative(hdop_x100);
    }
    if (has_altitude) {
        g_gps_state.altitude_cm = altitude_cm;
    }

#if (GPS_DIAG_LOG_ENABLE != 0U)
    LOGI("GPS",
         "gga quality=%u sat=%u hdop=%ld alt_cm=%ld",
         (u16)quality,
         (u16)(has_satellites ? satellites : 0U),
         (long)(has_hdop ? hdop_x100 : 0L),
         (long)(has_altitude ? altitude_cm : 0L));
#endif

    return 1;
}

static u8 GPS_ParseGSA(char **fields, u8 field_count)
{
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

    if (field_count < 18U) {
        return 0;
    }
    if (!GPS_FieldPresent(fields[2])) {
        return 0;
    }
    if (GPS_ParseU8(fields[2], &fix_mode) == 0) {
        return 0;
    }

    prn_count = 0U;
    prn_complete = 1U;
    for (prn_index = 3U; prn_index <= 14U; prn_index++) {
        if ((prn_index < field_count) && GPS_FieldPresent(fields[prn_index])) {
            prn_count++;
        } else {
            prn_complete = 0U;
        }
    }

    has_pdop = 0U;
    if (GPS_FieldPresent(fields[15])) {
        if (GPS_ParseDecimalScaled(fields[15], 2U, &pdop_x100) == 0) {
            return 0;
        }
        has_pdop = 1U;
    }

    has_hdop = 0U;
    if (GPS_FieldPresent(fields[16])) {
        if (GPS_ParseDecimalScaled(fields[16], 2U, &hdop_x100) == 0) {
            return 0;
        }
        has_hdop = 1U;
    }

    has_vdop = 0U;
    if (GPS_FieldPresent(fields[17])) {
        if (GPS_ParseDecimalScaled(fields[17], 2U, &vdop_x100) == 0) {
            return 0;
        }
        has_vdop = 1U;
    }

    g_gps_state.fix_mode = fix_mode;
    g_gps_state.satellites_used_gsa = (prn_complete != 0U) ? prn_count : 0U;
    if (has_pdop) {
        g_gps_state.pdop_x100 = GPS_ToU16NonNegative(pdop_x100);
    }
    if (has_hdop) {
        g_gps_state.hdop_x100 = GPS_ToU16NonNegative(hdop_x100);
    }
    if (has_vdop) {
        g_gps_state.vdop_x100 = GPS_ToU16NonNegative(vdop_x100);
    }

    return 1;
}

static u8 GPS_ParseGSV(char **fields, u8 field_count)
{
    u8 sentence_index;
    u8 satellites_view;
    u8 max_snr;
    u8 snr;
    u8 field_index;

    if (field_count < 4U) {
        return 0;
    }
    if ((!GPS_FieldPresent(fields[2])) ||
        (!GPS_FieldPresent(fields[3]))) {
        return 0;
    }
    if ((GPS_ParseU8(fields[2], &sentence_index) == 0) ||
        (GPS_ParseU8(fields[3], &satellites_view) == 0) ||
        (sentence_index == 0U)) {
        return 0;
    }

    max_snr = g_gps_state.max_snr;
    if (sentence_index == 1U) {
        max_snr = 0U;
    }

    field_index = 7U;
    while (field_index < field_count) {
        if (GPS_FieldPresent(fields[field_index])) {
            if (GPS_ParseU8(fields[field_index], &snr) == 0) {
                return 0;
            }
            if (snr > max_snr) {
                max_snr = snr;
            }
        }
        field_index = (u8)(field_index + 4U);
    }

    g_gps_state.satellites_view = satellites_view;
    g_gps_state.max_snr = max_snr;
    return 1;
}

static u8 GPS_ParseVTG(char **fields, u8 field_count)
{
    int32 course_deg_x100;
    int32 speed_knots_x100;
    int32 speed_kmh_x100;
    u8 has_course;
    u8 has_speed_knots;
    u8 has_speed_kmh;

    if (field_count < 8U) {
        return 0;
    }

    has_course = 0U;
    if (GPS_FieldPresent(fields[1])) {
        if (GPS_ParseDecimalScaled(fields[1], 2U, &course_deg_x100) == 0) {
            return 0;
        }
        has_course = 1U;
    }

    has_speed_knots = 0U;
    if (GPS_FieldPresent(fields[5])) {
        if (GPS_ParseDecimalScaled(fields[5], 2U, &speed_knots_x100) == 0) {
            return 0;
        }
        has_speed_knots = 1U;
    }

    has_speed_kmh = 0U;
    if (GPS_FieldPresent(fields[7])) {
        if (GPS_ParseDecimalScaled(fields[7], 2U, &speed_kmh_x100) == 0) {
            return 0;
        }
        has_speed_kmh = 1U;
    }

    if ((!has_course) && (!has_speed_knots) && (!has_speed_kmh)) {
        return 0;
    }

    if (has_course) {
        g_gps_state.course_deg_x100 = GPS_ToU16NonNegative(course_deg_x100);
    }
    if (has_speed_knots) {
        g_gps_state.speed_knots_x100 = GPS_ToU32NonNegative(speed_knots_x100);
        if (!has_speed_kmh) {
            g_gps_state.speed_kmh_x100 = GPS_KnotsX100ToKmhX100(g_gps_state.speed_knots_x100);
        }
    }
    if (has_speed_kmh) {
        g_gps_state.speed_kmh_x100 = GPS_ToU32NonNegative(speed_kmh_x100);
    }

    return 1;
}

static void GPS_UpdateTalker(const char *sentence_id)
{
    if ((sentence_id == 0) || (sentence_id[0] == 0) || (sentence_id[1] == 0)) {
        g_gps_state.talker[0] = '-';
        g_gps_state.talker[1] = '-';
        g_gps_state.talker[2] = 0;
        g_gps_state.talker_id = GPS_TALKER_UNKNOWN;
        return;
    }

    g_gps_state.talker[0] = sentence_id[0];
    g_gps_state.talker[1] = sentence_id[1];
    g_gps_state.talker[2] = 0;
    g_gps_state.talker_id = GPS_ParseTalkerId(sentence_id);
}

static u8 GPS_ParseTalkerId(const char *sentence_id)
{
    if ((sentence_id[0] == 'G') && (sentence_id[1] == 'P')) {
        return GPS_TALKER_GP;
    }
    if ((sentence_id[0] == 'B') && (sentence_id[1] == 'D')) {
        return GPS_TALKER_BD;
    }
    if ((sentence_id[0] == 'G') && (sentence_id[1] == 'N')) {
        return GPS_TALKER_GN;
    }
    return GPS_TALKER_UNKNOWN;
}

static u8 GPS_FieldPresent(const char *text)
{
    return (u8)((text != 0) && (text[0] != 0));
}

static u8 GPS_HexToNibble(char ch, u8 *value)
{
    if ((value == 0) || (ch == 0)) {
        return 0;
    }

    if ((ch >= '0') && (ch <= '9')) {
        *value = (u8)(ch - '0');
        return 1;
    }
    if ((ch >= 'A') && (ch <= 'F')) {
        *value = (u8)(ch - 'A' + 10);
        return 1;
    }
    if ((ch >= 'a') && (ch <= 'f')) {
        *value = (u8)(ch - 'a' + 10);
        return 1;
    }
    return 0;
}

static u8 GPS_ParseHexByte(const char *text, u8 *value)
{
    u8 high;
    u8 low;

    if ((text == 0) || (value == 0)) {
        return 0;
    }
    if ((GPS_HexToNibble(text[0], &high) == 0) ||
        (GPS_HexToNibble(text[1], &low) == 0)) {
        return 0;
    }

    *value = (u8)((high << 4) | low);
    return 1;
}

static u8 GPS_ParseU32(const char *text, u32 *value)
{
    u32 result;
    u8 digit_found;
    char ch;

    if ((text == 0) || (value == 0) || (text[0] == 0)) {
        return 0;
    }

    result = 0UL;
    digit_found = 0U;
    while (*text != 0) {
        ch = *text;
        if ((ch < '0') || (ch > '9')) {
            return 0;
        }
        digit_found = 1U;
        if ((result > 429496729UL) ||
            ((result == 429496729UL) && ((u8)(ch - '0') > 5U))) {
            return 0;
        }
        result = result * 10UL + (u32)(ch - '0');
        text++;
    }

    if (!digit_found) {
        return 0;
    }

    *value = result;
    return 1;
}

static u8 GPS_ParseU8(const char *text, u8 *value)
{
    u32 temp;

    if ((GPS_ParseU32(text, &temp) == 0) || (temp > 255UL)) {
        return 0;
    }

    *value = (u8)temp;
    return 1;
}

static u8 GPS_ParseDecimalScaled(const char *text, u8 frac_digits, int32 *value)
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
        return 0;
    }
    if (frac_digits > 4U) {
        return 0;
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
            if (!dot_seen) {
                if ((integer_part > 429496729UL) ||
                    ((integer_part == 429496729UL) && (digit > 5U))) {
                    return 0;
                }
                integer_part = integer_part * 10UL + (u32)digit;
            } else {
                if (frac_count < 4U) {
                    fractional_part = fractional_part * 10UL + (u32)digit;
                    frac_count++;
                }
            }
        } else if ((ch == '.') && (!dot_seen)) {
            dot_seen = 1U;
        } else {
            return 0;
        }
        text++;
    }

    if (!digits_found) {
        return 0;
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
        return 0;
    }
    combined_value = integer_part * scale_base + fractional_part;
    if (combined_value > 2147483647UL) {
        return 0;
    }

    if (negative) {
        *value = -((int32)combined_value);
    } else {
        *value = (int32)combined_value;
    }

    return 1;
}

static u8 GPS_ParseUtc(const char *text, u8 *hour, u8 *minute, u8 *second, u16 *msec)
{
    u32 whole_part;
    u32 frac_part;
    char frac_buf[5];
    u8 frac_len;
    const char *dot;

    if ((text == 0) || (hour == 0) || (minute == 0) || (second == 0) || (msec == 0)) {
        return 0;
    }
    if ((text[0] == 0) || (text[1] == 0) || (text[2] == 0) ||
        (text[3] == 0) || (text[4] == 0) || (text[5] == 0)) {
        return 0;
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
        if (GPS_ParseU32(whole_buf, &whole_part) == 0) {
            return 0;
        }
    }

    *hour = (u8)(whole_part / 10000UL);
    *minute = (u8)((whole_part / 100UL) % 100UL);
    *second = (u8)(whole_part % 100UL);

    if ((*hour > 23U) || (*minute > 59U) || (*second > 59U)) {
        return 0;
    }

    frac_part = 0UL;
    frac_len = 0U;
    if (*dot == '.') {
        dot++;
        while (*dot != 0) {
            if ((*dot < '0') || (*dot > '9')) {
                return 0;
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
    if (GPS_ParseU32(frac_buf, &frac_part) == 0) {
        return 0;
    }

    *msec = (u16)frac_part;
    return 1;
}

static u8 GPS_ParseDate(const char *text, u8 *day, u8 *month, u8 *year)
{
    char buf[3];

    if ((text == 0) || (day == 0) || (month == 0) || (year == 0)) {
        return 0;
    }
    if ((text[0] == 0) || (text[1] == 0) || (text[2] == 0) ||
        (text[3] == 0) || (text[4] == 0) || (text[5] == 0)) {
        return 0;
    }

    buf[2] = 0;
    buf[0] = text[0];
    buf[1] = text[1];
    if (GPS_ParseU8(buf, day) == 0) {
        return 0;
    }
    buf[0] = text[2];
    buf[1] = text[3];
    if (GPS_ParseU8(buf, month) == 0) {
        return 0;
    }
    buf[0] = text[4];
    buf[1] = text[5];
    if (GPS_ParseU8(buf, year) == 0) {
        return 0;
    }

    if ((*day == 0U) || (*day > 31U) || (*month == 0U) || (*month > 12U)) {
        return 0;
    }
    return 1;
}

static u8 GPS_ParseCoordinate1e7(const char *text, char hemi, int32 *value)
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
        return 0;
    }
    if ((hemi != 'N') && (hemi != 'S') && (hemi != 'E') && (hemi != 'W')) {
        return 0;
    }

    dot = text;
    while ((*dot != 0) && (*dot != '.')) {
        dot++;
    }

    expected_whole_len = (((hemi == 'N') || (hemi == 'S')) ? 4U : 5U);
    whole_len = (u8)(dot - text);
    if ((whole_len != expected_whole_len) || (whole_len >= (u8)sizeof(whole_buf))) {
        return 0;
    }
    {
        u8 i;
        for (i = 0; i < whole_len; i++) {
            whole_buf[i] = text[i];
        }
        whole_buf[whole_len] = 0;
    }
    if (GPS_ParseU32(whole_buf, &whole_part) == 0) {
        return 0;
    }

    frac_len = 0U;
    if (*dot == '.') {
        dot++;
        while (*dot != 0) {
            if ((*dot < '0') || (*dot > '9')) {
                return 0;
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
    if (GPS_ParseU32(frac_buf, &frac_part) == 0) {
        return 0;
    }

    degrees = whole_part / 100UL;
    minutes_scaled1e4 = (whole_part % 100UL) * 10000UL + frac_part;
    if (minutes_scaled1e4 >= 600000UL) {
        return 0;
    }
    if (((hemi == 'N') || (hemi == 'S')) &&
        ((degrees > 90UL) || ((degrees == 90UL) && (minutes_scaled1e4 != 0UL)))) {
        return 0;
    }
    if (((hemi == 'E') || (hemi == 'W')) &&
        ((degrees > 180UL) || ((degrees == 180UL) && (minutes_scaled1e4 != 0UL)))) {
        return 0;
    }
    deg1e7 = degrees * 10000000UL + (minutes_scaled1e4 * 100UL + 3UL) / 6UL;

    if ((hemi == 'S') || (hemi == 'W')) {
        *value = -((int32)deg1e7);
    } else {
        *value = (int32)deg1e7;
    }
    return 1;
}

static u8 GPS_ParseLegacyCoordParts(const char *text, u8 whole_len, u16 *coord1, u16 *coord2)
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
        return 0;
    }

    dot = text;
    while ((*dot != 0) && (*dot != '.')) {
        dot++;
    }
    if ((u8)(dot - text) != whole_len) {
        return 0;
    }

    for (i = 0U; i < whole_len; i++) {
        if ((text[i] < '0') || (text[i] > '9')) {
            return 0;
        }
        whole_buf[i] = text[i];
    }
    whole_buf[whole_len] = 0;
    if (GPS_ParseU32(whole_buf, &whole_part) == 0) {
        return 0;
    }

    frac_len = 0U;
    if (*dot == '.') {
        dot++;
        while (*dot != 0) {
            if ((*dot < '0') || (*dot > '9')) {
                return 0;
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
    if (GPS_ParseU32(frac_buf, &frac_part) == 0) {
        return 0;
    }

    *coord1 = (u16)whole_part;
    *coord2 = (u16)frac_part;
    return 1;
}

static u8 GPS_IsNewRmcTimestamp(u8 hour, u8 minute, u8 second, u16 msec,
                                u8 day, u8 month, u8 year)
{
    if ((g_gps_state.utc_hour != hour) ||
        (g_gps_state.utc_minute != minute) ||
        (g_gps_state.utc_second != second) ||
        (g_gps_state.utc_millisecond != msec) ||
        (g_gps_state.date_day != day) ||
        (g_gps_state.date_month != month) ||
        (g_gps_state.date_year != year)) {
        return 1;
    }
    return 0;
}

static u32 GPS_KnotsX100ToKmhX100(u32 speed_knots_x100)
{
    if (speed_knots_x100 > ((0xFFFFFFFFUL - 500UL) / 1852UL)) {
        return 0xFFFFFFFFUL;
    }
    return (speed_knots_x100 * 1852UL + 500UL) / 1000UL;
}

static u16 GPS_ToU16NonNegative(int32 value)
{
    if (value <= 0) {
        return 0U;
    }
    if (value > 65535L) {
        return 65535U;
    }
    return (u16)value;
}

static u32 GPS_ToU32NonNegative(int32 value)
{
    if (value <= 0L) {
        return 0UL;
    }
    return (u32)value;
}

s8 GPS_Init(void)
{
    COMx_InitDefine uart_init;

    GPS_ClearState();
    GPS_ClearParser();

    uart_init.UART_Mode = UART_8bit_BRTx;
    uart_init.UART_BRT_Use = BRT_Timer2;
    uart_init.UART_BaudRate = GPS_BAUDRATE;
    uart_init.Morecommunicate = DISABLE;
    uart_init.UART_RxEnable = ENABLE;
    uart_init.BaudRateDouble = DISABLE;

    if (UART_Configuration(UART2, &uart_init) != SUCCESS) {
        LOGE("GPS", "UART2 init fail baud=%lu", GPS_BAUDRATE);
        return FAIL;
    }
    if (NVIC_UART2_Init(ENABLE, Priority_1) != SUCCESS) {
        LOGE("GPS", "UART2 NVIC init fail");
        return FAIL;
    }

    g_gps_initialized = 1;
    LOGI("GPS", "init uart=2 route=P1.0/P1.1 baud=%lu", GPS_BAUDRATE);
#if (GPS_RAW_ECHO_ENABLE != 0U)
    LOGW("GPS", "raw echo uart2->uart1 enabled for baud test");
#endif
    return SUCCESS;
}

void GPS_Reset(void)
{
    GPS_ClearState();
    GPS_ClearParser();
    if (g_gps_initialized) {
        g_gps_uart_read_index = COM2.RX_Cnt;
    }
}

void GPS_Poll(void)
{
    u8 dat;

    if (!g_gps_initialized) {
        return;
    }

    GPS_DrainUart2Buffer();
    while (GPS_FifoPop(&dat) == 0) {
        GPS_ParseByte(dat);
    }
#if (GPS_DIAG_LOG_ENABLE != 0U)
    GPS_DiagLogPoll();
#endif
}

const GPS_State_t *GPS_GetState(void)
{
    return &g_gps_state;
}
