#include "autodrive_internal.h"
#include "autodrive_config.h"
#include "platform_scheduler.h"
#include "ship_control.h"

u8 autodrive_switch = 0U;
u8 autodrive_state = AUTO_DRIVE_IDLE;
u8 autodrive_mode = AUTO_DRIVE_CLOSE;
u16 autodrive_work_overtime = 0U;
u8 autodrive_fail_flag = 0U;

AutoDrive_PointRaw_t idle_position;
AutoDrive_PointRaw_t now_position;
AutoDrive_PointRaw_t last_position;
AutoDrive_PointRaw_t return_position;
AutoDrive_PointRaw_t fish_position;
AutoDrive_FishPointStore_t fish_points;
u8 last_fish_cmd_index = 0U;
u8 last_fish_save_result = AUTODRIVE_FISH_SAVE_NONE;
AutoDrive_PointRaw_t last_fish_rx_point;
u32 last_fish_rx_ms = 0UL;
u8 last_fish_rx_valid = 0U;
AutoDrive_ReturnConfig_t autodrv_cfg;

u8 destination_direction = POSITION_EAST;
u8 nowrun_direction = POSITION_EAST;
u16 destination_angle = 0U;
u16 autodrive_target_heading_cd = 0U;
u8 autodrive_target_heading_valid = 0U;
u16 autodrive_base_speed = AUTODRIVE_CRUISE_BASE_SPEED;
u16 autodrive_align_ticks = 0U;
u8 autodrive_align_stable_ticks = 0U;
u8 autodrive_align_zero_seen = 0U;
u8 autodrive_align_prev_valid = 0U;
int16 autodrive_align_prev_error_cd = 0;

u16 link_alive_ticks = 0U;
u16 link_close_ticks = 0U;
u32 last_run_update_seq = 0UL;
u32 last_poll_tick_ms = 0UL;
u32 last_link_tick_ms = 0UL;
u8 last_diag_reason = AUTODRIVE_DIAG_REASON_NONE;

void AutoDrive_SetDiagReason(u8 reason)
{
    last_diag_reason = reason;
}

void AutoDrive_StopMotion(void)
{
    autodrive_target_heading_valid = 0U;
    AutoDrive_ResetApproachTracker();
    AutoDrive_ResetAlignTracker();
    ShipControl_StopGpsNav();
}

void AutoDrive_Stop(void)
{
    autodrive_state = AUTO_DRIVE_IDLE;
    AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_STOP);
    AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
}

void AutoDrive_SetMode(u8 mode)
{
    if ((autodrive_mode != AUTO_DRIVE_CLOSE) && (mode == AUTO_DRIVE_CLOSE)) {
        AutoDrive_StopMotion();
    }
    autodrive_mode = mode;
    if (autodrive_mode == AUTO_DRIVE_CLOSE) {
        autodrive_state = AUTO_DRIVE_IDLE;
    }
}

u8 AutoDrive_GetMode(void)
{
    return autodrive_mode;
}

u8 AutoDrive_InActive(void)
{
    if (autodrive_state != AUTO_DRIVE_IDLE) {
        if (autodrive_mode == AUTO_DRIVE_GO_HOME_POSITION) {
            return 1U;
        }
        if (autodrive_mode == AUTO_DRIVE_GO_FISISH_POSITION) {
            return 2U;
        }
    }
    return 0U;
}

u8 AutoDrive_IsBusy(void)
{
    return (autodrive_state != AUTO_DRIVE_IDLE) ? 1U : 0U;
}

void AutoDrive_WorkOvertimeFail(void)
{
    autodrive_fail_flag = 1U;
    AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_OVERTIME);
}

u8 AutoDrive_TickWorkOvertime(void)
{
    if (autodrive_work_overtime > 0U) {
        autodrive_work_overtime--;
        return 1U;
    }
    AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
    AutoDrive_WorkOvertimeFail();
    autodrive_state = AUTO_DRIVE_TIMEOUT;
    return 0U;
}

void AutoDrive_Init(void)
{
    AutoDriveCfg_Init();
    AutoDriveCfg_Load(&autodrv_cfg);
    if (autodrv_cfg.auto_ret_onoff == 0xFFU) {
        autodrv_cfg.auto_ret_onoff = 0x30U;
    }
    AutoDrive_CopyPoint(&return_position, &autodrv_cfg.ret_point);
    AutoDrive_ClearPoint(&fish_position);
    AutoDrive_ClearFishPoints();
    last_fish_cmd_index = 0U;
    last_fish_save_result = AUTODRIVE_FISH_SAVE_NONE;
    AutoDrive_ClearPoint(&last_fish_rx_point);
    last_fish_rx_ms = 0UL;
    last_fish_rx_valid = 0U;

    /* 运行态复位保留旧工程语义：模式关闭、链路计数清零、运动输出释放。 */
    autodrive_switch = autodrv_cfg.auto_ret_onoff;
    autodrive_state = AUTO_DRIVE_IDLE;
    autodrive_mode = AUTO_DRIVE_CLOSE;
    autodrive_work_overtime = 0U;
    autodrive_fail_flag = 0U;
    link_alive_ticks = 0U;
    link_close_ticks = 0U;
    last_run_update_seq = 0UL;
    last_poll_tick_ms = platform_scheduler_get_tick_ms();
    last_link_tick_ms = last_poll_tick_ms;
    last_diag_reason = AUTODRIVE_DIAG_REASON_NONE;
    autodrive_target_heading_cd = 0U;
    autodrive_target_heading_valid = 0U;
    AutoDrive_ResetAlignTracker();
    AutoDrive_ResetApproachTracker();
    AutoDrive_StopMotion();
}

void AutoDrive_LinkAliveKick(void)
{
    link_alive_ticks = 0U;
    link_close_ticks = AUTODRIVE_MANUAL_CLOSE_TICKS;
}

void AutoDrive_LinkAliveTick(void)
{
    u32 now_ms;

    now_ms = platform_scheduler_get_tick_ms();
    if ((now_ms - last_link_tick_ms) < 10U) {
        return;
    }
    last_link_tick_ms = now_ms;

    /* 遥控链路超时后按旧工程配置尝试自动返航。 */
    if (link_alive_ticks < AUTODRIVE_MANUAL_TIMEOUT_TICKS) {
        link_alive_ticks++;
    } else {
        AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_LINK_TIMEOUT);
        link_alive_ticks = 0U;
    }

    if (link_close_ticks > 0U) {
        link_close_ticks--;
        if ((link_close_ticks == 0U) && (AutoDrive_GetMode() == AUTO_DRIVE_CLOSE)) {
            AutoDrive_StopMotion();
        }
    }
}
