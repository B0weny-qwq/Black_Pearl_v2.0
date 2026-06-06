#include "autodrive_internal.h"
#include "autodrive_config.h"
#include "platform_scheduler.h"

u8 AutoDrive_IsCanActive(const AutoDrive_PointRaw_t *point)
{
    AutoDrive_PointRaw_t current_point;
    u16 distance;

    if (autodrive_state != AUTO_DRIVE_IDLE) {
        return 0U;
    }
    if (AutoDrive_PointRawValid(point) == 0U) {
        return 0U;
    }
    if (AutoDrive_GetReadyCurrentPoint(&current_point) == 0U) {
        return 0U;
    }
    distance = AutoDrive_GetDistanceNowToDestination((const u8 *)point,
                                                     (const u8 *)&current_point);
    if ((distance > AUTODRIVE_MIN_ACTIVE_DISTANCE_M) &&
        (distance < AUTODRIVE_MAX_ACTIVE_DISTANCE_M)) {
        AutoDrive_CopyPoint(&idle_position, &current_point);
        return 1U;
    }
    return 0U;
}

void AutoDrive_SetReturnPositionRaw(const u8 *data_m)
{
    AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_CMD_RETURN_HOME);
    if ((data_m == 0) || (autodrive_state != AUTO_DRIVE_IDLE)) {
        return;
    }

    /* 0x13 返航点沿用旧协议 10 字节坐标，并同步保存到配置区。 */
    AutoDrive_PointFromLegacyWire(&return_position, data_m);
    if (AutoDrive_PointRawValid(&return_position) == 0U) {
        autodrive_state = AUTO_DRIVE_REJECT;
        AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
        return;
    }
    AutoDrive_CopyPoint(&autodrv_cfg.ret_point, &return_position);
    (void)AutoDriveCfg_Save(&autodrv_cfg);
    if (AutoDrive_IsCanActive(&return_position) == 0U) {
        autodrive_state = AUTO_DRIVE_REJECT;
        AutoDrive_SetMode(AUTO_DRIVE_CLOSE);
        return;
    }

    AutoDrive_SetMode(AUTO_DRIVE_GO_HOME_POSITION);
    autodrive_state = AUTO_DRIVE_START;
    autodrive_work_overtime = AUTODRIVE_WORK_OVERTIME;
    autodrive_fail_flag = 0U;
}

u8 AutoDrive_SetFishPositionRaw(const u8 *data_m)
{
    AutoDrive_PointRaw_t rx_point;
    u32 now_ms;

    AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_CMD_GOTO_POINT);
    last_fish_cmd_index = 0U;
    last_fish_rx_result = AUTODRIVE_FISH_RX_NONE;
    if (autodrive_state != AUTO_DRIVE_IDLE) {
        last_fish_rx_result = AUTODRIVE_FISH_RX_BUSY;
        return AUTODRIVE_FISH_CMD_BUSY;
    }
    if (data_m == 0) {
        last_fish_rx_result = AUTODRIVE_FISH_RX_INVALID;
        return AUTODRIVE_FISH_CMD_INVALID;
    }

    AutoDrive_PointFromLegacyWire(&rx_point, data_m);
    if (AutoDrive_PointRawValid(&rx_point) == 0U) {
        last_fish_rx_result = AUTODRIVE_FISH_RX_INVALID;
        return AUTODRIVE_FISH_CMD_INVALID;
    }

    now_ms = platform_scheduler_get_tick_ms();
    if (AutoDrive_IsFishRxDuplicate(&rx_point, now_ms) != 0U) {
        last_fish_cmd_index = 1U;
        last_fish_rx_result = AUTODRIVE_FISH_RX_EXISTS;
        AutoDrive_RecordFishRxPoint(&rx_point, now_ms);
        return AUTODRIVE_FISH_CMD_DUP_WAIT;
    }

    AutoDrive_RecordFishRxPoint(&rx_point, now_ms);
    AutoDrive_CopyPoint(&fish_position, &rx_point);
    last_fish_cmd_index = 1U;
    last_fish_rx_result = AUTODRIVE_FISH_RX_STORED;
    if (AutoDrive_IsCanActive(&fish_position) == 0U) {
        return AUTODRIVE_FISH_CMD_REJECT_DISTANCE;
    }

    AutoDrive_SetMode(AUTO_DRIVE_GO_FISISH_POSITION);
    autodrive_state = AUTO_DRIVE_START;
    autodrive_work_overtime = AUTODRIVE_WORK_OVERTIME;
    autodrive_fail_flag = 0U;
    return AUTODRIVE_FISH_CMD_STARTED;
}

void AutoDrive_TriggerReturnWithReason(u8 reason)
{
    AutoDrive_SetDiagReason(reason);
    if (autodrv_cfg.auto_ret_onoff != 0x30U) {
        if ((autodrive_fail_flag != 0U) || (autodrive_state != AUTO_DRIVE_IDLE)) {
            return;
        }
        if (AutoDrive_IsCanActive(&autodrv_cfg.ret_point) != 0U) {
            AutoDrive_CopyPoint(&return_position, &autodrv_cfg.ret_point);
            AutoDrive_SetMode(AUTO_DRIVE_GO_HOME_POSITION);
            autodrive_state = AUTO_DRIVE_START;
            autodrive_work_overtime = AUTODRIVE_WORK_OVERTIME;
        }
    }
}

void AutoDrive_TriggerReturn(void)
{
    AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_GENERIC_TRIGGER);
}

void AutoDrive_SetSwitchRaw(const u8 *data_m, u8 len)
{
    if ((data_m == 0) || (len == 0U)) {
        return;
    }
    autodrive_switch = data_m[0];
    autodrv_cfg.auto_ret_onoff = data_m[0];
    if (len >= (u8)(1U + AUTODRIVE_LEGACY_POINT_WIRE_LEN)) {
        AutoDrive_SetDiagReason(AUTODRIVE_DIAG_REASON_RETURN_SWITCH_SAVE);
        AutoDrive_PointFromLegacyWire(&autodrv_cfg.ret_point, &data_m[1]);
        if (AutoDrive_PointRawValid(&autodrv_cfg.ret_point) != 0U) {
            if (autodrive_state == AUTO_DRIVE_IDLE) {
                AutoDrive_CopyPoint(&return_position, &autodrv_cfg.ret_point);
            }
            (void)AutoDriveCfg_Save(&autodrv_cfg);
        }
    }
    if (autodrv_cfg.auto_ret_onoff != 0x30U) {
        AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_RETURN_SWITCH_SAVE);
    }
}

void AutoDrive_GetStoredConfig(AutoDrive_ReturnConfig_t *cfg)
{
    if (cfg != 0) {
        *cfg = autodrv_cfg;
    }
}

u8 AutoDrive_GetReturnPositionRaw(AutoDrive_PointRaw_t *point)
{
    if (point != 0) {
        AutoDrive_CopyPoint(point, &return_position);
    }
    return AutoDrive_PointRawValid(&return_position);
}

u8 AutoDrive_GetFishPositionRaw(AutoDrive_PointRaw_t *point)
{
    if (point != 0) {
        AutoDrive_CopyPoint(point, &fish_position);
    }
    return AutoDrive_PointRawValid(&fish_position);
}

u8 AutoDrive_GetFishPositionByIndexRaw(u8 index, AutoDrive_PointRaw_t *point)
{
    if (index != 1U) {
        return 0U;
    }
    if (point != 0) {
        AutoDrive_CopyPoint(point, &fish_position);
    }
    return AutoDrive_PointRawValid(&fish_position);
}

u8 AutoDrive_GetLastFishCommandIndex(void)
{
    return last_fish_cmd_index;
}

u8 AutoDrive_GetLastFishRxResult(void)
{
    return last_fish_rx_result;
}
