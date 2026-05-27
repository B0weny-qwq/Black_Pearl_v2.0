#include "ship_protocol_internal.h"
#include "autodrive.h"
#include "logger.h"
#include "ship_control.h"

/* 电源链路：BoardDevices 采样 -> 旧 0..4 电量等级 -> 日志/事件/0x12。 */
void ship_protocol_power_init(void)
{
    ship_protocol_rt.power_adc_ready =
        (board_power_init() == BOARD_POWER_OK) ? 1U : 0U;
}

void ship_protocol_read_power_sample(ship_protocol_power_sample_t *sample)
{
    board_power_sample_t board_sample;

    if (sample == 0) {
        return;
    }
    *sample = ship_protocol_rt.power_sample;
    sample->report = ship_protocol_rt.power_level;
    sample->sampled = 1U;
    sample->status = board_power_read(&board_sample);
    if (sample->status != BOARD_POWER_OK) {
        sample->raw = board_sample.raw;
        sample->adc_mv = board_sample.adc_mv;
        sample->bat_mv = board_sample.bat_mv;
        sample->report = board_sample.level;
        sample->valid = 0U;
        return;
    }
    sample->raw = board_sample.raw;
    sample->adc_mv = board_sample.adc_mv;
    sample->bat_mv = board_sample.bat_mv;
    sample->report = board_sample.level;
    sample->valid = board_sample.valid;
}

static void ship_protocol_service_power_sample(void)
{
    u8 previous_level;

    if (ship_protocol_rt.power_sample_divider_count < SHIP_POWER_SAMPLE_DIVIDER) {
        ship_protocol_rt.power_sample_divider_count++;
        return;
    }
    ship_protocol_rt.power_sample_divider_count = 0U;
    previous_level = ship_protocol_rt.power_level;
    ship_protocol_read_power_sample(&ship_protocol_rt.power_sample);
    if (ship_protocol_rt.power_sample.valid == 0U) {
        return;
    }
    ship_protocol_rt.power_level = ship_protocol_rt.power_sample.report;
    if (ship_protocol_rt.power_first_valid_logged == 0U) {
        ship_protocol_log_power_sample(&ship_protocol_rt.power_sample, 1U);
        ship_protocol_rt.power_first_valid_logged = 1U;
    }
    ship_protocol_clear_event_payload();
    ship_protocol_fill_power_event(&ship_protocol_rt.power_sample);
    ship_protocol_publish_event((ship_protocol_rt.power_level != previous_level) ?
                                SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED :
                                SHIP_PROTOCOL_EVENT_POWER_SAMPLE,
                                SHIP_PROTOCOL_EVENT_STATE_POWER,
                                0U,
                                0U);
}

void ship_protocol_log_power_sample(const ship_protocol_power_sample_t *sample, u8 force_log)
{
#if SHIP_ADC_LOG_ENABLE
    static u32 last_log_ms = 0UL;

    if (sample == 0) {
        return;
    }
    if ((force_log == 0U) &&
        (SHIP_POWER_LOG_PERIOD_MS != 0UL) &&
        ((ship_protocol_rt.tick_ms - last_log_ms) < SHIP_POWER_LOG_PERIOD_MS)) {
        return;
    }
    last_log_ms = ship_protocol_rt.tick_ms;
    if (sample->sampled == 0U) {
        LOGW(SHIP_TAG, "adc pending p=%u", (u16)sample->report);
        return;
    }
    if (sample->valid == 0U) {
        LOGW(SHIP_TAG, "adc not-ready rc=%d raw=%u p=%u",
             sample->status,
             sample->raw,
             (u16)sample->report);
        return;
    }
    LOGI(SHIP_TAG, "adc raw=%u mv=%u bat=%lu p=%u",
         sample->raw,
         sample->adc_mv,
         sample->bat_mv,
         (u16)sample->report);
#else
    (void)sample;
    (void)force_log;
#endif
}

void ship_protocol_low_power_check(void)
{
    ship_protocol_service_power_sample();
    ship_protocol_log_power_sample(&ship_protocol_rt.power_sample, 0U);
    if (ship_protocol_rt.power_sample.valid == 0U) {
        ship_protocol_rt.lowpower_check_ticks = 0U;
        return;
    }
    if (ship_protocol_rt.power_level != SHIP_POWER_LEVEL_0) {
        ship_protocol_rt.lowpower_check_ticks = 0U;
        ship_protocol_rt.lowpower_return_latched = 0U;
        return;
    }
    if (ship_protocol_rt.lowpower_check_ticks < 0xFFFFU) {
        ship_protocol_rt.lowpower_check_ticks++;
    }
    if ((ship_protocol_rt.lowpower_return_latched == 0U) &&
        (ship_protocol_rt.lowpower_check_ticks > SHIP_LOWPOWER_CHECK_TICKS) &&
        (AutoDrive_GetMode() == AUTO_DRIVE_CLOSE) &&
        (ShipControl_GetManualAccelerator() < SHIP_LOWPOWER_ACCEL_MAX)) {
        ship_protocol_rt.lowpower_return_latched = 1U;
        ship_protocol_clear_event_payload();
        ship_protocol_fill_power_event(&ship_protocol_rt.power_sample);
        ship_protocol_publish_event(SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED,
                                    SHIP_PROTOCOL_EVENT_STATE_POWER,
                                    0U,
                                    0U);
        LOGW(SHIP_TAG, "low power latched ticks=%u p=%u",
             ship_protocol_rt.lowpower_check_ticks,
             (u16)ship_protocol_rt.power_level);
        AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_LOW_POWER);
    }
}
