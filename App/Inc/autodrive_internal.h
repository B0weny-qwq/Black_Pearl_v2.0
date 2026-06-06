/**
 * @file autodrive_internal.h
 * @brief AutoDrive App 内部拆分接口。
 *
 * 本头文件只给 `App/Src/autodrive*.c` 使用，用来共享 v1.1 迁移后的
 * 自动驾驶运行态、旧协议点位格式和导航控制内部函数。外部模块仍只包含
 * `autodrive.h`。
 */

#ifndef __AUTODRIVE_INTERNAL_H__
#define __AUTODRIVE_INTERNAL_H__

#include "autodrive.h"
#include "board_gps.h"

#define AUTODRIVE_WORK_OVERTIME            (10U * 60U * 100U)
#define AUTODRIVE_MIN_ACTIVE_DISTANCE_M    10U
#define AUTODRIVE_MAX_ACTIVE_DISTANCE_M    800U
#define AUTODRIVE_ARRIVE_DISTANCE_M        3U
#define AUTODRIVE_MANUAL_TIMEOUT_TICKS     (30U * 100U)
#define AUTODRIVE_MANUAL_CLOSE_TICKS       300U
#define AUTODRIVE_CRUISE_BASE_SPEED        850
#define AUTODRIVE_APPROACH_DISTANCE_M      20U
#define AUTODRIVE_CRAWL_DISTANCE_M         8U
#define AUTODRIVE_APPROACH_BASE_SPEED      700
#define AUTODRIVE_CRAWL_BASE_SPEED         500
#define AUTODRIVE_MINUTE_SCALE             10000UL
#define AUTODRIVE_MINUTES_PER_DEG          60UL
#define AUTODRIVE_METERS_PER_MINUTE        1850UL
#define AUTODRIVE_METERS_PER_DEG           111130UL
#define AUTODRIVE_ATAN_Q10                 1024UL
#define AUTODRIVE_ALIGN_TOLERANCE_CD       800
#define AUTODRIVE_ALIGN_ZERO_CROSS_CD      50
#define AUTODRIVE_ALIGN_STABLE_TICKS       20U
#define AUTODRIVE_ALIGN_TIMEOUT_TICKS      800U
#define AUTODRIVE_FISH_DUP_WAIT_MS         1500UL

extern u8 autodrive_switch;
extern u8 autodrive_state;
extern u8 autodrive_mode;
extern u16 autodrive_work_overtime;
extern u8 autodrive_fail_flag;
extern AutoDrive_PointRaw_t idle_position;
extern AutoDrive_PointRaw_t now_position;
extern AutoDrive_PointRaw_t last_position;
extern AutoDrive_PointRaw_t return_position;
extern AutoDrive_PointRaw_t fish_position;
extern u8 last_fish_cmd_index;
extern u8 last_fish_rx_result;
extern AutoDrive_PointRaw_t last_fish_rx_point;
extern u32 last_fish_rx_ms;
extern u8 last_fish_rx_valid;
extern AutoDrive_ReturnConfig_t autodrv_cfg;
extern u8 destination_direction;
extern u8 nowrun_direction;
extern u16 destination_angle;
extern u16 autodrive_target_heading_cd;
extern u8 autodrive_target_heading_valid;
extern u16 autodrive_base_speed;
extern u16 autodrive_align_ticks;
extern u8 autodrive_align_stable_ticks;
extern u8 autodrive_align_zero_seen;
extern u8 autodrive_align_prev_valid;
extern int16 autodrive_align_prev_error_cd;
extern u16 link_alive_ticks;
extern u16 link_close_ticks;
extern u32 last_run_update_seq;
extern u32 last_poll_tick_ms;
extern u32 last_link_tick_ms;
extern u8 last_diag_reason;

/** @brief 读取旧协议大端 16 位字段。 */
u16 AutoDrive_ReadU16Wire(const u8 *data_m);

/** @brief 将 10 字节旧协议点位转换为内部点位结构。 */
void AutoDrive_PointFromLegacyWire(AutoDrive_PointRaw_t *point,
                                   const u8 *data_m);

/** @brief 计算两个无符号 32 位值差值的绝对值。 */
u32 AutoDrive_Abs32Diff(u32 lhs, u32 rhs);

/** @brief 旧工程整数近似 atan，输入为 0..1 的 Q10 比值。 */
u16 AutoDrive_Atan01Deg(u16 z_q10);

/** @brief 判断旧协议点位字段是否有效。 */
u8 AutoDrive_PointRawValid(const AutoDrive_PointRaw_t *point);

/** @brief 复制点位，参数为空时直接忽略。 */
void AutoDrive_CopyPoint(AutoDrive_PointRaw_t *dst,
                         const AutoDrive_PointRaw_t *src);

/** @brief 清空点位结构。 */
void AutoDrive_ClearPoint(AutoDrive_PointRaw_t *point);

/** @brief 比较两个旧协议点位是否完全一致。 */
u8 AutoDrive_PointRawEqual(const AutoDrive_PointRaw_t *lhs,
                           const AutoDrive_PointRaw_t *rhs);

/** @brief 判断短时间内重复收到同一钓点。 */
u8 AutoDrive_IsFishRxDuplicate(const AutoDrive_PointRaw_t *point, u32 now_ms);

/** @brief 记录最近一次钓点接收时间和内容。 */
void AutoDrive_RecordFishRxPoint(const AutoDrive_PointRaw_t *point, u32 now_ms);

/** @brief 将 GPS deg1e7 快照转换为旧协议 ddmm.mmmm 点位。 */
void AutoDrive_PointFromGps(AutoDrive_PointRaw_t *point,
                            const board_gps_state_t *gps);

/** @brief 判断当前 GPS 是否满足自动驾驶使用门槛。 */
u8 AutoDrive_GpsReady(void);

/** @brief 获取当前优先使用的卫星数。 */
u8 AutoDrive_GetSatCount(void);

/** @brief 获取满足 GPS 门槛的当前点位。 */
u8 AutoDrive_GetReadyCurrentPoint(AutoDrive_PointRaw_t *point);

/** @brief 记录最近一次诊断原因。 */
void AutoDrive_SetDiagReason(u8 reason);

/** @brief 重置接近阶段速度跟踪。 */
void AutoDrive_ResetApproachTracker(void);

/** @brief 重置对准阶段稳定判定。 */
void AutoDrive_ResetAlignTracker(void);

/** @brief 判断对准阶段是否已到达目标航向。 */
u8 AutoDrive_AlignTargetReached(void);

/** @brief 推进自动驾驶工作超时保护。 */
u8 AutoDrive_TickWorkOvertime(void);

/** @brief 根据目标距离更新前进基础速度。 */
void AutoDrive_UpdateApproachSpeed(u16 distance_m);

/** @brief 获取当前模式对应的目标点。 */
u8 AutoDrive_GetTargetPoint(const AutoDrive_PointRaw_t **target);

/** @brief 根据当前点和目标点刷新目标航向。 */
u8 AutoDrive_UpdateTargetHeading(const AutoDrive_PointRaw_t *current_point,
                                 const AutoDrive_PointRaw_t *target_point);

/** @brief 请求 ShipControl 进入 GPS 航向保持前进输出。 */
u8 AutoDrive_ApplyHeadingHold(u16 base_speed);

/** @brief 请求 ShipControl 进入 GPS 原地对准输出。 */
u8 AutoDrive_ApplyAlignHeadingHold(void);

/** @brief 更新空闲点位快照。 */
void AutoDrive_UpdateIdlePosition(void);

/** @brief 更新运行阶段 GPS 当前点。 */
void AutoDrive_UpdateGpsStepPoints(void);

/** @brief 计算当前可显示的航向角，未 ready 时返回 0。 */
u16 AutoDrive_GetStartHeadingDeg(void);

/** @brief 计算运行航向角，未 ready 时返回 65535。 */
u16 AutoDrive_GetRunHeadingDeg(void);

/** @brief 按模式和最近原因选择诊断目标点。 */
void AutoDrive_GetSnapshotTargetPoint(AutoDrive_PointRaw_t *point);

/** @brief 判断快照中的目标点是否可启动。 */
u8 AutoDrive_CanActivateTargetPoint(const AutoDrive_PointRaw_t *point,
                                    const AutoDrive_PointRaw_t *current_point);

#endif
