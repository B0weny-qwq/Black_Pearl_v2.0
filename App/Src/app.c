#include "app_internal.h"
#include "app_extension.h"
#include "board_console.h"
#include "board_gps.h"
#include "board_motor.h"
#include "board_wireless.h"
#include "logger.h"
#include "north_calib.h"
#include "platform_scheduler.h"
#include "ship_protocol.h"

/* App 公开入口保持很薄：真实业务拆在同层小文件中，便于外包按区域接入。 */
void app_init(void)
{
    if (board_console_init() == BOARD_CONSOLE_OK) {
        log_init();
        LOGI("SYS", "fw bp2-magdiag-20260530");
    }

    app_bring_up_devices();
    app_extension_init();
}

void app_loop(void)
{
    board_gps_poll();
    (void)board_wireless_poll();
    ship_protocol_run_scheduler();
    (void)board_wireless_search_signal_poll();
    app_spi_ps_poll();
    app_ship_event_poll();
    app_extension_poll(platform_scheduler_get_tick_ms());
    app_mag_observe_poll();
    app_ahrs_poll();
    (void)board_motor_service();
}

const AHRS_State_t *app_get_attitude_state(void)
{
    return AHRS_GetState();
}

u8 app_get_heading_ready(void)
{
    return app_heading_ready;
}

u16 app_get_heading_deg100(void)
{
    if (app_heading_ready == 0U) {
        return 0U;
    }
    return app_wrap_heading_deg100((int32)app_raw_heading_deg100 +
                                   (int32)NorthCalib_GetHeadingOffsetCd());
}

u16 app_get_raw_heading_deg100(void)
{
    return app_raw_heading_deg100;
}

int16 app_get_heading_relative_deg100(void)
{
    return app_heading_rel_deg100;
}
