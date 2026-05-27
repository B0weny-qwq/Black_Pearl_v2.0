# App 层说明

`App/` 是整船业务逻辑层，只编排状态机、协议、姿态航向、自动驾驶和扩展回调。这里可以调用 `BoardDevices/`、`Components/`、`Services/` 的公开 API，但不能直接包含 STC 寄存器头文件、裸引脚宏或官方外设驱动。

## 目录

- `Inc/`：App 对外接口、运行档位、协议结构和控制状态机头文件。
- `Src/app.c`：设备 bring-up、主循环调度、AHRS/MAG/Heading 融合、协议事件分发。
- `Src/app_extension.c`：外包二次业务预留入口。按键触发 LED 闪烁、蜂鸣器提示、现场联动逻辑优先写这里。
- `Src/ship_protocol.c`：旧无线协议 `0x10/0x11/0x12/0x13/0x14/0x15/0x16`，负责解帧、配对、GPS 回包、电量事件和命令分发。
- `Src/ship_control.c`：船体运动控制，最终电机输出统一从这里进入 `board_motor_set_both_speed()`。
- `Src/autodrive.c`：GPS 返航、钓点、对准和定点航向保持状态机。
- `Src/main.c`：平台初始化和 App 调度入口，不放业务。

## 外包改动入口

新增业务优先改 `App/Src/app_extension.c`：

- `app_extension_init()`：初始化扩展状态。
- `app_extension_poll(now_ms)`：主循环轮询，定时动作在这里用 `now_ms` 驱动。
- `app_extension_on_ship_event(event)`：观察协议事件，适合按键触发逻辑。

示例：要做“A 键 LED 闪烁”，先在 `BoardDevices/` 增加 `board_led_set()` 或 `board_led_toggle()`，再在 `app_extension_on_ship_event()` 里识别 `SHIP_PROTOCOL_EVENT_KEY_EDGE` 和 `SHIP_PROTOCOL_KEY_A_LIGHT`，记录闪烁状态，最后在 `app_extension_poll()` 中按时间翻转 LED。

## 禁止事项

- 不在 `App/` 里 include `STC32G_*.h`、`config.h`、`stc32g.h`。
- 不在 `App/` 里直接写 `P0/P1/P2/P3`、PWM 寄存器、ADC 寄存器。
- 不在协议回调里长延时或阻塞等待。
- 不绕过 `ShipControl` 直接写电机。

## 上位机卡片来源

| 上位机卡片 | 固件日志 | App 源头 | 底层来源 |
| --- | --- | --- | --- |
| 控制模式 | `[CTRL] I: event=mode ...` | `ShipControl_LogModeEvent()` | `ShipControl_SetMode()` |
| 电机输出 | `[CTRL] I: out m=... l=... r=...` | `ShipControl_LogMotorOutput()` | `board_motor_set_both_speed()` |
| 电量 | `[SHIP] I: adc raw=... bat=... p=...` | `ship_protocol_log_power_sample()` | `board_power_read()` |
| AHRS R/P/Y | `[AHRS] I: rpy=... flg=...` | `app_ahrs_log()` | `board_imu_read()` + `AHRS_UpdateRaw6Axis()` |
| IMU 原始加速度 | `[IMU] I: raw a=...` | `app_ahrs_poll()` | `board_imu_read()` |
| 磁力计 | `[MAG] I: test raw=... norm1=...`、`[MAG] I: raw=... norm=... yaw=...` | `app_mag_observe_poll()`、`app_ahrs_log()` | `board_mag_read()` + `MagCompass_Update()` |
| 航向调试 | `[HDG] I: abs=... rel=...` | `app_ahrs_log()` | `Heading_Update()` |
| 遥控输入 | `[SHIP] I: rc cmd=0x11 ...` | `ship_protocol_handle_throttle()` | `board_wireless_receive()` |
| GPS/AutoDrive | `[SHIP] I: tx16 ...`、`[SHIP] I: 0x13/0x14/0x15 ...` | `ship_protocol_*()`、`AutoDrive_*()` | `board_gps_get_state()` |

## 2026-05 v1.1 对齐补充

- `ShipControl_Init()` 会强制输出一帧 `l=0 r=0` 的 `[CTRL] I: out ...`，没有遥控动作时上位机也能确认电机输出链路已接到 ShipControl。
- E 键定速巡航由 `ship_protocol_handle_key_edge()` 请求，`ShipControl` 保存请求速度并单独输出 ramp 后的 `base`。上位机看到 `CRUISE_HEADING_HOLD`、`th=760`、`base=520..760` 时表示巡航正在正常斜坡加速。
- 电量采样仍按 `SHIP_POWER_SAMPLE_DIVIDER` 降频、`SHIP_POWER_LOG_PERIOD_MS` 节流；但首个有效 ADC 样本会强制输出一次 `[SHIP] I: adc raw=... mv=... bat=... p=...`。
- 若日志为 `adc not-ready rc=-3`，这是 ADC 采样无效而不是上位机解析错误；继续追 `BoardDevices/Src/board_power.c` 的 P0.0/ADC_CH8 采样链路。
- `app_ahrs_poll()` 每约 1 秒输出一次 `[IMU] I: raw a=...`；GX/GY/GZ 不再发给上位机，AHRS 内部仍使用 gyro 参与解算。
- `app_mag_observe_poll()` 独立于 AHRS 每约 1 秒读取一次 QMC6309 并输出 `[MAG] I: test raw=... norm1=...`；若读失败，会输出 `[MAG] W: read fail ...` 和 `addr/id/c1/c2` 诊断。
