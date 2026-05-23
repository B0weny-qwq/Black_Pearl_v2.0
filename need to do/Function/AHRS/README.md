# AHRS 与 HeadingEstimator 模块说明

## 概述

`Code_boweny/Function/AHRS/` 当前包含两部分：

1. `AHRS.c/.h`
   四元数 Mahony 风格姿态融合主链
2. `HeadingEstimator.c/.h`
   面向控船 yaw / heading 的航向估计与慢速磁修正链

当前根目录工程真实状态：

- IMU 启用
- MAG 启用
- AHRS 启用
- HeadingEstimator 启用
- GPS 不进入 AHRS 主融合链

## 当前主接口

AHRS：

```c
void AHRS_Reset(void);
s8 AHRS_Update6Axis(int16 ax, int16 ay, int16 az,
                    int16 gx, int16 gy, int16 gz,
                    u16 dt_ms);
s8 AHRS_UpdateRaw6Axis(int16 raw_ax, int16 raw_ay, int16 raw_az,
                       int16 raw_gx, int16 raw_gy, int16 raw_gz,
                       u16 dt_ms);
s8 AHRS_UpdateMag(int16 mx, int16 my, int16 mz);
s8 AHRS_UpdateRawMag(int16 raw_mx, int16 raw_my, int16 raw_mz);
const AHRS_State_t *AHRS_GetState(void);
u8 AHRS_IsReady(void);
```

HeadingEstimator：

```c
void Heading_Init(HeadingEstimator_t *h);
void Heading_ResetZero(HeadingEstimator_t *h);
void Heading_SetHeadingDeg(HeadingEstimator_t *h, float heading_deg);
void Heading_Update(HeadingEstimator_t *h,
                    float gyro_z_dps,
                    float yaw_mag_deg,
                    u8 mag_valid,
                    u8 static_flag,
                    float dt);
```

## 当前运行链路

主循环里与姿态相关的真实入口：

```text
MainLoop_RunOnce()
  -> IMU_ServicePoll()
  -> IMU_AhrsPoll()
```

其中：

- `IMU_ServicePoll()` 负责推进 `QMI8658_Service()` 状态机
- `IMU_AhrsPoll()` 负责读取 IMU / MAG，并推进 `AHRS` 与 `HeadingEstimator`

## 当前时序参数

以当前代码为准：

- `AHRS_IMU_PERIOD_MS = 17`
- `AHRS_MAG_PERIOD_MS = 100`
- `AHRS_DT_MAX_MS = 50`
- `AHRS_GYRO_LSB_PER_DPS = 128`
- `AHRS_MAG_ENABLE = 1`

HeadingEstimator 侧当前关键参数：

- `HEADING_USE_INTERNAL_BIAS = 0`
- `HEADING_DBG_FORCE_MAG_OFF = 0`
- `HEADING_KMAG_STATIC = 0.0008f`
- `HEADING_KMAG_MOVE = 0.0002f`

## 当前策略说明

### AHRS

当前 AHRS 负责：

- raw 轴映射
- gyro bias 学习
- gyro deadband / LPF
- 四元数传播
- acc 重力误差反馈
- mag 倾斜补偿误差反馈

输出：

- `roll_deg100`
- `pitch_deg100`
- `yaw_deg100`
- `yaw_gyro_deg100`
- `yaw_mag_deg100`
- `gyro_x/y/z_dps100`

### HeadingEstimator

当前 HeadingEstimator 负责：

- 用 `gyro_z_dps` 做短期 yaw / heading 积分
- 用 `yaw_mag_deg` 做慢速磁修正
- 管理 heading zero / relative yaw 输出

注意：

- 当前默认 `HEADING_USE_INTERNAL_BIAS=0`
- 也就是 HeadingEstimator 不再内部重复学习 gyro bias
- 传入值默认视为已经过 AHRS 处理后的 `gyro_z_dps`

## 当前磁修正规则

当前实现不是“磁航向一直强拉”。

真实行为是：

- `raw_mag_valid` 先看原始磁是否有效
- `mag_used` 再看门控后是否真的参与修正
- 静止和运动分别用不同 `KMAG`
- `mv=0` 或门控失败时，只保留诊断显示，不参与修正

日志里重点字段：

- `mv`
- `mu`
- `ym`
- `err`
- `hp`
- `hd`
- `hm`

## 自动归零

当前自动归零不是只看 gyro 静止。

真实门控已经加强，要求综合考虑：

- uptime
- gyro bias ready
- acc valid
- gyro still
- mag valid
- roll/pitch 稳定
- yaw_mag 稳定
- stable_count

因此不能再把 `ys` 小范围增长简单理解为“马上会归零”。

## 当前已知边界

- 绝对 yaw 仍依赖磁环境
- GPS 还没有进入 AHRS 主链
- HeadingEstimator 当前更偏工程调试可用，不是最终导航级航向系统
- 如果磁环境差，`ym` 漂移并不一定等于 gyro 漂移

## 调参入口

先看：

- `Code_boweny/Function/AHRS/AHRS.h`
- `Code_boweny/Function/AHRS/HeadingEstimator.h`
- `User/FeatureSwitch.h`

不要直接在 `.c` 里散改常量。

## 版本说明

当前 README 只描述根目录工程真实状态。  
如果后续改了 `AHRS.h`、`HeadingEstimator.h` 或 `MainLoop.c` 的时序/开关，这份 README 也要同步更新。
