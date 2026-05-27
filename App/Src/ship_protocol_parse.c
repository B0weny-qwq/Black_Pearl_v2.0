#include "ship_protocol_internal.h"
#include "board_wireless.h"
#include "logger.h"

/* AA len cmd payload xor BB 旧帧解析和命令分发入口。 */
int8 ship_protocol_parse_frame(const u8 *frame, u8 frame_len)
{
    u8 body_len;
    u8 payload_len;
    u8 cmd;
    u8 checksum;
    const u8 *payload;

    if ((frame == 0) || (frame_len < 5U)) {
        return BOARD_WIRELESS_ERR_PARAM;
    }
    if ((frame[0] != SHIP_PROTO_HEAD) || (frame[frame_len - 1U] != SHIP_PROTO_TAIL)) {
        LOGW(SHIP_TAG, "bad frame edge len=%u", (u16)frame_len);
        return BOARD_WIRELESS_ERR_VERIFY;
    }
    body_len = frame[1];
    if (((u8)(body_len + 3U) != frame_len) || (body_len < 2U)) {
        LOGW(SHIP_TAG, "bad len field=%u frame=%u", (u16)body_len, (u16)frame_len);
        return BOARD_WIRELESS_ERR_VERIFY;
    }
    payload_len = (u8)(body_len - 2U);
    checksum = ship_protocol_xor(&frame[1], (u8)(2U + payload_len));
    if (checksum != frame[frame_len - 2U]) {
        LOGW(SHIP_TAG, "bad xor cmd=0x%02X calc=0x%02X got=0x%02X",
             (u16)frame[2],
             (u16)checksum,
             (u16)frame[frame_len - 2U]);
        return BOARD_WIRELESS_ERR_VERIFY;
    }
    cmd = frame[2];
    payload = &frame[3];
    if (cmd == SHIP_CMD_PAIR_RSP) {
        ship_protocol_handle_pair_rsp(payload, payload_len);
        ship_protocol_send_gps_once();
        return BOARD_WIRELESS_OK;
    }
    ship_protocol_mark_proto_activity(cmd);
    switch (cmd) {
    case SHIP_CMD_THROTTLE:
        ship_protocol_handle_throttle(payload, payload_len);
        break;
    case SHIP_CMD_RETURN_HOME:
        ship_protocol_handle_return_home(payload, payload_len);
        break;
    case SHIP_CMD_GOTO_POINT:
        (void)ship_protocol_handle_fish_point(payload,
                                              payload_len,
                                              frame,
                                              frame_len,
                                              checksum,
                                              frame[frame_len - 2U]);
        break;
    case SHIP_CMD_RETURN_SWITCH:
        ship_protocol_handle_return_switch(payload, payload_len);
        break;
    case SHIP_CMD_GPS_REPORT:
        LOGI(SHIP_TAG, "cmd=0x12 rx ignored len=%u", (u16)payload_len);
        break;
    default:
        LOGW(SHIP_TAG, "unknown cmd=0x%02X len=%u", (u16)cmd, (u16)payload_len);
        break;
    }
    ship_protocol_send_gps_once();
    return BOARD_WIRELESS_OK;
}

static void ship_protocol_consume_byte(u8 value)
{
    u8 frame_len;

    if (ship_protocol_parse_state == SHIP_PARSE_WAIT_HEAD) {
        if (value != SHIP_PROTO_HEAD) {
            return;
        }
        ship_protocol_parse_index = 0U;
        ship_protocol_parse_buffer[ship_protocol_parse_index++] = value;
        ship_protocol_parse_expected_len = 0U;
        ship_protocol_parse_state = SHIP_PARSE_READ_LEN;
        return;
    }
    ship_protocol_parse_buffer[ship_protocol_parse_index++] = value;
    if (ship_protocol_parse_state == SHIP_PARSE_READ_LEN) {
        ship_protocol_parse_expected_len = (u8)(value + 3U);
        if ((ship_protocol_parse_expected_len < 5U) ||
            (ship_protocol_parse_expected_len > SHIP_PROTO_MAX_FRAME_LEN)) {
            ship_protocol_parse_index = 0U;
            ship_protocol_parse_expected_len = 0U;
            ship_protocol_parse_state = SHIP_PARSE_WAIT_HEAD;
        } else {
            ship_protocol_parse_state = SHIP_PARSE_READ_BODY;
        }
        return;
    }
    if ((ship_protocol_parse_state == SHIP_PARSE_READ_BODY) &&
        (ship_protocol_parse_expected_len != 0U) &&
        (ship_protocol_parse_index >= ship_protocol_parse_expected_len)) {
        frame_len = ship_protocol_parse_index;
        ship_protocol_parse_state = SHIP_PARSE_DISPATCH;
        (void)ship_protocol_parse_frame(ship_protocol_parse_buffer, frame_len);
        ship_protocol_parse_index = 0U;
        ship_protocol_parse_expected_len = 0U;
        ship_protocol_parse_state = SHIP_PARSE_WAIT_HEAD;
    }
    if (ship_protocol_parse_index >= SHIP_PROTO_MAX_FRAME_LEN) {
        ship_protocol_parse_index = 0U;
        ship_protocol_parse_expected_len = 0U;
        ship_protocol_parse_state = SHIP_PARSE_WAIT_HEAD;
    }
}

void ship_protocol_poll_rx_frames(void)
{
    u8 payload_len;
    u8 i;
    int8 ret;

    do {
        ret = board_wireless_receive(ship_protocol_rx_payload,
                                     BOARD_WIRELESS_MAX_PAYLOAD_LEN,
                                     &payload_len);
        if (ret == BOARD_WIRELESS_OK) {
            for (i = 0U; i < payload_len; i++) {
                ship_protocol_consume_byte(ship_protocol_rx_payload[i]);
            }
        }
    } while (ret == BOARD_WIRELESS_OK);
}
