#include "ship_protocol_internal.h"
#include "board_gps.h"
#include "board_wireless.h"
#include "logger.h"

u8 ship_protocol_get_autodrive_status(void)
{
    return AutoDrive_InActive();
}

static void ship_protocol_build_gps_payload(u8 *payload)
{
    const board_gps_state_t *gps;
    ship_protocol_power_sample_t power;
    u16 angle;
    u16 lon1;
    u16 lon2;
    u16 lat1;
    u16 lat2;
    u8 sat;

    gps = board_gps_get_state();
    sat = 0U;
    lon1 = 0U;
    lon2 = 0U;
    lat1 = 0U;
    lat2 = 0U;
    if (gps != 0) {
        sat = (gps->satellites_used_gsa != 0U) ?
              gps->satellites_used_gsa : gps->satellites_used;
        if (sat > 24U) {
            sat = 24U;
        }
        angle = (u16)((gps->course_deg_x100 / 100U) % 360U);
        if (gps->legacy_coord_valid != 0U) {
            lon1 = gps->legacy_lon1;
            lon2 = gps->legacy_lon2;
            lat1 = gps->legacy_lat1;
            lat2 = gps->legacy_lat2;
        } else {
            ship_protocol_to_legacy_nmea_coord(ship_protocol_abs_int32(gps->lon_deg1e7),
                                               &lon1,
                                               &lon2);
            ship_protocol_to_legacy_nmea_coord(ship_protocol_abs_int32(gps->lat_deg1e7),
                                               &lat1,
                                               &lat2);
        }
        payload[1] = (u8)(angle >> 8);
        payload[2] = (u8)(angle & 0x00FFU);
    } else {
        payload[1] = 0U;
        payload[2] = 0U;
    }

    power = ship_protocol_rt.power_sample;
    power.report = ship_protocol_rt.power_level;
    payload[0] = sat;
    /* 旧手柄解析固定使用 E/W 标记，这里保持历史线格式。 */
    payload[3] = (u8)'E';
    ship_protocol_put_u16_be(&payload[4], lon1);
    ship_protocol_put_u16_be(&payload[6], lon2);
    payload[8] = (u8)'W';
    ship_protocol_put_u16_be(&payload[9], lat1);
    ship_protocol_put_u16_be(&payload[11], lat2);
    payload[13] = ship_protocol_rt.power_level;
    payload[14] = ship_protocol_get_autodrive_status();
    ship_protocol_log_power_sample(&power, 0U);
}

int8 ship_protocol_send_frame(u8 channel, u8 cmd, const u8 *payload, u8 payload_len)
{
    u8 body_len;
    u8 idx;
    u8 i;
    int8 ret;

    if (((payload_len > 0U) && (payload == 0)) ||
        ((u16)payload_len + 5U > SHIP_PROTO_MAX_FRAME_LEN)) {
        return BOARD_WIRELESS_ERR_PARAM;
    }
    body_len = (u8)(2U + payload_len);
    idx = 0U;
    ship_protocol_tx_frame[idx++] = SHIP_PROTO_HEAD;
    ship_protocol_tx_frame[idx++] = body_len;
    ship_protocol_tx_frame[idx++] = cmd;
    for (i = 0U; i < payload_len; i++) {
        ship_protocol_tx_frame[idx++] = payload[i];
    }
    ship_protocol_tx_frame[idx] =
        ship_protocol_xor(&ship_protocol_tx_frame[1], (u8)(2U + payload_len));
    idx++;
    ship_protocol_tx_frame[idx++] = SHIP_PROTO_TAIL;
    ret = board_wireless_send_on_channel(channel, ship_protocol_tx_frame, idx);
    if (ret == BOARD_WIRELESS_OK) {
#if (SHIP_PROTOCOL_VERBOSE_LOG_ENABLE != 0U)
        LOGI(SHIP_TAG, "tx cmd=%02X ch=%u len=%u",
             (u16)cmd,
             (u16)channel,
             (u16)payload_len);
#endif
    } else {
        LOGE(SHIP_TAG, "tx cmd=%02X fail rc=%d", (u16)cmd, ret);
    }
    return ret;
}

void ship_protocol_send_gps_once(void)
{
    ship_protocol_build_gps_payload(ship_protocol_gps_payload);
    (void)ship_protocol_send_frame(ship_protocol_rt.rf_channel[0],
                                   SHIP_CMD_GPS_REPORT,
                                   ship_protocol_gps_payload,
                                   SHIP_GPS_REPORT_PAYLOAD_LEN);
    (void)board_wireless_set_channel(ship_protocol_rt.rf_channel[0]);
}

static void ship_protocol_write_point_legacy(u8 *dst, const AutoDrive_PointRaw_t *point)
{
    if ((dst == 0) || (point == 0)) {
        return;
    }
    dst[0] = point->lon_ew;
    ship_protocol_put_u16_be(&dst[1], point->lon_whole);
    ship_protocol_put_u16_be(&dst[3], point->lon_frac);
    dst[5] = point->lat_ns;
    ship_protocol_put_u16_be(&dst[6], point->lat_whole);
    ship_protocol_put_u16_be(&dst[8], point->lat_frac);
}

static void ship_protocol_send_autodrive_diag_once(u8 log_this_tx)
{
    AutoDrive_DebugSnapshot_t snapshot;
    u8 idx;

    AutoDrive_GetDebugSnapshot(&snapshot);
    idx = 0U;
    ship_protocol_diag_payload[idx++] = 0x01U;
    ship_protocol_diag_payload[idx++] = snapshot.state;
    ship_protocol_diag_payload[idx++] = snapshot.mode;
    ship_protocol_diag_payload[idx++] = snapshot.auto_ret_onoff;
    ship_protocol_diag_payload[idx++] = snapshot.fail_flag;
    ship_protocol_diag_payload[idx++] = snapshot.last_reason;
    ship_protocol_diag_payload[idx++] = snapshot.gps_ready;
    ship_protocol_diag_payload[idx++] = snapshot.sat_count;
    ship_protocol_diag_payload[idx++] = snapshot.can_activate_target;
    ship_protocol_diag_payload[idx++] = 0U;
    ship_protocol_put_u16_be(&ship_protocol_diag_payload[idx], snapshot.distance_to_target_m);
    idx = (u8)(idx + 2U);
    ship_protocol_put_u16_be(&ship_protocol_diag_payload[idx], snapshot.current_heading_deg);
    idx = (u8)(idx + 2U);
    ship_protocol_put_u16_be(&ship_protocol_diag_payload[idx], snapshot.target_heading_deg);
    idx = (u8)(idx + 2U);
    ship_protocol_write_point_legacy(&ship_protocol_diag_payload[idx], &snapshot.current_point);
    idx = (u8)(idx + AUTODRIVE_LEGACY_POINT_WIRE_LEN);
    ship_protocol_write_point_legacy(&ship_protocol_diag_payload[idx], &snapshot.target_point);
    idx = (u8)(idx + AUTODRIVE_LEGACY_POINT_WIRE_LEN);
    if (idx != SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN) {
        LOGE(SHIP_TAG, "diag len bad=%u", (u16)idx);
        return;
    }
    if (log_this_tx != 0U) {
        LOGI(SHIP_TAG, "tx16 st=%u md=%u sw=%02X rsn=%u gps=%u sat=%u dist=%u",
             (u16)snapshot.state,
             (u16)snapshot.mode,
             (u16)snapshot.auto_ret_onoff,
             (u16)snapshot.last_reason,
             (u16)snapshot.gps_ready,
             (u16)snapshot.sat_count,
             snapshot.distance_to_target_m);
    }
    (void)ship_protocol_send_frame(ship_protocol_rt.rf_channel[0],
                                   SHIP_CMD_AUTODRIVE_DIAG,
                                   ship_protocol_diag_payload,
                                   SHIP_AUTODRIVE_DIAG_PAYLOAD_LEN);
    (void)board_wireless_set_channel(ship_protocol_rt.rf_channel[0]);
}

void ship_protocol_service_autodrive_diag(u32 now_ms)
{
    static u8 initialized = 0U;
    static u8 last_state = 0U;
    static u8 last_mode = 0U;
    static u8 last_busy = 0U;
    static u8 last_reason = 0U;
    static u32 last_tx_ms = 0UL;
    AutoDrive_DebugSnapshot_t snapshot;
    u8 busy;
    u8 changed;

    AutoDrive_GetDebugSnapshot(&snapshot);
    busy = (snapshot.state != AUTO_DRIVE_IDLE) ? 1U : 0U;
    changed = (initialized == 0U) ? 1U : 0U;
    initialized = 1U;
    if ((snapshot.state != last_state) ||
        (snapshot.mode != last_mode) ||
        (busy != last_busy) ||
        (snapshot.last_reason != last_reason)) {
        changed = 1U;
    }
    if (((changed != 0U) && ((now_ms - last_tx_ms) >= SHIP_AUTODRIVE_DIAG_MIN_GAP_MS)) ||
        ((now_ms - last_tx_ms) >= SHIP_AUTODRIVE_DIAG_PERIOD_MS)) {
        ship_protocol_send_autodrive_diag_once(changed);
        last_tx_ms = now_ms;
    }
    last_state = snapshot.state;
    last_mode = snapshot.mode;
    last_busy = busy;
    last_reason = snapshot.last_reason;
}
