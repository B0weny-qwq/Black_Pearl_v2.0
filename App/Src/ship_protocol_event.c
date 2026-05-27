#include "ship_protocol_internal.h"
#include "logger.h"

/* 协议事件通过环形队列交给 App 扩展和联调日志观察。 */
void ship_protocol_clear_spi_ps_event(void)
{
    u8 i;

    ship_protocol_rt.event.spi_ps.status = 0;
    ship_protocol_rt.event.spi_ps.len = 0U;
    ship_protocol_rt.event.spi_ps.stored_len = 0U;
    for (i = 0U; i < SHIP_PROTOCOL_SPI_PS_EVENT_DATA_MAX; i++) {
        ship_protocol_rt.event.spi_ps.bytes[i] = 0U;
    }
}

void ship_protocol_clear_event_payload(void)
{
    ship_protocol_rt.event.switch_state = 0U;
    ship_protocol_rt.event.point_valid = 0U;
    ship_protocol_rt.event.key_action = SHIP_PROTOCOL_KEY_ACTION_NONE;
    ship_protocol_rt.event.power.raw = 0U;
    ship_protocol_rt.event.power.adc_mv = 0U;
    ship_protocol_rt.event.power.bat_mv = 0UL;
    ship_protocol_rt.event.power.level = SHIP_POWER_LEVEL_0;
    ship_protocol_rt.event.power.valid = 0U;
    ship_protocol_clear_spi_ps_event();
}

void ship_protocol_fill_power_event(const ship_protocol_power_sample_t *sample)
{
    if (sample == 0) {
        ship_protocol_rt.event.power.raw = 0U;
        ship_protocol_rt.event.power.adc_mv = 0U;
        ship_protocol_rt.event.power.bat_mv = 0UL;
        ship_protocol_rt.event.power.level = ship_protocol_rt.power_level;
        ship_protocol_rt.event.power.valid = 0U;
        return;
    }
    ship_protocol_rt.event.power.raw = sample->raw;
    ship_protocol_rt.event.power.adc_mv = sample->adc_mv;
    ship_protocol_rt.event.power.bat_mv = sample->bat_mv;
    ship_protocol_rt.event.power.level = sample->report;
    ship_protocol_rt.event.power.valid = sample->valid;
}

void ship_protocol_event_queue_reset(void)
{
    u8 i;

    ship_protocol_rt.event_queue_head = 0U;
    ship_protocol_rt.event_queue_tail = 0U;
    ship_protocol_rt.event_queue_count = 0U;
    ship_protocol_rt.event_queue_dropped = 0U;
    for (i = 0U; i < SHIP_PROTOCOL_EVENT_QUEUE_DEPTH; i++) {
        ship_protocol_rt.event_queue[i].type = SHIP_PROTOCOL_EVENT_NONE;
        ship_protocol_rt.event_queue[i].state = SHIP_PROTOCOL_EVENT_STATE_IDLE;
        ship_protocol_rt.event_queue[i].pending = 0U;
    }
}

static void ship_protocol_event_queue_push(const ship_protocol_event_snapshot_t *event)
{
    if (event == 0) {
        return;
    }
    if (ship_protocol_rt.event_queue_count >= SHIP_PROTOCOL_EVENT_QUEUE_DEPTH) {
        ship_protocol_rt.event_queue_head++;
        if (ship_protocol_rt.event_queue_head >= SHIP_PROTOCOL_EVENT_QUEUE_DEPTH) {
            ship_protocol_rt.event_queue_head = 0U;
        }
        ship_protocol_rt.event_queue_count--;
        ship_protocol_rt.event_queue_dropped++;
    }
    ship_protocol_rt.event_queue[ship_protocol_rt.event_queue_tail] = *event;
    ship_protocol_rt.event_queue_tail++;
    if (ship_protocol_rt.event_queue_tail >= SHIP_PROTOCOL_EVENT_QUEUE_DEPTH) {
        ship_protocol_rt.event_queue_tail = 0U;
    }
    ship_protocol_rt.event_queue_count++;
}

void ship_protocol_publish_event(ship_protocol_event_type_t type,
                                 ship_protocol_event_state_t state,
                                 u8 cmd,
                                 u8 payload_len)
{
    ship_protocol_rt.event.type = type;
    ship_protocol_rt.event.state = state;
    ship_protocol_rt.event.cmd = cmd;
    ship_protocol_rt.event.payload_len = payload_len;
    ship_protocol_rt.event.pending = 1U;
    ship_protocol_rt.event.sequence++;
    ship_protocol_rt.event.tick_ms = ship_protocol_rt.tick_ms;
    ship_protocol_event_queue_push(&ship_protocol_rt.event);
}

void ship_protocol_publish_error_event(u8 cmd, u8 payload_len)
{
    ship_protocol_clear_event_payload();
    ship_protocol_clear_point_event();
    ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_FRAME_ERROR,
                                SHIP_PROTOCOL_EVENT_STATE_ERROR,
                                cmd,
                                payload_len);
}

void ship_protocol_parse_point_payload(const u8 *payload, ship_protocol_point_t *point)
{
    if ((payload == 0) || (point == 0)) {
        return;
    }
    point->lon_ew = payload[0];
    point->lon_whole = ship_protocol_read_u16_be(&payload[1]);
    point->lon_frac = ship_protocol_read_u16_be(&payload[3]);
    point->lat_ns = payload[5];
    point->lat_whole = ship_protocol_read_u16_be(&payload[6]);
    point->lat_frac = ship_protocol_read_u16_be(&payload[8]);
}

void ship_protocol_clear_point_event(void)
{
    ship_protocol_rt.event.point.lon_ew = 0U;
    ship_protocol_rt.event.point.lon_whole = 0U;
    ship_protocol_rt.event.point.lon_frac = 0U;
    ship_protocol_rt.event.point.lat_ns = 0U;
    ship_protocol_rt.event.point.lat_whole = 0U;
    ship_protocol_rt.event.point.lat_frac = 0U;
    ship_protocol_rt.event.point_valid = 0U;
}

void ship_protocol_log_point(const char *label, const ship_protocol_point_t *point)
{
    if (point == 0) {
        return;
    }
    LOGI(SHIP_TAG, "%s ew=0x%02X lon=%u.%u ns=0x%02X lat=%u.%u",
         label,
         (u16)point->lon_ew,
         point->lon_whole,
         point->lon_frac,
         (u16)point->lat_ns,
         point->lat_whole,
         point->lat_frac);
}

int16 ship_protocol_raw_ud_to_input(u8 front_back)
{
    return (int16)((int16)front_back - (int16)SHIP_AXIS_CENTER);
}

int16 ship_protocol_raw_lr_to_input(u8 left_right)
{
    return (int16)((int16)left_right - (int16)SHIP_AXIS_CENTER);
}

ship_protocol_key_action_t ship_protocol_key_to_action(u8 key)
{
    switch (key) {
    case SHIP_PROTOCOL_KEY_B_UNUSED:
        return SHIP_PROTOCOL_KEY_ACTION_B_NOOP;
    case SHIP_PROTOCOL_KEY_C_UNUSED:
        return SHIP_PROTOCOL_KEY_ACTION_C_NOOP;
    case SHIP_PROTOCOL_KEY_D_UNUSED:
        return SHIP_PROTOCOL_KEY_ACTION_D_NOOP;
    default:
        return SHIP_PROTOCOL_KEY_ACTION_NONE;
    }
}
