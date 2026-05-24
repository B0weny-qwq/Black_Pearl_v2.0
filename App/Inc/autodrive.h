/**
 * @file autodrive.h
 * @brief GPS 返航/定点自动驾驶状态机接口。
 *
 * 协议层通过本接口提交返航点、钓点和返航开关命令；本模块负责点位表、
 * 目标航向、对齐阶段、运行阶段和诊断快照。
 */

#ifndef __AUTODRIVE_H__
#define __AUTODRIVE_H__

#include "type_def.h"

typedef enum
{
    AUTO_DRIVE_IDLE = 0,
    AUTO_DRIVE_START,
    AUTO_DRIVE_ALIGN,
    AUTO_DRIVE_RUN,
    AUTO_DRIVE_ARRIVE,
    AUTO_DRIVE_REJECT,
    AUTO_DRIVE_TIMEOUT,
    AUTO_DRIVE_STOP
} AutoDrive_State_t;

typedef enum
{
    AUTO_DRIVE_CLOSE = 0,
    AUTO_DRIVE_GO_FISISH_POSITION,
    AUTO_DRIVE_GO_HOME_POSITION
} AutoDrive_Mode_t;

typedef enum
{
    POSITION_NORTH = 1,
    POSITION_EAST_NORTH = 2,
    POSITION_EAST = 3,
    POSITION_EAST_SOUTH = 4,
    POSITION_SOUTH = 5,
    POSITION_WEST_SOUTH = 6,
    POSITION_WEST = 7,
    POSITION_WEST_NORTH = 8
} AutoDrive_Direction_t;

typedef struct
{
    u8 lon_ew;
    u16 lon_whole;
    u16 lon_frac;
    u8 lat_ns;
    u16 lat_whole;
    u16 lat_frac;
} AutoDrive_PointRaw_t;

#define AUTODRIVE_LEGACY_POINT_WIRE_LEN 10U
#define AUTODRIVE_FISH_POINT_COUNT      5U

#define AUTODRIVE_FISH_CMD_BUSY            0U
#define AUTODRIVE_FISH_CMD_STORED          1U
#define AUTODRIVE_FISH_CMD_DUP_WAIT        2U
#define AUTODRIVE_FISH_CMD_REJECT_UNKNOWN  3U
#define AUTODRIVE_FISH_CMD_REJECT_DISTANCE 4U
#define AUTODRIVE_FISH_CMD_STARTED         5U
#define AUTODRIVE_FISH_CMD_INVALID         6U

typedef struct
{
    u8 auto_ret_onoff;
    AutoDrive_PointRaw_t ret_point;
} AutoDrive_ReturnConfig_t;

typedef struct
{
    AutoDrive_PointRaw_t point[AUTODRIVE_FISH_POINT_COUNT];
    u8 valid_mask;
    u8 next_index;
    u8 latest_index;
} AutoDrive_FishPointStore_t;

typedef enum
{
    AUTODRIVE_DIAG_REASON_NONE = 0,
    AUTODRIVE_DIAG_REASON_CMD_RETURN_HOME = 1,
    AUTODRIVE_DIAG_REASON_CMD_GOTO_POINT = 2,
    AUTODRIVE_DIAG_REASON_RETURN_SWITCH_SAVE = 3,
    AUTODRIVE_DIAG_REASON_LINK_TIMEOUT = 4,
    AUTODRIVE_DIAG_REASON_LOW_POWER = 5,
    AUTODRIVE_DIAG_REASON_GENERIC_TRIGGER = 6,
    AUTODRIVE_DIAG_REASON_ARRIVE = 7,
    AUTODRIVE_DIAG_REASON_OVERTIME = 8,
    AUTODRIVE_DIAG_REASON_STOP = 9
} AutoDrive_DiagReason_t;

typedef struct
{
    u8 state;
    u8 mode;
    u8 auto_ret_onoff;
    u8 fail_flag;
    u8 last_reason;
    u8 gps_ready;
    u8 sat_count;
    u8 can_activate_target;
    u16 distance_to_target_m;
    u16 current_heading_deg;
    u16 target_heading_deg;
    AutoDrive_PointRaw_t current_point;
    AutoDrive_PointRaw_t target_point;
} AutoDrive_DebugSnapshot_t;

void AutoDrive_Init(void);
void AutoDrive_Poll(void);
void AutoDrive_Stop(void);
void AutoDrive_StopMotion(void);
void AutoDrive_TriggerReturn(void);
void AutoDrive_TriggerReturnWithReason(u8 reason);
void AutoDrive_WorkOvertimeFail(void);

void AutoDrive_SetMode(u8 mode);
u8 AutoDrive_GetMode(void);
u8 AutoDrive_InActive(void);
u8 AutoDrive_IsBusy(void);
u8 AutoDrive_IsCanActive(const AutoDrive_PointRaw_t *point);

void AutoDrive_SetReturnPositionRaw(const u8 *data_m);
u8 AutoDrive_SetFishPositionRaw(const u8 *data_m);
void AutoDrive_SetSwitchRaw(const u8 *data_m, u8 len);
void AutoDrive_GetStoredConfig(AutoDrive_ReturnConfig_t *cfg);
void AutoDrive_GetCurrentPointRaw(AutoDrive_PointRaw_t *point);
u8 AutoDrive_GetReturnPositionRaw(AutoDrive_PointRaw_t *point);
u8 AutoDrive_GetFishPositionRaw(AutoDrive_PointRaw_t *point);
u8 AutoDrive_GetFishPositionByIndexRaw(u8 index, AutoDrive_PointRaw_t *point);
u8 AutoDrive_GetLastFishCommandIndex(void);
void AutoDrive_GetDebugSnapshot(AutoDrive_DebugSnapshot_t *snapshot);

u16 AutoDrive_GetDistanceNowToDestination(const u8 *nowpositionData,
                                          const u8 *despositionData);
u16 AutoDrive_GetAngelNowToDestination(const u8 *nowpositionData,
                                       const u8 *despositionData);
u16 AutoDrive_GetNorthAngel(u8 direction, u8 angel);
u8 AutoDrive_GetDirectionNowToDestination(const u8 *nowpositionData,
                                          const u8 *despositionData);

void AutoDrive_LinkAliveTick(void);
void AutoDrive_LinkAliveKick(void);

#endif
