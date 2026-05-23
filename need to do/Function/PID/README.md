# PID 定点控制器

`Code_boweny/Function/PID/` 提供通用位置式 PID 控制器，适合电机速度环、航向环、姿态角速度环等上层控制逻辑复用。

## 特点

- 不使用浮点运算。
- 增益使用 Q10 定点格式。
- 每个 `PID_Controller_t` 独立维护状态。
- 支持输出限幅。
- 支持积分限幅，降低积分饱和风险。
- 首次更新时微分项为 0，避免启动瞬间突跳。

## 文件结构

```text
Code_boweny/Function/PID/
├── PID.h       # 类型定义、宏、API 声明
├── PID.c       # PID 实现
└── README.md   # 本文档
```

## 增益格式

PID 增益使用 Q10 定点数：

```c
#define PID_GAIN_Q      10
#define PID_GAIN_SCALE  (1L << PID_GAIN_Q)
```

含义：

- `1024` 表示 `1.0`
- `512` 表示 `0.5`
- `2048` 表示 `2.0`

示例：

```c
PID_GAIN_FROM_INT(1)  /* 1.0 */
512                   /* 0.5 */
```

## API

```c
void PID_Init(PID_Controller_t *pid,
              int16 kp, int16 ki, int16 kd,
              int16 output_min, int16 output_max,
              int32 integral_min, int32 integral_max);

void PID_Reset(PID_Controller_t *pid);
void PID_SetTarget(PID_Controller_t *pid, int16 target);
void PID_SetGains(PID_Controller_t *pid, int16 kp, int16 ki, int16 kd);
void PID_SetOutputLimit(PID_Controller_t *pid, int16 output_min, int16 output_max);
void PID_SetIntegralLimit(PID_Controller_t *pid, int32 integral_min, int32 integral_max);
int16 PID_Update(PID_Controller_t *pid, int16 measured);
int16 PID_UpdateTarget(PID_Controller_t *pid, int16 target, int16 measured);
```

## 使用示例

```c
#include "PID.h"

static PID_Controller_t left_speed_pid;

void Control_Init(void)
{
    PID_Init(&left_speed_pid,
             900, 80, 20,          /* kp=0.879, ki=0.078, kd=0.019 */
             -1000, 1000,          /* 输出对应 Motor_SetSpeed 范围 */
             -3000L, 3000L);       /* 积分限幅 */
}

void Control_Update(int16 target_speed, int16 measured_speed)
{
    int16 pwm_cmd;

    pwm_cmd = PID_UpdateTarget(&left_speed_pid, target_speed, measured_speed);
    Motor_SetSpeed(MOTOR_LEFT, pwm_cmd);
}
```

## 计算公式

```text
error      = target - measured
integral   = clamp(integral + error)
derivative = error - prev_error
output     = (kp * error + ki * integral + kd * derivative) >> 10
output     = clamp(output)
```

## 航向自稳定流程：手动油门 + PID 差速

本项目的手动航向自稳定在 `Code_boweny/Device/Control/ShipControl.c` 中实现，PID 本身只负责输出航向误差修正量，不直接接管油门。

### 1. 更新频率

- 遥控器油门输入只由 `SHIP_CMD_THROTTLE(0x11)` 更新，`lr/ud/key` 的来源频率与遥控器油门帧一致。
- 手动控制目标由 `ShipControl_Tick()` 按 `SHIP_MANUAL_CONTROL_PERIOD_MS=10ms` 使用最新 `lr/ud/key` 连续刷新，避免遥控帧率低或偶发丢帧时电机目标一顿一顿。
- `Motor` 模块只接收最新目标 speed；底层 PWM 波形仍按硬件 PWM 定时器频率连续输出。
- 日志由独立限频控制：`SHIP_RC_INPUT_LOG_PERIOD_MS` 只限制 `0x11` 输入日志，`SHIP_YAW_HOLD_LOG_PERIOD_MS` 限制控制层 yaw-hold 诊断日志。日志打印不触发、不跳过、不延后内部控制计算。
- 无遥控油门时不会启动空闲 yaw-hold 驱动电机，避免上电后 AHRS ready 触发电机自转。

### 2. 遥控输入转为左右输入油门

收到 `0x11` 后先解析：

```text
lr  = 左右摇杆
ud  = 前后油门
key = 按键
```

随后做滤波、死区和曲线映射：

```text
throttle_speed = ThrottleToSignedSpeed(filtered_ud)
steering_speed = SteeringToSignedSpeed(filtered_lr)
left_input     = clamp(throttle_speed + steering_speed)
right_input    = clamp(throttle_speed - steering_speed)
```

这里的 `left_input/right_input` 是遥控器原始意图对应的左右电机输入油门。

### 3. 进入手动航向自稳定的条件

只有同时满足以下条件，才会叠加 PID：

- `SHIP_YAW_HOLD_ENABLE=1`
- `SHIP_YAW_HOLD_MANUAL_ENABLE=1`
- 转向输入在 `SHIP_YAW_HOLD_STEER_GATE` 内，认为用户想直线走
- 若 `SHIP_YAW_HOLD_FORWARD_ONLY=1`，则只允许前进油门进入自稳定
- `MainLoop_IsHeadingReady()` 返回可用航向

如果不满足这些条件，直接按普通差速开环输出 `left_input/right_input`。

### 4. PID 如何计算

第一次进入自稳定时，锁定当前相对航向作为目标：

```text
yaw_hold_target_cd = current_yaw_cd
```

后续每到 `SHIP_YAW_HOLD_PERIOD_MS` 更新一次 PID。注意这里有两套单位：

- `yaw_error_cd`：真实航向误差，单位是 0.01°。
- `yaw_error_ctrl`：进入 PID 的归一化控制量，范围是 `-SHIP_YAW_HOLD_OUTPUT_LIMIT ~ +SHIP_YAW_HOLD_OUTPUT_LIMIT`。

先把角度误差按实际满量程映射到控制量：

```text
yaw_error_cd = wrap(yaw_hold_target_cd - current_yaw_cd)
if abs(yaw_error_cd) <= SHIP_YAW_HOLD_DEADBAND_CD:
    yaw_error_ctrl = 0
else:
    yaw_error_ctrl =
        sign(yaw_error_cd)
        * (abs(yaw_error_cd) - SHIP_YAW_HOLD_DEADBAND_CD)
        * SHIP_YAW_HOLD_OUTPUT_LIMIT
        / (SHIP_YAW_HOLD_FULL_ERROR_CD - SHIP_YAW_HOLD_DEADBAND_CD)

yaw_error_ctrl = clamp(yaw_error_ctrl,
                       -SHIP_YAW_HOLD_OUTPUT_LIMIT,
                       +SHIP_YAW_HOLD_OUTPUT_LIMIT)

pid_output = PID_UpdateTarget(yaw_pid, yaw_error_ctrl, 0)
```

当前标定下，`SHIP_YAW_HOLD_FULL_ERROR_CD=1000` 表示偏航 10.00° 才达到满控制输入，`SHIP_YAW_HOLD_OUTPUT_LIMIT=1000` 与 `Motor_SetSpeed()` 的满量程一致。这样不会再出现 1~2° 偏差就把 PID 打满的情况。

注意：这里 PID 的输出只是归一化航向修正强度，不是电机油门。

### 5. 自稳定输出如何叠加到油门

自稳定时两个电机先取同一个基础油门，基础油门不是单独的 `throttle_speed`，而是左右输入油门的最大值：

```text
base = max(abs(left_input), abs(right_input))
if throttle_speed < 0:
    base = -base
```

然后把 PID 输出按当前基础油门和最大差速比例换算成真实电机 `speed`：

```text
yaw_limit  = abs(base) * SHIP_YAW_HOLD_DIFF_LIMIT_PERMILLE / 1000
if base != 0:
    yaw_limit = min(yaw_limit, MOTOR_SPEED_MAX - abs(base))
yaw_output = pid_output * yaw_limit / SHIP_YAW_HOLD_OUTPUT_LIMIT
yaw_output = clamp(yaw_output, -yaw_limit, +yaw_limit)
```

当前 `SHIP_YAW_HOLD_DIFF_LIMIT_PERMILLE=400`，所以 PID 满输出时差速修正最多是当前基础油门的 40%，同时还会受电机满量程余量限制。例如 `base=600` 时最大 `yaw_output=240`，最终左右最多变成 `840/360`；`base=850` 时电机上限余量只有 150，最大 `yaw_output=150`，最终最多是 `1000/700`，不会让 `base + PID` 长时间被独立夹死在满量程。

最终输出：

```text
left_speed  = clamp(base + yaw_output)
right_speed = clamp(base - yaw_output)
```

也就是说，自稳定控制方式是：

```text
两个电机共同基础油门 = max(左输入油门, 右输入油门)
PID 只在这个共同基础油门上做左右差速修正
```

它不是“偏航后单独锁死一个电机、另一个拉满”，也不是让 PID 替代遥控器油门。

## 注意事项

- `PID_Update()` 返回 `int16`，通常可直接映射到电机 PWM 命令或舵机控制量。
- `kp/ki/kd` 都是 Q10 定点值，不要传入浮点数。
- 重新进入控制模式前，建议先调用 `PID_Reset()` 清除历史积分和误差。
- 控制方向相反时，应在上层调整误差方向或对输出取反。
