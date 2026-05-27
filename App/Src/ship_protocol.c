#include "ship_protocol_internal.h"
#include "autodrive.h"
#include "platform_scheduler.h"
#include "ship_control.h"

/* 协议主入口保留旧无线状态机初始化和 10 ms 调度节拍。 */
void ship_protocol_init(void)
{
#if (SHIP_PROTOCOL_VERBOSE_LOG_ENABLE != 0U)
    u8 seed[4];
    ship_protocol_get_pair_seed(seed);
#endif
    ship_protocol_apply_default_rf();
    ship_protocol_rt.lr = SHIP_AXIS_CENTER;
    ship_protocol_rt.ud = SHIP_AXIS_CENTER;
    ship_protocol_rt.key = SHIP_KEY_NULL;
    ship_protocol_rt.last_key = SHIP_KEY_NULL;
    ship_protocol_rt.valid = 0U;
    ship_protocol_rt.paired = 0U;
    ship_protocol_rt.work_rx_configured = 0U;
    ship_protocol_rt.work_state_logged = 0U;
    ship_protocol_rt.state = SHIP_PROTOCOL_STATE_BOOT_WAIT;
    ship_protocol_rt.pair_wait_rsp_ticks = 0U;
    ship_protocol_rt.wait_ticks = SHIP_WAIT_TICKS_DEFAULT;
    ship_protocol_rt.pair_left = SHIP_PAIR_SEND_TIMES;
    ship_protocol_rt.pair_retry_count = 0U;
    ship_protocol_rt.work_rx_reopen_ticks = 0U;
    ship_protocol_rt.work_rx_reopen_total = 0U;
    ship_protocol_rt.tick_ms = platform_scheduler_get_tick_ms();
    ship_protocol_rt.pair_start_ms = ship_protocol_rt.tick_ms;
    ship_protocol_rt.pair_wait_start_ms = 0UL;
    ship_protocol_rt.last_proto_rx_ms = 0UL;
    ship_protocol_rt.last_throttle_rx_ms = 0UL;
    ship_protocol_rt.pair_rsp_timeout_logged = 0U;
    ship_protocol_rt.rx_idle_warned = 0U;
    ship_protocol_rt.remote_online = 0U;
    ship_protocol_rt.throttle_online = 0U;
    ship_protocol_rt.throttle_recover_done = 0U;
    ship_protocol_rt.manual_boot_block_logged = 0U;
    ship_protocol_rt.manual_boot_ready_logged = 0U;
    ship_protocol_rt.power_level = SHIP_POWER_LEVEL_0;
    ship_protocol_rt.power_adc_ready = 0U;
    ship_protocol_rt.power_first_valid_logged = 0U;
    ship_protocol_rt.lowpower_return_latched = 0U;
    ship_protocol_rt.power_sample_divider_count = 0U;
    ship_protocol_rt.lowpower_check_ticks = 0U;
    ship_protocol_rt.power_sample_period_ms = (u32)SHIP_POWER_SAMPLE_DIVIDER * 10UL;
    ship_protocol_rt.power_sample.raw = 0U;
    ship_protocol_rt.power_sample.adc_mv = 0U;
    ship_protocol_rt.power_sample.bat_mv = 0UL;
    ship_protocol_rt.power_sample.report = SHIP_POWER_LEVEL_0;
    ship_protocol_rt.power_sample.valid = 0U;
    ship_protocol_rt.power_sample.sampled = 0U;
    ship_protocol_rt.power_sample.status = BOARD_POWER_ERR_NOT_READY;
    ship_protocol_rt.event.type = SHIP_PROTOCOL_EVENT_NONE;
    ship_protocol_rt.event.state = SHIP_PROTOCOL_EVENT_STATE_IDLE;
    ship_protocol_rt.event.cmd = 0U;
    ship_protocol_rt.event.payload_len = 0U;
    ship_protocol_rt.event.switch_state = 0U;
    ship_protocol_rt.event.point_valid = 0U;
    ship_protocol_rt.event.pending = 0U;
    ship_protocol_rt.event.sequence = 0U;
    ship_protocol_rt.event.tick_ms = 0UL;
    ship_protocol_rt.event.throttle.lr = SHIP_AXIS_CENTER;
    ship_protocol_rt.event.throttle.ud = SHIP_AXIS_CENTER;
    ship_protocol_rt.event.throttle.key = SHIP_KEY_NULL;
    ship_protocol_rt.event.throttle.key_changed = 0U;
    ship_protocol_rt.event.throttle.key_event = SHIP_KEY_NULL;
    ship_protocol_rt.event.throttle.throttle_input = 0;
    ship_protocol_rt.event.throttle.steering_input = 0;
    ship_protocol_clear_event_payload();
    ship_protocol_clear_point_event();
    ship_protocol_event_queue_reset();
    ship_protocol_last_tick_ms = ship_protocol_rt.tick_ms;
    ship_protocol_parse_index = 0U;
    ship_protocol_parse_expected_len = 0U;
    ship_protocol_parse_state = SHIP_PARSE_WAIT_HEAD;
    ship_protocol_power_init();
    ship_protocol_read_power_sample(&ship_protocol_rt.power_sample);
    if (ship_protocol_rt.power_sample.valid != 0U) {
        ship_protocol_rt.power_level = ship_protocol_rt.power_sample.report;
        ship_protocol_rt.power_first_valid_logged = 1U;
    }
    ShipControl_Init();
    AutoDrive_Init();
    ship_protocol_initialized = 1U;
    ship_protocol_log_power_sample(&ship_protocol_rt.power_sample, 1U);
#if (SHIP_PROTOCOL_VERBOSE_LOG_ENABLE != 0U)
    (void)seed;
#endif
}

void ship_protocol_run_scheduler(void)
{
    u8 step_link_state;
    u32 now_ms;

    if (ship_protocol_initialized == 0U) {
        ship_protocol_init();
    }
    now_ms = platform_scheduler_get_tick_ms();
    ship_protocol_poll_rx_frames();
    if ((now_ms - ship_protocol_last_tick_ms) < 10UL) {
        ShipControl_Tick(now_ms);
        return;
    }
    ship_protocol_last_tick_ms += 10UL;
    ship_protocol_rt.tick_ms = ship_protocol_last_tick_ms;
    AutoDrive_LinkAliveTick();
    ship_protocol_low_power_check();
    ship_protocol_check_timeouts();
    step_link_state = 1U;
    if (ship_protocol_rt.wait_ticks > 0U) {
        ship_protocol_rt.wait_ticks--;
        step_link_state = 0U;
    }
    if (step_link_state != 0U) {
        if (ship_protocol_rt.state == SHIP_PROTOCOL_STATE_BOOT_WAIT) {
            ship_protocol_rt.state = SHIP_PROTOCOL_STATE_PAIR_SEND;
        }
        switch (ship_protocol_rt.state) {
        case SHIP_PROTOCOL_STATE_PAIR_SEND:
            ship_protocol_step_pair_send();
            break;
        case SHIP_PROTOCOL_STATE_PAIR_WAIT_RSP:
        case SHIP_PROTOCOL_STATE_WORK_RX:
            ship_protocol_step_work_rx();
            break;
        default:
            ship_protocol_rt.state = SHIP_PROTOCOL_STATE_PAIR_SEND;
            break;
        }
    }
    AutoDrive_Poll();
    ship_protocol_service_autodrive_diag(now_ms);
    ShipControl_Tick(now_ms);
}

u8 ship_protocol_is_paired(void)
{
    return ship_protocol_rt.paired;
}

void ship_protocol_publish_spi_ps_frame(const u8 *buffer, u8 len, int8 status)
{
    u8 i;
    u8 stored_len;

    stored_len = len;
    if (stored_len > SHIP_PROTOCOL_SPI_PS_EVENT_DATA_MAX) {
        stored_len = SHIP_PROTOCOL_SPI_PS_EVENT_DATA_MAX;
    }
    ship_protocol_clear_event_payload();
    ship_protocol_rt.event.spi_ps.status = status;
    ship_protocol_rt.event.spi_ps.len = len;
    ship_protocol_rt.event.spi_ps.stored_len = stored_len;
    for (i = 0U; i < stored_len; i++) {
        ship_protocol_rt.event.spi_ps.bytes[i] = (buffer != 0) ? buffer[i] : 0U;
    }
    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX,
                                SHIP_PROTOCOL_EVENT_STATE_SPI_PS,
                                0U,
                                len);
}

u8 ship_protocol_get_event_snapshot(ship_protocol_event_snapshot_t *snapshot)
{
    if (snapshot == 0) {
        return 0U;
    }
    *snapshot = ship_protocol_rt.event;
    return ship_protocol_rt.event.pending;
}

u8 ship_protocol_take_event(ship_protocol_event_snapshot_t *snapshot)
{
    if ((snapshot == 0) || (ship_protocol_rt.event_queue_count == 0U)) {
        return 0U;
    }
    *snapshot = ship_protocol_rt.event_queue[ship_protocol_rt.event_queue_head];
    ship_protocol_rt.event_queue[ship_protocol_rt.event_queue_head].pending = 0U;
    ship_protocol_rt.event_queue_head++;
    if (ship_protocol_rt.event_queue_head >= SHIP_PROTOCOL_EVENT_QUEUE_DEPTH) {
        ship_protocol_rt.event_queue_head = 0U;
    }
    ship_protocol_rt.event_queue_count--;
    if (ship_protocol_rt.event_queue_count == 0U) {
        ship_protocol_rt.event.pending = 0U;
        ship_protocol_rt.event.state = SHIP_PROTOCOL_EVENT_STATE_IDLE;
        ship_protocol_rt.event.type = SHIP_PROTOCOL_EVENT_NONE;
        ship_protocol_rt.event.cmd = 0U;
        ship_protocol_rt.event.payload_len = 0U;
    }
    return 1U;
}
