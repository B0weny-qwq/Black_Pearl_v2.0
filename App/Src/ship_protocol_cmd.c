#include "ship_protocol_internal.h"
#include "app.h"
#include "logger.h"
#include "north_calib.h"
#include "platform_scheduler.h"
#include "ship_control.h"

/* 0x11 按键语义：E 键巡航，B/C/D 仅发事件给外包扩展入口。 */
static void ship_protocol_service_north_calib_key(u8 key, u32 now_ms)
{
    if (key == SHIP_PROTOCOL_KEY_D_UNUSED) {
        if (ship_protocol_rt.d_key_pressed == 0U) {
            ship_protocol_rt.d_key_pressed = 1U;
            if ((ship_protocol_rt.d_key_click_waiting != 0U) &&
                ((now_ms - ship_protocol_rt.d_key_first_click_ms) <=
                 SHIP_NORTH_CALIB_DOUBLE_CLICK_MS)) {
                ship_protocol_rt.d_key_click_waiting = 0U;
                ship_protocol_rt.d_key_first_click_ms = 0UL;
                if (NorthCalib_RequestStart() != 0U) {
                    LOGI(SHIP_TAG, "key action=D north-calib-start");
                } else {
                    LOGW(SHIP_TAG, "key action=D north-calib-reject");
                }
            } else {
                ship_protocol_rt.d_key_click_waiting = 1U;
                ship_protocol_rt.d_key_first_click_ms = now_ms;
            }
        }
    } else {
        ship_protocol_rt.d_key_pressed = 0U;
        if ((ship_protocol_rt.d_key_click_waiting != 0U) &&
            ((now_ms - ship_protocol_rt.d_key_first_click_ms) >
             SHIP_NORTH_CALIB_DOUBLE_CLICK_MS)) {
            ship_protocol_rt.d_key_click_waiting = 0U;
            ship_protocol_rt.d_key_first_click_ms = 0UL;
        }
    }
}

static void ship_protocol_handle_key_edge(u8 key)
{
    ship_protocol_key_action_t key_action;
    int16 throttle_input;
    int16 steering_input;
    int16 yaw_rate_dps;
    u16 heading_cd;

    key_action = ship_protocol_key_to_action(key);
    ship_protocol_rt.event.throttle.lr = ship_protocol_rt.lr;
    ship_protocol_rt.event.throttle.ud = ship_protocol_rt.ud;
    ship_protocol_rt.event.throttle.key = ship_protocol_rt.key;
    ship_protocol_rt.event.throttle.key_event = key;
    ship_protocol_rt.event.throttle.throttle_input = ship_protocol_raw_ud_to_input(ship_protocol_rt.ud);
    ship_protocol_rt.event.throttle.steering_input = ship_protocol_raw_lr_to_input(ship_protocol_rt.lr);
    ship_protocol_clear_event_payload();
    ship_protocol_clear_point_event();
    ship_protocol_rt.event.key_action = key_action;
    ship_protocol_publish_event((key_action != SHIP_PROTOCOL_KEY_ACTION_NONE) ?
                                SHIP_PROTOCOL_EVENT_KEY_ACTION :
                                SHIP_PROTOCOL_EVENT_KEY_EDGE,
                                (key_action != SHIP_PROTOCOL_KEY_ACTION_NONE) ?
                                SHIP_PROTOCOL_EVENT_STATE_KEY_ACTION :
                                SHIP_PROTOCOL_EVENT_STATE_KEY_EDGE,
                                SHIP_CMD_THROTTLE,
                                3U);
    LOGI(SHIP_TAG, "key edge key=0x%02X action=%u", (u16)key, (u16)key_action);
    if (key != SHIP_PROTOCOL_KEY_E_RESERVED) {
        return;
    }

    throttle_input = ship_protocol_raw_ud_to_input(ship_protocol_rt.ud);
    steering_input = ship_protocol_raw_lr_to_input(ship_protocol_rt.lr);
    yaw_rate_dps = 0;
    if (app_get_attitude_state() != 0) {
        yaw_rate_dps = (int16)(app_get_attitude_state()->gyro_z_dps100 / 100);
    }
    if (ShipControl_GetMode() == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) {
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_CRUISE_KEY);
        LOGI("DATA", "cruise exit reason=key-toggle input=%d raw_ud=%u",
             throttle_input,
             (u16)ship_protocol_rt.ud);
        return;
    }
    if ((throttle_input > SHIP_CRUISE_KEY_START_INPUT) &&
        (steering_input <= SHIP_CRUISE_STEER_START_MAX) &&
        (steering_input >= (int16)(-SHIP_CRUISE_STEER_START_MAX)) &&
        (yaw_rate_dps <= SHIP_CRUISE_GYRO_START_MAX_DPS) &&
        (yaw_rate_dps >= (int16)(-SHIP_CRUISE_GYRO_START_MAX_DPS))) {
        if (app_get_heading_ready() != 0U) {
            heading_cd = app_get_heading_deg100();
            ShipControl_RequestCruise(heading_cd, SHIP_CRUISE_KEY_SPEED);
            LOGI("DATA", "cruise enter input=%d raw_ud=%u speed=%d hd=%u",
                 throttle_input,
                 (u16)ship_protocol_rt.ud,
                 (int16)SHIP_CRUISE_KEY_SPEED,
                 heading_cd);
        } else {
            ShipControl_Stop(SHIP_CONTROL_STOP_REASON_HEADING_LOST);
            LOGW(SHIP_TAG, "cruise rej no-hdg");
        }
    } else {
        LOGW(SHIP_TAG, "cruise reject input=%d steer=%d yaw=%d",
             throttle_input,
             steering_input,
             yaw_rate_dps);
    }
}

void ship_protocol_handle_pair_rsp(const u8 *payload, u8 payload_len)
{
    if (ship_protocol_rt.pair_wait_rsp_ticks == 0U) {
        LOGW(SHIP_TAG, "pair rsp ignored outside window len=%u", (u16)payload_len);
        return;
    }
    ship_protocol_rt.pair_wait_rsp_ticks = 0U;
    ship_protocol_rt.pair_wait_start_ms = 0UL;
    ship_protocol_rt.last_proto_rx_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.paired = 1U;
    ship_protocol_rt.pair_left = 0U;
    ship_protocol_rt.wait_ticks = 0U;
    ship_protocol_rt.state = SHIP_PROTOCOL_STATE_WORK_RX;
    ship_protocol_rt.work_rx_configured = 1U;
    ship_protocol_rt.work_state_logged = 0U;
    ship_protocol_rt.pair_rsp_timeout_logged = 0U;
    LOGI(SHIP_TAG, "pair ok work rx=%u tx=%u",
         (u16)ship_protocol_rt.rf_channel[0],
         (u16)ship_protocol_rt.rf_channel[0]);
#if (SHIP_PROTOCOL_VERBOSE_LOG_ENABLE != 0U)
    if ((payload != 0) && (payload_len == 4U)) {
        LOGI(SHIP_TAG, "pair rsp=%02X%02X%02X%02X",
             (u16)payload[0], (u16)payload[1], (u16)payload[2], (u16)payload[3]);
    }
#else
    if ((payload == 0) && (payload_len == 0U)) {
        return;
    }
#endif
}

void ship_protocol_handle_throttle(const u8 *payload, u8 payload_len)
{
    u32 now_ms;

    if (payload_len < 3U) {
        LOGW(SHIP_TAG, "throttle short len=%u", (u16)payload_len);
        ship_protocol_publish_error_event(SHIP_CMD_THROTTLE, payload_len);
        return;
    }
    ship_protocol_rt.lr = payload[0];
    ship_protocol_rt.ud = payload[1];
    ship_protocol_rt.key = payload[2];
    ship_protocol_rt.valid = 1U;
    ship_protocol_rt.last_throttle_rx_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.throttle_recover_done = 0U;
    ship_protocol_rt.throttle_online = 1U;
    now_ms = platform_scheduler_get_tick_ms();
    NorthCalib_UpdateRemoteInput(ship_protocol_rt.lr,
                                 ship_protocol_rt.ud,
                                 ship_protocol_rt.key,
                                 now_ms);
    ship_protocol_service_north_calib_key(ship_protocol_rt.key, now_ms);
    ship_protocol_rt.event.throttle.lr = ship_protocol_rt.lr;
    ship_protocol_rt.event.throttle.ud = ship_protocol_rt.ud;
    ship_protocol_rt.event.throttle.key = ship_protocol_rt.key;
    ship_protocol_rt.event.throttle.throttle_input = ship_protocol_raw_ud_to_input(ship_protocol_rt.ud);
    ship_protocol_rt.event.throttle.steering_input = ship_protocol_raw_lr_to_input(ship_protocol_rt.lr);
    ship_protocol_rt.event.throttle.key_changed =
        (ship_protocol_rt.key != ship_protocol_rt.last_key) ? 1U : 0U;
    ship_protocol_clear_event_payload();
    ship_protocol_clear_point_event();
    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_THROTTLE,
                                SHIP_PROTOCOL_EVENT_STATE_THROTTLE_ACTIVE,
                                SHIP_CMD_THROTTLE,
                                payload_len);
    LOGI(SHIP_TAG, "rc cmd=0x11 lr=%u ud=%u tv=%d sv=%d key=0x%02X",
         (u16)ship_protocol_rt.lr,
         (u16)ship_protocol_rt.ud,
         ship_protocol_rt.event.throttle.throttle_input,
         ship_protocol_rt.event.throttle.steering_input,
         (u16)ship_protocol_rt.key);
    if ((now_ms < SHIP_MANUAL_BOOT_BLOCK_MS) ||
        ((SHIP_MANUAL_BOOT_WAIT_HEADING != 0U) && (app_get_heading_ready() == 0U))) {
        if (ship_protocol_rt.manual_boot_block_logged == 0U) {
            ship_protocol_rt.manual_boot_block_logged = 1U;
            LOGW(SHIP_TAG, "boot blk wait=%lums hd=%u",
                 (now_ms < SHIP_MANUAL_BOOT_BLOCK_MS) ?
                 (u32)(SHIP_MANUAL_BOOT_BLOCK_MS - now_ms) : 0UL,
                 (u16)app_get_heading_ready());
        }
        AutoDrive_LinkAliveKick();
        return;
    }
    if (ship_protocol_rt.manual_boot_ready_logged == 0U) {
        ship_protocol_rt.manual_boot_ready_logged = 1U;
        LOGI(SHIP_TAG, "boot ready t=%lums hd=%u", now_ms, (u16)app_get_heading_ready());
    }
    if (NorthCalib_IsBusy() != 0U) {
        if ((ship_protocol_rt.key != ship_protocol_rt.last_key) &&
            (ship_protocol_rt.key == SHIP_PROTOCOL_KEY_E_RESERVED)) {
            NorthCalib_Cancel(NORTH_CALIB_FAIL_USER_CANCEL);
            LOGI(SHIP_TAG, "key action=E north-calib-cancel");
        }
        ship_protocol_rt.last_key = ship_protocol_rt.key;
        AutoDrive_LinkAliveKick();
        return;
    }
    if (AutoDrive_IsBusy() != 0U) {
        AutoDrive_LinkAliveKick();
        return;
    }
    if (ship_protocol_rt.key != ship_protocol_rt.last_key) {
        ship_protocol_handle_key_edge(ship_protocol_rt.key);
        ship_protocol_rt.last_key = ship_protocol_rt.key;
    }
    if ((ShipControl_GetMode() == SHIP_CONTROL_MODE_CRUISE_HEADING_HOLD) &&
        (ship_protocol_raw_ud_to_input(ship_protocol_rt.ud) < SHIP_CRUISE_KEY_STOP_INPUT)) {
        ShipControl_Stop(SHIP_CONTROL_STOP_REASON_CRUISE_KEY);
        LOGI("DATA", "cruise exit reason=throttle input=%d raw_ud=%u stop_th=%d",
             ship_protocol_raw_ud_to_input(ship_protocol_rt.ud),
             (u16)ship_protocol_rt.ud,
             (int16)SHIP_CRUISE_KEY_STOP_INPUT);
        AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
        AutoDrive_LinkAliveKick();
        return;
    }
    ShipControl_UpdateManualInput(ship_protocol_rt.lr, ship_protocol_rt.ud, ship_protocol_rt.key, now_ms);
    AutoDrive_LinkAliveKick();
}
