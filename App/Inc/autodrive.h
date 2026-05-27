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

/**
 * @brief 初始化自动驾驶状态机。
 *
 * 函数会加载返航配置、清空钓点缓存、复位链路超时计数和目标航向状态。
 * 不直接访问底层 GPS/UART/电机硬件，运动输出通过 ShipControl 请求完成。
 */
void AutoDrive_Init(void);

/**
 * @brief 推进自动驾驶状态机。
 *
 * 调用者应在主循环中高频调用；函数内部按约 10 ms 节拍处理 GPS 点位、
 * 航向对准、路径运行、到达判断和超时保护。
 */
void AutoDrive_Poll(void);

/**
 * @brief 停止自动驾驶并关闭当前模式。
 *
 * 会记录停止原因并调用 AutoDrive_SetMode(AUTO_DRIVE_CLOSE)，最终释放
 * GPS 导航运动输出。
 */
void AutoDrive_Stop(void);

/**
 * @brief 仅停止当前自动驾驶运动输出。
 *
 * 清除目标航向、对齐/接近状态并通知 ShipControl 退出 GPS 导航；
 * 不修改已保存的返航点和钓点。
 */
void AutoDrive_StopMotion(void);

/** @brief 使用通用原因触发自动返航。 */
void AutoDrive_TriggerReturn(void);

/**
 * @brief 使用指定诊断原因触发自动返航。
 * @param reason AutoDrive_DiagReason_t 诊断原因码。
 */
void AutoDrive_TriggerReturnWithReason(u8 reason);

/** @brief 标记自动驾驶因工作超时失败，供诊断和返航保护使用。 */
void AutoDrive_WorkOvertimeFail(void);

/**
 * @brief 设置自动驾驶模式。
 * @param mode AutoDrive_Mode_t 模式值。
 */
void AutoDrive_SetMode(u8 mode);

/**
 * @brief 获取当前自动驾驶模式。
 * @return AutoDrive_Mode_t 模式值。
 */
u8 AutoDrive_GetMode(void);

/**
 * @brief 获取当前是否处于活动自动驾驶任务。
 * @return 0 空闲；1 正在返航；2 正在前往钓点。
 */
u8 AutoDrive_InActive(void);

/**
 * @brief 查询自动驾驶状态机是否非空闲。
 * @return 1 忙，0 空闲。
 */
u8 AutoDrive_IsBusy(void);

/**
 * @brief 判断目标点是否满足启动条件。
 * @param point 目标点，不能为 NULL。
 * @return 1 当前 GPS 和距离条件允许启动，0 不允许。
 */
u8 AutoDrive_IsCanActive(const AutoDrive_PointRaw_t *point);

/**
 * @brief 从旧协议 10 字节点位载荷设置返航点并尝试启动返航。
 * @param data_m 旧协议点位数据，长度必须至少为 AUTODRIVE_LEGACY_POINT_WIRE_LEN。
 */
void AutoDrive_SetReturnPositionRaw(const u8 *data_m);

/**
 * @brief 从旧协议 10 字节点位载荷接收钓点命令。
 * @param data_m 旧协议点位数据，长度必须至少为 AUTODRIVE_LEGACY_POINT_WIRE_LEN。
 * @return AUTODRIVE_FISH_CMD_* 命令处理结果。
 */
u8 AutoDrive_SetFishPositionRaw(const u8 *data_m);

/**
 * @brief 设置自动返航开关并可选保存返航点。
 * @param data_m 旧协议开关载荷；第 1 字节为开关值，后续可带 10 字节点位。
 * @param len 载荷长度，单位字节。
 */
void AutoDrive_SetSwitchRaw(const u8 *data_m, u8 len);

/**
 * @brief 获取当前已加载的返航配置。
 * @param cfg 输出配置；为 NULL 时直接返回。
 */
void AutoDrive_GetStoredConfig(AutoDrive_ReturnConfig_t *cfg);

/**
 * @brief 获取当前 GPS 点位快照。
 * @param point 输出点位；为 NULL 时只执行有效性判断。
 */
void AutoDrive_GetCurrentPointRaw(AutoDrive_PointRaw_t *point);

/**
 * @brief 获取当前返航点。
 * @param point 输出返航点；可为 NULL。
 * @return 1 点位有效，0 点位无效。
 */
u8 AutoDrive_GetReturnPositionRaw(AutoDrive_PointRaw_t *point);

/**
 * @brief 获取当前选中的钓点。
 * @param point 输出钓点；可为 NULL。
 * @return 1 点位有效，0 点位无效。
 */
u8 AutoDrive_GetFishPositionRaw(AutoDrive_PointRaw_t *point);

/**
 * @brief 按 1 起始索引读取缓存钓点。
 * @param index 钓点序号，范围 1..AUTODRIVE_FISH_POINT_COUNT。
 * @param point 输出钓点；可为 NULL。
 * @return 1 点位有效，0 索引无效或槽位为空。
 */
u8 AutoDrive_GetFishPositionByIndexRaw(u8 index, AutoDrive_PointRaw_t *point);

/**
 * @brief 获取最近一次钓点命令匹配的槽位索引。
 * @return 0 表示未匹配；1..AUTODRIVE_FISH_POINT_COUNT 表示钓点槽位。
 */
u8 AutoDrive_GetLastFishCommandIndex(void);

/**
 * @brief 获取自动驾驶诊断快照。
 * @param snapshot 输出快照；为 NULL 时直接返回。
 */
void AutoDrive_GetDebugSnapshot(AutoDrive_DebugSnapshot_t *snapshot);

/**
 * @brief 估算两个旧协议点位之间的距离。
 * @param nowpositionData 当前点位，按 AutoDrive_PointRaw_t 布局解释。
 * @param despositionData 目标点位，按 AutoDrive_PointRaw_t 布局解释。
 * @return 近似距离，单位 m；超过 65535 m 时饱和到 65535。
 */
u16 AutoDrive_GetDistanceNowToDestination(const u8 *nowpositionData,
                                          const u8 *despositionData);

/**
 * @brief 估算当前点到目标点所在象限内的夹角。
 * @param nowpositionData 当前点位，按 AutoDrive_PointRaw_t 布局解释。
 * @param despositionData 目标点位，按 AutoDrive_PointRaw_t 布局解释。
 * @return 0..90 度夹角；两点重合时返回 65535。
 */
u16 AutoDrive_GetAngelNowToDestination(const u8 *nowpositionData,
                                       const u8 *despositionData);

/**
 * @brief 将象限方向和夹角转换为北基准航向角。
 * @param direction AutoDrive_Direction_t 方向。
 * @param angel 象限内夹角，单位 deg。
 * @return 0..359 范围的航向角，单位 deg。
 */
u16 AutoDrive_GetNorthAngel(u8 direction, u8 angel);

/**
 * @brief 判断当前点到目标点的大致方向。
 * @param nowpositionData 当前点位，按 AutoDrive_PointRaw_t 布局解释。
 * @param despositionData 目标点位，按 AutoDrive_PointRaw_t 布局解释。
 * @return AutoDrive_Direction_t 方向值。
 */
u8 AutoDrive_GetDirectionNowToDestination(const u8 *nowpositionData,
                                          const u8 *despositionData);

/** @brief 推进手动链路在线计数和自动返航超时判断。 */
void AutoDrive_LinkAliveTick(void);

/** @brief 喂狗手动链路在线计数，短时间内禁止链路超时返航。 */
void AutoDrive_LinkAliveKick(void);

#endif
