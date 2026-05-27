#include "ship_protocol_internal.h"
#include "logger.h"

#if (SHIP_PROTOCOL_VERBOSE_LOG_ENABLE != 0U)
static const char *ship_protocol_fish_result_name(u8 result)
{
    switch (result) {
    case AUTODRIVE_FISH_CMD_BUSY:
        return "busy";
    case AUTODRIVE_FISH_CMD_STORED:
        return "stored";
    case AUTODRIVE_FISH_CMD_DUP_WAIT:
        return "dup-wait";
    case AUTODRIVE_FISH_CMD_REJECT_UNKNOWN:
        return "reject-unknown";
    case AUTODRIVE_FISH_CMD_REJECT_DISTANCE:
        return "reject-distance";
    case AUTODRIVE_FISH_CMD_STARTED:
        return "started";
    case AUTODRIVE_FISH_CMD_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}
#endif

static void ship_protocol_log_goto_point(const u8 *frame,
                                         u8 frame_len,
                                         const u8 *payload,
                                         u8 payload_len,
                                         u8 xor_calc,
                                         u8 xor_recv,
                                         u8 result)
{
#if (SHIP_PROTOCOL_VERBOSE_LOG_ENABLE != 0U)
    ship_protocol_point_t point;
#endif
    u8 idx;
    u8 save_result;

    idx = AutoDrive_GetLastFishCommandIndex();
    save_result = AutoDrive_GetLastFishSaveResult();
    LOGI(SHIP_TAG, "0x14 rx save=%u nav=%u idx=%u",
         (u16)save_result,
         (u16)result,
         (u16)idx);
#if (SHIP_PROTOCOL_VERBOSE_LOG_ENABLE != 0U)
    LOGI(SHIP_TAG, "0x14 nav=%s", ship_protocol_fish_result_name(result));
    if ((payload != 0) && (payload_len >= SHIP_PROTOCOL_POINT_PAYLOAD_LEN)) {
        ship_protocol_parse_point_payload(payload, &point);
        ship_protocol_log_point("0x14 point", &point);
    }
    if ((frame != 0) && (frame_len >= 5U)) {
        LOGI(SHIP_TAG, "0x14 frm h=%02X len=%u cmd=%02X t=%02X",
             (u16)frame[0],
             (u16)frame[1],
             (u16)frame[2],
             (u16)frame[frame_len - 1U]);
    }
#else
    if ((frame != 0) || (frame_len != 0U) || (payload != 0) ||
        (payload_len != 0U) || (xor_calc != xor_recv)) {
        return;
    }
#endif
}

/* 0x13/0x14/0x15 自动驾驶入口：协议层只解帧，路径计算交给 AutoDrive。 */
void ship_protocol_handle_return_home(const u8 *payload, u8 payload_len)
{
    if ((payload == 0) || (payload_len < SHIP_PROTOCOL_POINT_PAYLOAD_LEN)) {
        LOGW(SHIP_TAG, "0x13 short len=%u", (u16)payload_len);
        ship_protocol_publish_error_event(SHIP_CMD_RETURN_HOME, payload_len);
        return;
    }
    ship_protocol_clear_event_payload();
    ship_protocol_parse_point_payload(payload, &ship_protocol_rt.event.point);
    ship_protocol_rt.event.switch_state = 0U;
    ship_protocol_rt.event.point_valid = 1U;
    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_RETURN_HOME,
                                SHIP_PROTOCOL_EVENT_STATE_RETURN_HOME_PENDING,
                                SHIP_CMD_RETURN_HOME,
                                payload_len);
    ship_protocol_log_point("0x13 ret", &ship_protocol_rt.event.point);
    AutoDrive_SetReturnPositionRaw(payload);
}

u8 ship_protocol_handle_fish_point(const u8 *payload,
                                   u8 payload_len,
                                   const u8 *frame,
                                   u8 frame_len,
                                   u8 xor_calc,
                                   u8 xor_recv)
{
    u8 result;

    if ((payload == 0) || (payload_len < SHIP_PROTOCOL_POINT_PAYLOAD_LEN)) {
        LOGW(SHIP_TAG, "0x14 short len=%u", (u16)payload_len);
        ship_protocol_publish_error_event(SHIP_CMD_GOTO_POINT, payload_len);
        ship_protocol_log_goto_point(frame,
                                     frame_len,
                                     payload,
                                     payload_len,
                                     xor_calc,
                                     xor_recv,
                                     AUTODRIVE_FISH_CMD_INVALID);
        return AUTODRIVE_FISH_CMD_INVALID;
    }
    ship_protocol_clear_event_payload();
    ship_protocol_parse_point_payload(payload, &ship_protocol_rt.event.point);
    ship_protocol_rt.event.switch_state = 0U;
    ship_protocol_rt.event.point_valid = 1U;
    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_FISH_POINT,
                                SHIP_PROTOCOL_EVENT_STATE_FISH_POINT_PENDING,
                                SHIP_CMD_GOTO_POINT,
                                payload_len);
    ship_protocol_log_point("0x14 fish", &ship_protocol_rt.event.point);
    result = AutoDrive_SetFishPositionRaw(payload);
    ship_protocol_log_goto_point(frame, frame_len, payload, payload_len, xor_calc, xor_recv, result);
    return result;
}

void ship_protocol_handle_return_switch(const u8 *payload, u8 payload_len)
{
    if ((payload == 0) || (payload_len < 1U)) {
        LOGW(SHIP_TAG, "0x15 short len=%u", (u16)payload_len);
        ship_protocol_publish_error_event(SHIP_CMD_RETURN_SWITCH, payload_len);
        return;
    }
    ship_protocol_clear_event_payload();
    ship_protocol_rt.event.switch_state = payload[0];
    ship_protocol_clear_point_event();
    if (payload_len >= (u8)(1U + SHIP_PROTOCOL_POINT_PAYLOAD_LEN)) {
        ship_protocol_parse_point_payload(&payload[1], &ship_protocol_rt.event.point);
        ship_protocol_rt.event.point_valid = 1U;
    }
    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_RETURN_SWITCH,
                                SHIP_PROTOCOL_EVENT_STATE_RETURN_SWITCH_PENDING,
                                SHIP_CMD_RETURN_SWITCH,
                                payload_len);
    LOGI(SHIP_TAG, "0x15 sw=%02X point=%u len=%u",
         (u16)ship_protocol_rt.event.switch_state,
         (u16)ship_protocol_rt.event.point_valid,
         (u16)payload_len);
    if (ship_protocol_rt.event.point_valid != 0U) {
        ship_protocol_log_point("0x15 point", &ship_protocol_rt.event.point);
    }
    AutoDrive_SetSwitchRaw(payload, payload_len);
}
