#include "app_internal.h"
#include "app_extension.h"
#include "board_spi_ps.h"
#include "logger.h"
#include "platform_scheduler.h"
#include "ship_protocol.h"

/* AHRS 未 ready 时也独立观察地磁，便于现场判断磁力计链路。 */
void app_mag_observe_poll(void)
{
#if (SHIP_MAG_STANDALONE_LOG_ENABLE != 0U)
    static u32 last_ms = 0UL;
    board_mag_sample_t mag_sample;
    u32 now_ms;
    int8 ret;

    if (board_mag_is_ready() == 0U) {
        return;
    }
    now_ms = platform_scheduler_get_tick_ms();
    if ((now_ms - last_ms) < SHIP_MAG_STANDALONE_LOG_PERIOD_MS) {
        return;
    }
    last_ms = now_ms;
    ret = board_mag_read(&mag_sample);
    if (ret == BOARD_MAG_OK) {
        app_record_mag_sample(&mag_sample, now_ms);
    } else if (app_mag_error_latched == 0U) {
        app_log_mag_read_fail(ret);
        app_mag_error_latched = 1U;
    }
#endif
}

void app_spi_ps_poll(void)
{
    u8 buffer[APP_SPI_PS_RX_EVENT_MAX];
    u8 len;
    int8 ret;

    if (app_spi_ps_ready == 0U) {
        return;
    }
    if (board_spi_ps_service() == 0U) {
        return;
    }

    len = 0U;
    ret = board_spi_ps_read(buffer, APP_SPI_PS_RX_EVENT_MAX, &len);
    if ((ret == BOARD_SPI_PS_OK) || (ret == BOARD_SPI_PS_ERR_OVERFLOW)) {
        ship_protocol_publish_spi_ps_frame(buffer, len, ret);
    }
}

static const char *app_ship_event_name(ship_protocol_event_type_t type)
{
    switch (type) {
    case SHIP_PROTOCOL_EVENT_THROTTLE:
        return "thr";
    case SHIP_PROTOCOL_EVENT_KEY_EDGE:
        return "key";
    case SHIP_PROTOCOL_EVENT_RETURN_HOME:
        return "ret";
    case SHIP_PROTOCOL_EVENT_FISH_POINT:
        return "fish";
    case SHIP_PROTOCOL_EVENT_RETURN_SWITCH:
        return "sw";
    case SHIP_PROTOCOL_EVENT_KEY_ACTION:
        return "act";
    case SHIP_PROTOCOL_EVENT_POWER_SAMPLE:
        return "pwr";
    case SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED:
        return "plvl";
    case SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED:
        return "low";
    case SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX:
        return "spi";
    case SHIP_PROTOCOL_EVENT_FRAME_ERROR:
        return "err";
    default:
        return "none";
    }
}

static void app_dispatch_ship_event(const ship_protocol_event_snapshot_t *event)
{
    if ((event == 0) || (event->type == SHIP_PROTOCOL_EVENT_NONE)) {
        return;
    }

    switch (event->type) {
    case SHIP_PROTOCOL_EVENT_KEY_EDGE:
        LOGI("EVT", "%s seq=%u key=%02X in=%d/%d",
             app_ship_event_name(event->type),
             event->sequence,
             (u16)event->throttle.key_event,
             event->throttle.throttle_input,
             event->throttle.steering_input);
        break;
    case SHIP_PROTOCOL_EVENT_KEY_ACTION:
        LOGI("EVT", "%s seq=%u act=%u key=%02X",
             app_ship_event_name(event->type),
             event->sequence,
             (u16)event->key_action,
             (u16)event->throttle.key_event);
        break;
    case SHIP_PROTOCOL_EVENT_RETURN_HOME:
    case SHIP_PROTOCOL_EVENT_FISH_POINT:
    case SHIP_PROTOCOL_EVENT_RETURN_SWITCH:
        LOGI("EVT", "%s seq=%u cmd=%02X pt=%u sw=%02X",
             app_ship_event_name(event->type),
             event->sequence,
             (u16)event->cmd,
             (u16)event->point_valid,
             (u16)event->switch_state);
        break;
    case SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED:
    case SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED:
        LOGI("EVT", "%s seq=%u lvl=%u bat=%lu v=%u",
             app_ship_event_name(event->type),
             event->sequence,
             (u16)event->power.level,
             event->power.bat_mv,
             (u16)event->power.valid);
        break;
    case SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX:
        LOGI("EVT", "%s seq=%u rc=%d len=%u st=%u",
             app_ship_event_name(event->type),
             event->sequence,
             event->spi_ps.status,
             (u16)event->spi_ps.len,
             (u16)event->spi_ps.stored_len);
        break;
    case SHIP_PROTOCOL_EVENT_FRAME_ERROR:
        LOGW("EVT", "%s seq=%u cmd=%02X len=%u",
             app_ship_event_name(event->type),
             event->sequence,
             (u16)event->cmd,
             (u16)event->payload_len);
        break;
    default:
        break;
    }

    app_extension_on_ship_event(event);
}

void app_ship_event_poll(void)
{
    ship_protocol_event_snapshot_t event;

    while (ship_protocol_take_event(&event) != 0U) {
        app_dispatch_ship_event(&event);
    }
}
