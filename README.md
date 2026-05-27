# Black Pearl v2.0

本工程是面向 Black Pearl v1.0 的移植、解耦和重构版本。目标是在保留
STC32G 官方驱动参考价值的前提下，把真实项目代码迁移到更清晰
的 EmbedForge Level 1.5 工程结构中。

项目文档默认使用中文。目录名、文件名、函数名和宏名保持英文，便于和代码、
Keil 工程以及芯片资料对应。

## 工程目标

- 保留 v1.0 行为和 STC 官方驱动语义，降低移植成本。
- 拆掉旧版本中 `User/`、官方 `App/` 示例和真实应用混在一起的问题。
- 避免应用层直接依赖 STC 寄存器头、裸引脚、裸寄存器和官方 `Sample_*`
  函数。
- 为板级设备 API、纯算法组件和后续 YAML 生成接口预留稳定位置。

## 当前状态

- Keil 工程：`RVMDK/STC32G-LIB.uvproj`
- 主入口：`App/Src/main.c`
- 应用编排：`App/Src/app.c`
- 平台初始化：`Platform/Src/platform.c`
- 平台调度：`Platform/Src/platform_scheduler.c`
- 板级控制台：`BoardDevices/Src/board_console.c`
- 板级 GPS：`BoardDevices/Src/board_gps.c`，当前已接入 UART2 接收和 NMEA 解析轮询。
- 板级 IMU：`BoardDevices/Src/board_imu.c`，当前已完成 QMI8658 板级绑定和板上移植验证，支持初始化、ready 刷新和原始采样读取。
- 板级磁力计：`BoardDevices/Src/board_mag.c`，当前已完成 QMC6309 板级绑定并复用传感器 IIC 总线。
- 板级传感器总线：`BoardDevices/Src/board_sensor_bus.c`，当前封装 DMA IIC，固定 P1.4/P1.5、100 kHz，供传感器链路复用。
- 板级 LT8920：`BoardDevices/Src/board_lt8920.c`，当前已封装 LT8920 + KCT8206 的最小 bring-up 和寄存器校验。
- 板级无线链路：`BoardDevices/Src/board_wireless.c`，当前在 LT8920/KCT8206 之上提供 RX/TX、配对信道发送、payload 队列和天线扫描。
- 板级电源采样：`BoardDevices/Src/board_power.c`，当前隐藏 ADC/GPIO 细节，对上提供电池原始值、毫伏值和 `0..4` 电量等级。
- 板级存储：`BoardDevices/Src/board_storage.c`，当前隐藏 STC EEPROM/IAP 细节，供服务层保存少量配置。
- 参数服务：`Services/Src/parameter_store.c`，当前通过 `board_storage` 保存 AutoDrive 返航开关和返航点配置。
- 船端协议状态机：`App/Src/ship_protocol.c`，当前接入无线配对、旧帧截取、`0x11/0x13/0x14/0x15` 分发、事件快照队列、`0x12` GPS/status 回包和 `0x16` AutoDrive 诊断上报。
- 船体控制状态机：`App/Src/ship_control.c`，当前拥有电机输出控制权，负责手动开环/航向保持、E 键巡航、GPS 对齐/导航输出和超时停机。
- GPS AutoDrive 状态机：`App/Src/autodrive.c`，当前负责返航、去定点、对齐阶段、钓点表、配置加载保存和诊断快照。
- 烧录后运行行为：固件会启动 UART1 115200 日志，初始化 GPS、QMI8658、QMC6309、电源、LT8920/KCT8206、电机和协议状态机；随后持续执行 GPS 解析、无线配对/收发、手动控制、AutoDrive、AHRS/Heading 和电机服务。
- 上位机/遥控兼容：保留旧 `AA | len | cmd | payload | xor | BB` 帧，兼容 `0x10/0x0F/0x11/0x12/0x13/0x14/0x15`；`0x12` 仍固定 15 字节，`payload[13]` 为 `0..4` 电量等级，`payload[14]` 为 AutoDrive 状态。`0x16` 是新增主动诊断帧，不替代旧 `0x12`。
- 板级 SPI-PS：`BoardDevices/Src/board_spi_ps.c`，当前生成配置默认关闭，并显式标记为与 LT8920 共用 STC SPI 外设，防止误启用后重配无线 SPI。
- 芯片驱动层：`ChipDrivers/Src/`，当前含 `gnss_nmea`、`QMI8658`、`QMC6309`、`LT8920`、`KCT8206`
- 算法组件：`Components/Src/Filter.c`、`Components/Src/PID.c`
- MCU 抽象层：`McuAbstraction/Src/`，当前含 `ef_uart`、`ef_iic`、`ef_spi`
- 轻量日志：`Services/Src/logger.c`
- 官方 STC 驱动、ISR 和库：`Drivers/`

## 分层规则

- `App/` 只放真实项目的应用编排、状态机和主循环。
- `BoardDevices/` 后续用于隐藏板级引脚、极性、总线、通道和外设实例选择。
- `ChipDrivers/` 用于放外部芯片或协议解析驱动，不直接绑定板级引脚和外设实例。
- `Components/` 用于纯算法、滤波器、PID、缓冲区、解析器等非硬件逻辑。
- `Services/` 用于日志、控制台、参数等系统服务；服务只能通过 `BoardDevices/`
  或抽象接口访问硬件。
- `Platform/` 负责时钟、引脚复用、启动胶水、STC 配置头和平台调度钩子。
- `Drivers/` 只保存 STC32G 官方外设驱动、ISR 和库文件。
- `McuAbstraction/` 保存基于 STC SDK 的统一外设抽象接口，例如 `ef_uart`、
  `ef_iic`、`ef_spi`。

`App/`、`Components/` 和 `BoardDevices/Inc` 中不要直接包含：

- `config.h`
- `stc32g.h`
- `STC32G_*.h`
- 裸端口或寄存器宏，例如 `P0`、`P10`、`EA`、`CLKSEL`

这些细节应留在 `Platform/`、`Drivers/` 或 `BoardDevices/Src` 的私有实现中。

## 时钟策略

工程保留官方资料使用的 24 MHz 基准：

```c
#define MAIN_Fosc 24000000L
```

`platform_clock_config()` 会开启扩展 SFR 访问，并选择 HIRC 不分频运行。这样
Timer、UART、I2C、SPI、CAN、Delay、LIN、EEPROM 等官方驱动里的分频公式仍
按 STC 驱动文档的含义工作。

高速 PLL 不做全局切换。HSPWM、HSSPI 这类需要高速域的外设，应在自己的
初始化流程中单独调用 `HSPllClkConfig()`，并按该外设的时钟重新计算参数。

## 文档索引

- `doc/total.md`：工程目录总览和各层职责。
- `doc/data.md`：平台数据、时钟策略、构建边界和迁移数据。
- `doc/CHANGELOG.md`：迁移和重构变更记录。
- `.codex/skills/embedforge-project-standards/SKILL.md`：项目内自带的 EmbedForge
  工程规范副本，供切换环境时直接复用。

## 迁移流程

1. 从芯片资料、旧版工程或本地参考代码提取需要的逻辑。
2. 将板级硬件访问迁移到 `BoardDevices/`。
3. 将纯算法、状态逻辑和数据处理迁移到 `Components/`。
4. `App/` 只保留初始化顺序、状态机调度和高层控制流。
5. 当板级 API、时钟假设或构建边界变化时，同步更新 `doc/` 文档。

## 当前已迁移模块

### 控制台、日志与 GPS 接收链

当前控制台、日志和 GPS 接收链已经按新结构拆分：

- `McuAbstraction/Inc/ef_uart.h`、`McuAbstraction/Src/ef_uart.c`：UART 统一封装，内部调用
  STC 官方 UART/NVIC 驱动。
- `BoardDevices/Inc/board_console.h`、`BoardDevices/Src/board_console.c`：板级
  console，固定使用 UART1，P3.1=TXD，P3.0=RXD，115200 8N1。
- `BoardDevices/Inc/board_gps.h`、`BoardDevices/Src/board_gps.c`：板级 GPS 设备，
  当前固定使用 UART2/P1.0/P1.1/115200，通过轮询喂入 NMEA 解析器。
- `ChipDrivers/Inc/gnss_nmea.h`、`ChipDrivers/Src/gnss_nmea.c`：GNSS NMEA0183
  解析器，只负责字节流解析和导航状态快照。
- `Services/Inc/logger.h`、`Services/Src/logger.c`：日志服务，提供
  `LOGI/LOGW/LOGE/LOGD/log_printf`。
- `Services/Inc/Log.h`：旧代码 include 兼容入口，后续迁移 v1.0 代码时可临时
  保持 `#include "Log.h"`。

当前主循环路径：

```text
main()
  -> platform_init()
  -> app_init()
     -> board_console_init()
     -> log_init()
     -> app_bring_up_devices()
        -> 输出版本号
        -> board_gps_init()
        -> board_imu_init()
        -> board_mag_init()
        -> board_power_init()
        -> board_wireless_init()
        -> ship_protocol_init()
        -> board_spi_ps_init()
        -> board_motor_init()
  -> loop
     -> platform_scheduler_run()
     -> app_loop()
        -> board_gps_poll()
        -> board_wireless_poll()
        -> ship_protocol_run_scheduler()
        -> board_wireless_search_signal_poll()
        -> app_spi_ps_poll()
        -> app_ahrs_poll()
        -> board_motor_service()
```

其中 `log_init()` 仅在 `board_console_init()` 成功时执行；设备 bring-up 本身不依赖
日志初始化，控制台异常时仍会继续初始化 IMU、磁力计和无线链路。

日志输出格式保持旧版口径：

```text
[tag] I: message\r\n
[tag] W: message\r\n
[tag] E: message\r\n
[tag] D: message\r\n
```

### 传感器总线、IMU/地磁/RF bring-up 与滤波组件

- `McuAbstraction/Inc/ef_iic.h`、`McuAbstraction/Src/ef_iic.c`：IIC 统一封装，当前采用
  STC DMA IIC 参考时序实现寄存器连续读写。
- `BoardDevices/Src/board_sensor_bus.c`、`BoardDevices/Src/board_sensor_bus.h`：
  板级传感器总线私有适配层，当前固定使用 P1.4/P1.5、100 kHz，不对 `App/`
  暴露 IIC 细节。
- `ChipDrivers/Inc/QMI8658.h`、`ChipDrivers/Src/QMI8658.c`：QMI8658 寄存器层驱动，
  已支持地址探测、可靠上电等待、ready 轮询、状态读取和原始数据读取；当前移植已在板上验证成功。
- `BoardDevices/Inc/board_imu.h`、`BoardDevices/Src/board_imu.c`：板级 IMU 接口，
  当前已绑定 `QMI8658` + `board_sensor_bus`，初始化和读数都返回显式状态码。初始化成功前会等待
  `STATUS0` accel/gyro ready 并实际读取首帧；如果寄存器配置成功但数据路径不出样，会返回数据错误并打印诊断，而不是继续报 `init ok`。
- `ChipDrivers/Inc/QMC6309.h`、`ChipDrivers/Src/QMC6309.c`：QMC6309 地磁计寄存器层驱动。
- `BoardDevices/Inc/board_mag.h`、`BoardDevices/Src/board_mag.c`：板级磁力计接口，
  当前已绑定 QMC6309，并复用 `board_sensor_bus`。
- `McuAbstraction/Inc/ef_spi.h`、`McuAbstraction/Src/ef_spi.c`：补充
  `ef_spi_transfer_byte()`，供 LT8920 这类全双工 SPI 寄存器访问复用。
- `ChipDrivers/Inc/LT8920.h`、`ChipDrivers/Src/LT8920.c` 与
  `ChipDrivers/Inc/KCT8206.h`、`ChipDrivers/Src/KCT8206.c`：LT8920 无线芯片和
  KCT8206 射频前端控制层。
- `BoardDevices/Inc/board_lt8920.h`、`BoardDevices/Src/board_lt8920.c`：板级
  LT8920 bring-up，隐藏 SPI 路由、RST、ANT_SEL、RXEN、TXEN 和默认寄存器校验。
- `BoardDevices/Inc/board_wireless.h`、`BoardDevices/Src/board_wireless.c`：板级
  LT8920 无线链路管理层，状态命名为 `BOARD_WIRELESS_MODE_IDLE/RX/TX`，对上提供
  `board_wireless_poll()`、`board_wireless_send()`、`board_wireless_receive()`、
  `board_wireless_set_channel()` 和天线搜索接口。
- `BoardDevices/Inc/board_power.h`、`BoardDevices/Src/board_power.c`：板级
  电池采样接口，隐藏 `P0.0 / ADC_CH8`、ADC 初始化和等级换算。
- `BoardDevices/Inc/board_storage.h`、`BoardDevices/Src/board_storage.c`：板级
  EEPROM/IAP 存储接口，App 和 Services 不直接包含 STC EEPROM 驱动。
- `Services/Inc/parameter_store.h`、`Services/Src/parameter_store.c`：轻量参数服务，
  当前用于保存 AutoDrive 配置字节。
- `App/Inc/ship_protocol.h`、`App/Src/ship_protocol.c`：船端协议状态机，状态命名为
  `SHIP_PROTOCOL_STATE_BOOT_WAIT/PAIR_SEND/PAIR_WAIT_RSP/WORK_RX`。当前固定配对信道
  `0x7F`，seed 为 `65 65 A0 65`，发送 10 次 `PAIR_REQ(0x10)` 后进入响应窗口和
  工作 RX；seed 派生 `work_rx=13`、`work_tx=77`、`key=32/30`、
  `reg36=0x2020`、`reg39=0x1E1E`。RF payload 按旧格式
  `AA | len | cmd | payload | xor | BB` 截帧，合法 `0x0F/0x11/0x13/0x14/0x15`
  分发后发送 `GPS_REPORT(0x12)`；`0x16` 作为 AutoDrive 主动诊断上报，不替代 `0x12`。
  协议事件通过 8 深度环形队列交给 `ship_protocol_take_event()`，`ship_protocol_get_event_snapshot()`
  仍保留最近一次快照语义。
- `App/Inc/ship_control.h`、`App/Src/ship_control.c`：船体控制状态机，协议层只提交
  手动输入、巡航请求和 GPS 导航请求；最终左右电机输出统一从这里写入 `board_motor`。
- `App/Inc/autodrive.h`、`App/Src/autodrive.c`：GPS 自动驾驶状态机，`0x13/0x14/0x15`
  会进入这里完成返航点保存、钓点表查重、点位启动、对齐、运行、到达和诊断快照。
- `App/Inc/autodrive_config.h`、`App/Src/autodrive_config.c`：AutoDrive 配置适配层，
  通过 `parameter_store` 保存返航开关和返航点，不直接调用 STC EEPROM。
- `Components/Inc/Filter.h`、`Components/Src/Filter.c`：三轴传感器一阶低通
  滤波组件，保留旧版 `Filter_*` API，供 IMU/地磁计读数链路复用。

### SPI-PS 对等通信

STC 官方 `APP_SPI_PS` 示例已抽象为两层：

- `McuAbstraction/Inc/ef_spi.h`、`McuAbstraction/Src/ef_spi.c`：SPI 统一封装，内部调用
  STC 官方 SPI/NVIC 驱动，并封装从机 ISR 接收缓冲。
- `BoardDevices/Inc/board_spi_ps.h`、`BoardDevices/Src/board_spi_ps.c`：
  板级 SPI-PS 对等链路，固定使用 P2.2=SS、P2.3=MOSI、P2.4=MISO、
  P2.5=SCLK。

保留原始参考实现的核心行为：

```text
默认从机模式
SS 高电平表示总线空闲
发送前拉低本端 SS 并切到主机
发送完成释放 SS 并退回从机
从机接收通过 SPI ISR 缓冲，周期调用 board_spi_ps_service() 判帧完成
```

原始参考代码中的 UART2 桥接回显属于 sample app 行为，未放入 SPI-PS 抽象层。
当前 `EF_BOARD_SPI_PS_ENABLED = 0U`；同时生成配置给出
`EF_BOARD_SPI_PS_SHARES_LT8920_SPI = 1U`，因此即使后续误打开 SPI-PS，`board_spi_ps_init()`
也会返回 `BOARD_SPI_PS_ERR_RESOURCE`，直到确认有独立 SPI 或实现安全仲裁/重初始化策略。

## v1.1 AHRS/MAG port note

- `Components/Inc/AHRS.h` and `Components/Src/AHRS.c` port the v1.1 quaternion AHRS solver. It keeps the 17 ms IMU period, 100 ms MAG period, 50 ms dt clamp, gyro bias learning, acc reference, Mahony acc/mag correction, and `deg*100` state outputs.
- `Components/Inc/HeadingEstimator.h` and `Components/Src/HeadingEstimator.c` port the v1.1 heading estimator. It integrates gyro Z as the primary heading source and slowly fuses magnetic heading while the platform is static.
- `Components/Inc/MagCompass.h` and `Components/Src/MagCompass.c` split the v1.1 QMC6309 compass solver out of `User/MainLoop.c`, including axis mapping, install offset `219.30 deg`, direction sign `-1`, norm gate, jump gate, IIR filter, and 5-sample ready gate.
- `Platform/Src/platform_scheduler.c` now exposes `platform_scheduler_get_tick_ms()` from the 1 kHz Timer0 tick, so `App` can feed AHRS with real `dt_ms`.
- `App/Src/app.c` now polls IMU roughly every 17 ms and MAG roughly every 100 ms, feeds `AHRS_UpdateRaw6Axis()`, `AHRS_UpdateRawMag()`, `MagCompass_Update()`, and `Heading_Update()`, then exposes attitude/heading snapshots through `app_get_*` getters. Current static gating uses AHRS gyro-bias/acc/gyro-still checks; v1.1 motor-stop gating is left for the later control-layer port.
- C251 memory note: `ahrs_ctx` and the BoardDevices RF receive queue are placed in XDATA so the 256-byte C251 stack fits in EDATA; latest command-line build reports `0 Error(s), 0 Warning(s)`.
- Public API comments in the touched App, BoardDevices, ChipDrivers/parser and MCU abstraction headers use Chinese Doxygen to spell out layer ownership, timing/blocking notes and return meanings.

## 2026-05 state-machine wireless/control note

The current tree now aligns the old wireless/control behavior with the v2 layered structure:

- `App/Src/app.c` now performs `board_gps_init()` during bring-up and logs the init return code at startup.
- `BoardDevices/Src/board_gps.c` and `ChipDrivers/Src/gnss_nmea.c` already expose the old upper-layer dependent GPS fields, including:
  - `legacy_coord_valid`
  - `legacy_lon1`, `legacy_lon2`, `legacy_lat1`, `legacy_lat2`
  - `lat_deg1e7`, `lon_deg1e7`
  - `course_deg_x100`
  - `satellites_used_gsa`
- `App/Src/ship_protocol.c` `0x12` payload now follows the old report semantics:
  - satellite count prefers `satellites_used_gsa`
  - heading uses integer degrees from `course_deg_x100 / 100`
  - coordinates prefer the old split legacy fields and fall back to runtime conversion from `deg1e7`
  - `payload[13]` comes from `board_power`
  - `payload[14]` comes from `AutoDrive_InActive()`
  - transmit channel uses the old work TX path `rf_channel[2]`
- `0x11` now feeds `ShipControl_UpdateManualInput()` after boot/heading guards, and E-key cruise uses heading hold.
- `0x11` key-edge handling keeps A/E/unknown on the existing `SHIP_PROTOCOL_EVENT_KEY_EDGE` path. B/C/D key edges now publish `SHIP_PROTOCOL_EVENT_KEY_ACTION` with `SHIP_PROTOCOL_KEY_ACTION_B_NOOP`, `SHIP_PROTOCOL_KEY_ACTION_C_NOOP`, or `SHIP_PROTOCOL_KEY_ACTION_D_NOOP`; these are semantic no-op events and do not drive hardware.
- `ship_protocol_event_snapshot_t` now exposes `key_action`, `power`, and `spi_ps` observation fields for App-side or external subscribers.
- Protocol events are no longer a single overwritten slot: `ship_protocol_publish_event()` pushes snapshots into an 8-entry ring queue, `ship_protocol_take_event()` drains them FIFO, and `ship_protocol_get_event_snapshot()` remains a latest-snapshot helper.
- `App/Src/app.c` now drains protocol events in `app_ship_event_poll()` each loop. High-rate throttle/power sample events stay mostly quiet, while key/action/point/power-latch/SPI-PS/error events have explicit dispatch logs.
- `0x13/0x14/0x15` now dispatch to `AutoDrive_SetReturnPositionRaw()`,
  `AutoDrive_SetFishPositionRaw()`, and `AutoDrive_SetSwitchRaw()`.
- `0x14` logs fish-point result codes and matched indexes; unknown points are stored only while the RAM table has space.
- `0x16` sends the 36-byte AutoDrive diagnostic payload on state/reason changes or periodic diagnostic cadence.

Power reporting is also aligned back to the old discrete behavior for the current board:

- The actual board voltage-detect pin is `P0.0 / ADC_CH8`.
- ADC/GPIO access is inside `BoardDevices/Src/board_power.c`; `App/` only calls `board_power_read()`.
- Power sampling runs inside the 10 ms `ship_protocol_run_scheduler()` path, but the battery sample itself is down-sampled with `SHIP_POWER_SAMPLE_DIVIDER = 100`, so the effective ADC update period is about `1000 ms`.
- The computed sampling period is stored in global runtime state as `ship_protocol_rt.power_sample_period_ms`.
- Before the first down-sampled ADC read actually happens, the cache is marked as pending. Startup diagnostics therefore print `adc pending` instead of treating the initial `raw=0` cache value as a failed sample.
- `0x12 payload[13]` now carries the real old-style power level `0..4`, not raw ADC counts.
- Power sampling now publishes protocol observation events: unchanged valid samples use `SHIP_PROTOCOL_EVENT_POWER_SAMPLE`, level transitions use `SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED`, and both fill the snapshot `power` fields.
- Low-power latch now requires `power_level == 0`, more than `600` scheduler ticks, `AutoDrive_GetMode() == AUTO_DRIVE_CLOSE`, and `ShipControl_GetManualAccelerator() < 10`; when latched it publishes `SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED` and calls `AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_LOW_POWER)`.
- `bat_mv` is still a `1:1` placeholder engineering value until the real resistor-divider ratio is confirmed; the scale macros live in `BoardDevices/Inc/board_power.h` as `BOARD_POWER_BAT_SCALE_NUM/DEN`, and boot logs warn while `BOARD_POWER_BAT_MV_UNCALIBRATED` is set. The remote-compatible `0..4` level remains valid.

SPI-PS now has an App event bridge without changing the BoardDevices boundary:

- `app_init()` initializes `board_spi_ps`; if init fails, the main loop stays silent for SPI-PS.
- `app_loop()` polls `board_spi_ps_service()`, reads completed RX frames with `board_spi_ps_read()`, and publishes `SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX`.
- The SPI-PS event snapshot stores the status code, RX length, truncated stored length, and the first `16` bytes in `spi_ps.bytes`.
- In the current generated config SPI-PS is disabled and marked as sharing LT8920's STC SPI resource, so normal firmware logs the resource guard instead of enabling SPI-PS RX.
