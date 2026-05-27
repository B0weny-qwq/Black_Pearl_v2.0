#include "ship_protocol_internal.h"
#include "board_wireless.h"
#include "logger.h"
#include "ship_control.h"

/* 配对与工作信道：旧 seed/key/channel 算法保留，硬件动作仍走 BoardDevices。 */
int8 ship_protocol_apply_work_sync_idle(void)
{
    ship_protocol_apply_default_rf();
    return board_wireless_set_sync_regs_idle(ship_protocol_pair_params.reg36,
                                             ship_protocol_pair_params.reg39);
}

int8 ship_protocol_apply_work_rx(void)
{
    int8 ret;

    ship_protocol_apply_default_rf();
    ret = board_wireless_set_channel(ship_protocol_rt.rf_channel[0]);
    if (ret == BOARD_WIRELESS_OK) {
        ship_protocol_rt.work_rx_configured = 1U;
        ship_protocol_rt.work_rx_reopen_ticks = 0U;
        ship_protocol_rt.rx_idle_warned = 0U;
    } else {
        ship_protocol_rt.work_rx_configured = 0U;
    }
    return ret;
}

int8 ship_protocol_try_pair_send(u16 left_after_send)
{
    u8 pair_xor;
    int8 ret;

    ship_protocol_apply_default_rf();
    pair_xor = (u8)(0x06U ^ SHIP_CMD_PAIR ^
                    ship_protocol_pair_params.seed[0] ^
                    ship_protocol_pair_params.seed[1] ^
                    ship_protocol_pair_params.seed[2] ^
                    ship_protocol_pair_params.seed[3]);
    ret = ship_protocol_send_frame(SHIP_PAIR_CHANNEL_DEFAULT,
                                   SHIP_CMD_PAIR,
                                   ship_protocol_pair_params.seed,
                                   4U);
    if (ret == BOARD_WIRELESS_OK) {
        LOGI(SHIP_TAG, "pair req sent seq=%u/%u retry=%u ch=0x%02X",
             (u16)(SHIP_PAIR_SEND_TIMES - left_after_send),
             (u16)SHIP_PAIR_SEND_TIMES,
             (u16)ship_protocol_rt.pair_retry_count,
             (u16)SHIP_PAIR_CHANNEL_DEFAULT);
        if (left_after_send == (SHIP_PAIR_SEND_TIMES - 1U)) {
            LOGI(SHIP_TAG,
                 "pair req start r=%u ch=%02X seed=%02X%02X%02X%02X work_rx=%u work_tx=%u key=%u/%u r36=%04X r39=%04X",
                 (u16)ship_protocol_rt.pair_retry_count,
                 (u16)SHIP_PAIR_CHANNEL_DEFAULT,
                 (u16)ship_protocol_pair_params.seed[0],
                 (u16)ship_protocol_pair_params.seed[1],
                 (u16)ship_protocol_pair_params.seed[2],
                 (u16)ship_protocol_pair_params.seed[3],
                 (u16)ship_protocol_rt.rf_channel[0],
                 (u16)ship_protocol_rt.rf_channel[2],
                 (u16)ship_protocol_rt.rf_send_key[0],
                 (u16)ship_protocol_rt.rf_send_key[1],
                 ship_protocol_pair_params.reg36,
                 ship_protocol_pair_params.reg39);
            LOGI(SHIP_TAG, "pair req frame=AA 06 10 %02X %02X %02X %02X %02X BB",
                 (u16)ship_protocol_pair_params.seed[0],
                 (u16)ship_protocol_pair_params.seed[1],
                 (u16)ship_protocol_pair_params.seed[2],
                 (u16)ship_protocol_pair_params.seed[3],
                 (u16)pair_xor);
        } else if (left_after_send == 0U) {
            LOGI(SHIP_TAG, "pair req burst done, wait rsp");
        }
    } else {
        LOGE(SHIP_TAG, "pair req tx fail rc=%d", ret);
    }
    return ret;
}

int8 ship_protocol_arm_pair_rsp_window(void)
{
    int8 ret;

    ret = ship_protocol_apply_work_sync_idle();
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }
    ret = ship_protocol_apply_work_rx();
    if (ret != BOARD_WIRELESS_OK) {
        return ret;
    }
    ship_protocol_rt.pair_wait_rsp_ticks = SHIP_PAIR_WAIT_RSP_TICKS;
    ship_protocol_rt.pair_wait_start_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.last_proto_rx_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.pair_rsp_timeout_logged = 0U;
    LOGI(SHIP_TAG, "pair rsp win ticks=%u rx=%u key=%u/%u r36=%04X r39=%04X",
         (u16)ship_protocol_rt.pair_wait_rsp_ticks,
         (u16)ship_protocol_rt.rf_channel[0],
         (u16)ship_protocol_rt.rf_send_key[0],
         (u16)ship_protocol_rt.rf_send_key[1],
         ship_protocol_pair_params.reg36,
         ship_protocol_pair_params.reg39);
    return BOARD_WIRELESS_OK;
}

void ship_protocol_mark_proto_activity(u8 cmd)
{
    ship_protocol_rt.last_proto_rx_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.remote_online = 1U;
    ship_protocol_rt.rx_idle_warned = 0U;
    ship_protocol_rt.throttle_recover_done = 0U;
    if (ship_protocol_rt.paired != 0U) {
        return;
    }
    ship_protocol_rt.paired = 1U;
    ship_protocol_rt.pair_left = 0U;
    ship_protocol_rt.pair_wait_rsp_ticks = 0U;
    ship_protocol_rt.pair_wait_start_ms = 0UL;
    ship_protocol_rt.pair_rsp_timeout_logged = 0U;
    ship_protocol_rt.state = SHIP_PROTOCOL_STATE_WORK_RX;
    ship_protocol_rt.work_state_logged = 0U;
    LOGI(SHIP_TAG, "pair ok by frame cmd=%02X rx=%u key=%u/%u",
         (u16)cmd,
         (u16)ship_protocol_rt.rf_channel[0],
         (u16)ship_protocol_rt.rf_send_key[0],
         (u16)ship_protocol_rt.rf_send_key[1]);
}

void ship_protocol_step_pair_send(void)
{
    int8 ret;
    u16 left_after_send;

    if (ship_protocol_rt.paired != 0U) {
        ship_protocol_rt.pair_left = 0U;
        ship_protocol_rt.state = SHIP_PROTOCOL_STATE_WORK_RX;
        return;
    }
    if (ship_protocol_rt.pair_left == 0U) {
        return;
    }
    ship_protocol_rt.wait_ticks = SHIP_WAIT_TICKS_DEFAULT;
    left_after_send = (u16)(ship_protocol_rt.pair_left - 1U);
    ret = ship_protocol_try_pair_send(left_after_send);
    if (ret != BOARD_WIRELESS_OK) {
        return;
    }
    ship_protocol_rt.pair_left = left_after_send;
    if (ship_protocol_rt.pair_left != 0U) {
        return;
    }
    ret = ship_protocol_arm_pair_rsp_window();
    if (ret != BOARD_WIRELESS_OK) {
        LOGE(SHIP_TAG, "pair rsp window arm fail rc=%d", ret);
        ship_protocol_rt.pair_retry_count++;
        ship_protocol_rt.pair_left = SHIP_PAIR_SEND_TIMES;
        ship_protocol_rt.wait_ticks = SHIP_WAIT_TICKS_DEFAULT;
        ship_protocol_rt.pair_wait_rsp_ticks = 0U;
        ship_protocol_rt.pair_wait_start_ms = 0UL;
        return;
    }
    ship_protocol_rt.state = SHIP_PROTOCOL_STATE_PAIR_WAIT_RSP;
    LOGI(SHIP_TAG, "pair burst done, rsp wait");
}

void ship_protocol_step_work_rx(void)
{
    int8 ret;

    if (ship_protocol_rt.work_rx_configured == 0U) {
        ret = ship_protocol_apply_work_rx();
        if (ret != BOARD_WIRELESS_OK) {
            LOGE(SHIP_TAG, "enter work rx fail rc=%d", ret);
            return;
        }
        if (ship_protocol_rt.last_proto_rx_ms == 0UL) {
            ship_protocol_rt.last_proto_rx_ms = ship_protocol_rt.tick_ms;
        }
    }
    if (ship_protocol_rt.work_state_logged == 0U) {
        ship_protocol_rt.work_state_logged = 1U;
        LOGI(SHIP_TAG, "enter work-state rx_ch=%u tx_ch=%u",
             (u16)ship_protocol_rt.rf_channel[0],
             (u16)ship_protocol_rt.rf_channel[0]);
        return;
    }
    ship_protocol_rt.work_rx_reopen_ticks++;
    if (ship_protocol_rt.work_rx_reopen_ticks > SHIP_WORK_RX_REOPEN_TICKS) {
        ret = ship_protocol_apply_work_rx();
        if (ret == BOARD_WIRELESS_OK) {
            ship_protocol_rt.work_rx_reopen_total++;
        } else {
            LOGE(SHIP_TAG, "work rx reopen fail rc=%d", ret);
        }
    }
}

void ship_protocol_check_timeouts(void)
{
    u32 rx_silence_ms;
    u32 throttle_silence_ms;

    if ((ship_protocol_rt.paired == 0U) &&
        (SHIP_PAIR_FORCE_WORK_MS != 0UL) &&
        ((ship_protocol_rt.tick_ms - ship_protocol_rt.pair_start_ms) >= SHIP_PAIR_FORCE_WORK_MS)) {
        ship_protocol_rt.paired = 1U;
        ship_protocol_rt.pair_left = 0U;
        ship_protocol_rt.pair_wait_rsp_ticks = 0U;
        ship_protocol_rt.pair_wait_start_ms = 0UL;
        ship_protocol_rt.state = SHIP_PROTOCOL_STATE_WORK_RX;
        ship_protocol_rt.work_rx_configured = 0U;
        ship_protocol_rt.work_state_logged = 0U;
        LOGW(SHIP_TAG, "pair force work-rx after %lums", (u32)SHIP_PAIR_FORCE_WORK_MS);
    }
    if (ship_protocol_rt.pair_wait_rsp_ticks > 0U) {
        ship_protocol_rt.pair_wait_rsp_ticks--;
    } else if ((ship_protocol_rt.pair_wait_start_ms != 0UL) &&
               (ship_protocol_rt.pair_rsp_timeout_logged == 0U) &&
               ((ship_protocol_rt.tick_ms - ship_protocol_rt.pair_wait_start_ms) >= SHIP_PAIR_RSP_EXPIRE_LOG_MS) &&
               (ship_protocol_rt.paired == 0U)) {
        ship_protocol_rt.pair_rsp_timeout_logged = 1U;
        ship_protocol_rt.pair_retry_count++;
        ship_protocol_rt.pair_left = SHIP_PAIR_SEND_TIMES;
        ship_protocol_rt.wait_ticks = 0U;
        ship_protocol_rt.pair_wait_rsp_ticks = 0U;
        ship_protocol_rt.pair_wait_start_ms = 0UL;
        ship_protocol_rt.state = SHIP_PROTOCOL_STATE_PAIR_SEND;
        ship_protocol_rt.work_rx_configured = 0U;
        ship_protocol_rt.work_state_logged = 0U;
        LOGW(SHIP_TAG, "pair rsp window expired, retry seed burst");
    }
    if ((ship_protocol_rt.state == SHIP_PROTOCOL_STATE_WORK_RX) ||
        (ship_protocol_rt.state == SHIP_PROTOCOL_STATE_PAIR_WAIT_RSP)) {
        rx_silence_ms = ship_protocol_rt.tick_ms - ship_protocol_rt.last_proto_rx_ms;
        if ((ship_protocol_rt.last_proto_rx_ms != 0UL) &&
            (ship_protocol_rt.rx_idle_warned == 0U) &&
            (rx_silence_ms >= SHIP_RX_IDLE_WARN_MS)) {
            ship_protocol_rt.rx_idle_warned = 1U;
            LOGW(SHIP_TAG, "rx idle %lums no frame", rx_silence_ms);
        }
        if ((ship_protocol_rt.remote_online != 0U) &&
            (rx_silence_ms >= SHIP_THROTTLE_TIMEOUT_MS)) {
            ship_protocol_rt.remote_online = 0U;
            if (ship_protocol_rt.throttle_online != 0U) {
                ship_protocol_rt.throttle_online = 0U;
                ShipControl_Stop(SHIP_CONTROL_STOP_REASON_REMOTE_TIMEOUT);
            }
            LOGW(SHIP_TAG, "remote timeout dt=%lums", rx_silence_ms);
        }
        if ((ship_protocol_rt.paired != 0U) &&
            (ship_protocol_rt.throttle_recover_done == 0U) &&
            (ship_protocol_rt.last_proto_rx_ms != 0UL) &&
            (rx_silence_ms >= SHIP_THROTTLE_RECOVER_MS)) {
            ship_protocol_rt.throttle_recover_done = 1U;
            ship_protocol_apply_default_rf();
            ship_protocol_rt.work_rx_reopen_ticks = 0U;
            LOGW(SHIP_TAG, "remote recover dt=%lums", rx_silence_ms);
            (void)ship_protocol_apply_work_rx();
        }
    }
    if (ship_protocol_rt.throttle_online != 0U) {
        throttle_silence_ms = ship_protocol_rt.tick_ms - ship_protocol_rt.last_throttle_rx_ms;
        if (throttle_silence_ms >= SHIP_THROTTLE_TIMEOUT_MS) {
            ship_protocol_rt.throttle_online = 0U;
            LOGW(SHIP_TAG, "manual timeout dt=%lums", throttle_silence_ms);
            ShipControl_Stop(SHIP_CONTROL_STOP_REASON_MANUAL_TIMEOUT);
        }
    }
}
