# 工程目录总览

本工程采用 EmbedForge Level 1.5 风格组织，用于把 Black Pearl v1.0 代码
迁移到更清晰、可维护、可复用的结构中，同时保留 STC 官方驱动和芯片资料的
参考价值。

## 目录结构

```text
Black_Pearl_v2.0/
├── App/
│   ├── Inc/
│   └── Src/
├── BoardDevices/
│   ├── Inc/
│   └── Src/
├── Components/
│   ├── Inc/
│   └── Src/
├── ChipDrivers/
│   ├── Inc/
│   └── Src/
├── Services/
│   ├── Inc/
│   └── Src/
├── Drivers/
│   ├── Inc/
│   ├── Src/
│   ├── Isr/
│   └── Lib/
├── McuAbstraction/
│   ├── Inc/
│   └── Src/
├── Platform/
│   ├── Inc/
│   └── Src/
├── Startup/
├── RVMDK/
├── .codex/
│   └── skills/
│       └── embedforge-project-standards/
│           └── SKILL.md
└── doc/
```

## 各层职责

`App/`

- 负责 `main()`、应用生命周期和高层业务编排。
- 当前调用链为 `platform_init()`、`app_init()`、`platform_scheduler_run()`、
  `app_loop()`。
- 当前 `app_init()` 已接入 `board_console_init()`、`log_init()`、
  `board_gps_init()`、`board_imu_init()`、`board_mag_init()`、`board_power_init()`、
  `board_wireless_init()`、`ship_protocol_init()`、`board_spi_ps_init()` 和
  `board_motor_init()`；
  `app_loop()` 当前周期调用 `board_gps_poll()`、
  `board_wireless_poll()`、`ship_protocol_run_scheduler()`、`board_wireless_search_signal_poll()`
  、`app_spi_ps_poll()`、`app_ahrs_poll()` 和 `board_motor_service()`。
- 当前已有 `ship_protocol`、`ship_control`、`autodrive` 三个 App 状态机：
  协议负责收发和分发，控制负责电机所有权，AutoDrive 负责 GPS 返航/去点规划。
- `App/Inc/app_config.h` 是当前应用运行档位入口，集中保存 v1.1 迁移来的手动控制、
  yaw 自稳、协议节拍和电源日志节流参数。
- 不应依赖 STC vendor 头文件、裸寄存器或裸端口。

`Platform/`

- 负责 STC 平台配置、时钟配置、启动胶水、平台调度 tick 和平台初始化。
- 保存 `config.h`、`stc32g.h`、`type_def.h`，因为这些属于 vendor/platform
  细节。
- 当前主时钟基线为 HIRC 24 MHz。

`Drivers/`

- 保存 STC32G 官方外设驱动、ISR 和 STC 库文件。
- 保留官方命名，便于对照芯片资料和旧版代码。

`McuAbstraction/`

- 保存基于 STC 官方驱动实现的统一外设抽象接口。
- 当前已有 `ef_uart`、`ef_iic`、`ef_spi`，供 `BoardDevices/` 组合具体板级设备。
- 这一层可以包含 STC 官方头文件，但不应该承载板级引脚和具体设备地址。

`ChipDrivers/`

- 保存外部芯片驱动和协议解析驱动，不直接绑定板级引脚、外设实例或任务调度。
- 当前已有 `gnss_nmea`、`QMI8658`、`QMC6309`、`LT8920`、`KCT8206`。
- 例如 `QMI8658` 只负责寄存器层读写和最小初始化，UART/IIC 端口与板级资源仍由
  `BoardDevices/` 决定。

`BoardDevices/`

- 保存板级设备 API，例如 LED、按键、显示屏、电机、传感器、总线和通信口。
- 当前已有 `board_console`，绑定 UART1/P3.1/P3.0 作为日志控制台。
- 当前已有 `board_gps`，绑定 UART2/P1.0/P1.1，把接收字节流喂给 `gnss_nmea`
  解析器。
- 当前已有 `board_sensor_bus`，封装板级 DMA IIC 访问，当前固定使用 P1.4/P1.5。
- 当前已有 `board_imu`，绑定 QMI8658 + `board_sensor_bus`，对上暴露板级 IMU 入口。
- 当前已有 `board_mag`，绑定 QMC6309，并复用 `board_sensor_bus`。
- 当前已有 `board_lt8920`，封装 LT8920 + KCT8206 的最小上电 bring-up。
- 当前已有 `board_wireless`，在 `board_lt8920` 之上封装 RX/TX、RF payload 队列、
  配对信道发送、工作信道切换和天线 RSSI 扫描。
- 当前已有 `board_motor`，封装左右电机 PWM 初始化、目标速度、停机、周期刷新和 PWM 快照。
- 当前已有 `board_power`，封装 `P0.0 / ADC_CH8` 电池采样和 `0..4` 等级换算；真实 `bat_mv` 仍待分压比例标定。
- 当前已有 `board_spi_ps`，但生成配置默认禁用 SPI-PS，并通过资源护栏标记其与 LT8920 共用 STC SPI 外设。
- 当前已有 `board_storage`，封装 STC EEPROM/IAP 读写/擦除。
- 当前已有 `board_spi_ps`，绑定 SPI P2.2/P2.3/P2.4/P2.5，实现官方
  `APP_SPI_PS` 的对等主从切换模型。
- 应隐藏引脚、通道、极性和 STC 驱动细节，不让 `App/` 直接接触硬件资源。

`Components/`

- 预留给纯算法、滤波器、PID、状态机、解析器、缓冲区等可复用逻辑。
- 当前已有 `PID` 和 `Filter` 两个组件，后续继续承接 v1.0 算法模块迁移。
- 不应包含 vendor 头文件，也不应直接访问板级资源。

`Services/`

- 保存轻量系统服务，例如日志、控制台、参数管理。
- 当前已有 `logger` 日志服务，公开 `LOGI/LOGW/LOGE/LOGD/log_printf`。
- 当前已有 `parameter_store`，通过 `board_storage` 保存 AutoDrive 配置字节。
- 服务层不能直接调用 STC 官方驱动或裸寄存器；硬件输出通过 `BoardDevices/`
  或抽象接口完成。

`Startup/`

- 保存启动和中断向量相关汇编文件。

`RVMDK/`

- 保存 Keil 工程文件。
- 当前有效分组包括 `Startup`、`Platform`、`App`、`BoardDevices`、
  `Components`、`ChipDrivers`、`McuAbstraction`、`Services`、`Drivers`、
  `Drivers_ISR`、`Drivers_LIB`。

`.codex/skills/embedforge-project-standards/`

- 保存项目内自带的 EmbedForge 工程规范副本。
- 用于在切换 Codex 环境、账号或主机时，仍能从仓库本身读取相同的工程约束。

## 依赖方向

推荐依赖方向：

```text
App -> BoardDevices -> McuAbstraction -> Drivers -> Platform/STC registers
App -> BoardDevices -> ChipDrivers -> McuAbstraction -> Drivers -> Platform/STC registers
App -> Services -> BoardDevices
App -> Components
Platform -> Drivers/STC registers
ChipDrivers -> McuAbstraction
McuAbstraction -> Drivers -> Platform config/STC registers
Drivers -> Platform config/STC registers
```

## 迁移原则

从 v1.0 或本地参考代码迁移时，不要把 sample 代码直接粘到 `App/`。应先
识别它实际代表的硬件能力，再放入 `BoardDevices/` 或 `Platform/`；`App/`
只保留高层调用顺序和业务状态。

## 当前模块综述

### UART1 控制台、日志与 GPS

当前接收链路已经拆分为板级串口、日志服务和 GNSS 解析三段：

- `McuAbstraction/Inc/ef_uart.h`、`McuAbstraction/Src/ef_uart.c`：UART 统一封装。
- `BoardDevices/Inc/board_console.h`、`BoardDevices/Src/board_console.c`：
  板级 console，绑定 UART1/P3.1/P3.0/115200 8N1。
- `BoardDevices/Inc/board_gps.h`、`BoardDevices/Src/board_gps.c`：
  板级 GPS，绑定 UART2/P1.0/P1.1/115200，并周期轮询串口接收缓冲。
- `ChipDrivers/Inc/gnss_nmea.h`、`ChipDrivers/Src/gnss_nmea.c`：
  NMEA0183 解析器，只负责协议解析和状态快照维护。
- `Services/Inc/logger.h`、`Services/Src/logger.c`：轻量日志服务。

兼容入口：

- `Services/Inc/Log.h` 仅转发到 `logger.h`，用于降低 v1.0 代码迁移时的 include
  修改量。

调用顺序：

```text
app_init()
  -> board_console_init()
  -> log_init()
  -> 输出版本号
  -> board_gps_init()
  -> board_imu_init()
  -> board_mag_init()
  -> board_power_init()
  -> board_wireless_init()
  -> ship_protocol_init()
  -> board_spi_ps_init()
  -> board_motor_init()
app_loop()
  -> board_gps_poll()
  -> board_wireless_poll()
  -> ship_protocol_run_scheduler()
  -> board_wireless_search_signal_poll()
  -> app_spi_ps_poll()
  -> app_ahrs_poll()
  -> board_motor_service()
```

`App/` 可以调用日志宏，但不能直接初始化 UART，也不能包含 `STC32G_UART.h`。
设备 bring-up 本身不依赖日志初始化；若 `board_console_init()` 失败，只是日志被丢弃，
不会阻止 IMU、磁力计和无线链路继续初始化。

### 传感器总线、QMI8658/QMC6309/LT8920 与滤波

- `McuAbstraction/Inc/ef_iic.h`、`McuAbstraction/Src/ef_iic.c`：DMA IIC 统一封装。
- `BoardDevices/Src/board_sensor_bus.c`、`BoardDevices/Src/board_sensor_bus.h`：
  板级传感器总线适配层，当前固定 P1.4/P1.5、100 kHz，并对上收敛成简单寄存器读写接口。
- `ChipDrivers/Inc/QMI8658.h`、`ChipDrivers/Src/QMI8658.c`：QMI8658 寄存器层驱动，
  已支持地址探测、可靠上电等待、ready 轮询、状态读取和原始采样读取；当前移植已在板上验证成功。
- `BoardDevices/Inc/board_imu.h`、`BoardDevices/Src/board_imu.c`：
  板级 IMU 接口，当前已绑定 `QMI8658` + `board_sensor_bus`，用于初始化、ready 刷新和原始采样读取；应用启动时优先初始化 IMU，再初始化同总线上的 QMC6309。启动成功前会等待 `STATUS0` accel/gyro ready 并读取首帧，避免寄存器配置成功但数据路径异常时误报 ready。
- `ChipDrivers/Inc/QMC6309.h`、`ChipDrivers/Src/QMC6309.c`：QMC6309 地磁计寄存器层驱动。
- `BoardDevices/Inc/board_mag.h`、`BoardDevices/Src/board_mag.c`：
  板级磁力计接口，当前已绑定 QMC6309，并复用 `board_sensor_bus`。
- `McuAbstraction/Inc/ef_spi.h`、`McuAbstraction/Src/ef_spi.c`：
  SPI 统一封装，当前额外提供 `ef_spi_transfer_byte()` 供 LT8920 全双工寄存器访问。
- `ChipDrivers/Inc/LT8920.h`、`ChipDrivers/Src/LT8920.c`：LT8920 无线芯片寄存器层驱动。
- `ChipDrivers/Inc/KCT8206.h`、`ChipDrivers/Src/KCT8206.c`：射频前端控制层。
- `BoardDevices/Inc/board_lt8920.h`、`BoardDevices/Src/board_lt8920.c`：
  板级 LT8920 bring-up，隐藏 SPI 路由、RST、ANT_SEL、RXEN、TXEN 和寄存器校验细节。
- `BoardDevices/Inc/board_wireless.h`、`BoardDevices/Src/board_wireless.c`：
  板级无线链路管理层，隐藏 LT8920/KCT8206 实例绑定和天线控制，向 `App/`
  暴露 `BOARD_WIRELESS_MODE_IDLE/RX/TX`、收发队列、信道/sync 寄存器配置和搜索信号接口。
- `BoardDevices/Inc/board_power.h`、`BoardDevices/Src/board_power.c`：
  板级电池采样接口，隐藏 ADC 通道、GPIO 和电量等级换算。
- `BoardDevices/Inc/board_storage.h`、`BoardDevices/Src/board_storage.c`：
  板级 EEPROM/IAP 存储接口，隐藏 STC EEPROM 驱动。
- `Services/Inc/parameter_store.h`、`Services/Src/parameter_store.c`：
  轻量参数服务，当前用于保存 AutoDrive 返航开关和返航点配置。
- `App/Inc/ship_protocol.h`、`App/Src/ship_protocol.c`：
  船端协议状态机，状态为 `SHIP_PROTOCOL_STATE_BOOT_WAIT/PAIR_SEND/PAIR_WAIT_RSP/WORK_RX`。
  当前保留旧帧格式 `AA | len | cmd | payload | xor | BB`，固定 pair channel `0x7F`、
  seed `65 65 A0 65`、pair send count `10`，旧 seed 派生 `work_rx=13`、
  `work_tx=77`、`key=32/30`、`reg36=0x2020`、`reg39=0x1E1E`；最后一次
  `PAIR_REQ` 后开启 500 tick 的 `PAIR_RSP(0x0F)` 响应窗口，合法协议帧后发送
  cmd `0x12` + 15 字节 GPS payload 回包。
`0x11/0x13/0x14/0x15` 已分发到 `ShipControl` 和 `AutoDrive`，并保留
`ship_protocol_get_event_snapshot()` / `ship_protocol_take_event()` 供联调观察。
当前事件不是单槽覆盖：协议层维护 8 深度环形队列，`take_event()` 按 FIFO 消费；
`get_event_snapshot()` 只用于查看最近一次事件快照。
  `0x16` 由船端主动上报 AutoDrive 诊断，不替代 `0x12`。
- `App/Inc/app_config.h`：
  应用运行档位配置，当前对齐 v1.1 的 `SHIP_RC_AXIS_MAX_DELTA=60`、
  `SHIP_YAW_HOLD_DIFF_LIMIT_PERMILLE=320`、`SHIP_YAW_HOLD_GYRO_DAMP_Q10=4096`、
  `SHIP_YAW_HOLD_KP_Q10=384`、`SHIP_YAW_HOLD_KD_Q10=96` 和
  `SHIP_POWER_LOG_PERIOD_MS=10000 ms`。
- `App/Inc/ship_control.h`、`App/Src/ship_control.c`：
  船体控制状态机，模式覆盖开机保护、等待航向、手动开环、手动航向保持、
  E 键巡航、GPS 导航航向保持和 failsafe 停机。所有最终左右电机输出统一经过
  `board_motor`。
- `App/Inc/autodrive.h`、`App/Src/autodrive.c`：
  GPS AutoDrive 状态机，状态为 `IDLE/START/ALIGN/RUN/ARRIVE/REJECT/TIMEOUT/STOP`。
  它负责返航点、钓点表、距离/目标航向、对齐判定和诊断快照；电机输出仍通过
  `ShipControl_RequestGpsAlign()` / `ShipControl_RequestGpsNav()`。
- `App/Inc/autodrive_config.h`、`App/Src/autodrive_config.c`：
  AutoDrive 配置适配层，通过 `parameter_store` 读写配置，不触碰 STC EEPROM 驱动。
- `Components/Inc/Filter.h`、`Components/Src/Filter.c`：三轴传感器一阶低通
  滤波组件，保留旧 `Filter_*` 接口命名，便于后续接入 QMI8658/QMC6309。

### SPI-PS 对等通信

SPI-PS 参考实现已拆为两层：

- `McuAbstraction/Inc/ef_spi.h`、`McuAbstraction/Src/ef_spi.c`：SPI 统一封装。
- `BoardDevices/Inc/board_spi_ps.h`、`BoardDevices/Src/board_spi_ps.c`：板级
  SPI-PS 对等链路。

板级资源：

- SS：P2.2
- MOSI：P2.3
- MISO：P2.4
- SCLK：P2.5
- 默认模式：从机，SS 由引脚决定，MSB first，CPOL Low，CPHA 2Edge，Fosc/4。

行为边界：

- `board_spi_ps_send()` 负责抢主发送和发送后退回从机。
- `board_spi_ps_service()` 负责推进从机接收超时判帧。
- `board_spi_ps_read()` 负责读取并清空 ISR 接收缓冲。
- UART2 回显桥接是参考应用逻辑，不放入板级 SPI-PS 抽象。

Current SPI-PS App bridge:

- `app_init()` calls `board_spi_ps_init()` and records whether SPI-PS is available.
- `app_loop()` calls `app_spi_ps_poll()` after wireless/protocol servicing.
- `app_spi_ps_poll()` runs `board_spi_ps_service()`, then reads completed RX frames with `board_spi_ps_read()`.
- Completed reads and overflow/truncated reads publish `SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX`; disabled or failed init is silent.
- Current generated config keeps `EF_BOARD_SPI_PS_ENABLED = 0U`.
- `EF_BOARD_SPI_PS_SHARES_LT8920_SPI = 1U` documents the current hardware/resource risk. If SPI-PS is enabled without first clearing that guard, `board_spi_ps_init()` returns `BOARD_SPI_PS_ERR_RESOURCE` instead of reconfiguring the STC SPI peripheral used by LT8920.

## v1.1 AHRS/MAG solver port

The v1.1 IMU and magnetometer data-solving path is now split by layer:

- `Components/AHRS`: quaternion attitude solver for QMI8658 acc/gyro plus optional mag correction.
- `Components/HeadingEstimator`: gyro-Z heading integration with static magnetic heading fusion.
- `Components/MagCompass`: QMC6309 raw magnetic compass heading, install offset, direction sign, norm/jump gates, and IIR ready filter.
- `Platform/platform_scheduler`: exposes `platform_scheduler_get_tick_ms()` for real AHRS `dt_ms`.
- `App/app`: owns polling and lifecycle only. It reads `board_imu` and `board_mag`, feeds the components at the v1.1 periods, logs low-rate `AHRS`/`HDG` diagnostics, and exposes attitude/heading getters.

This keeps `Components` free of board, I2C, STC, and logging dependencies while preserving the v1.1 solver behavior.

### C251 memory layout and resource discipline

The current C251 build keeps the 256-byte startup stack in EDATA and moves large non-hot-path
runtime storage into XDATA:

- `Components/Src/AHRS.c`: `ahrs_ctx` is in XDATA because it contains the quaternion state,
  filter state and diagnostic snapshot.
- `BoardDevices/Src/board_wireless.c`: the RF receive payload queue is in XDATA because it stores
  up to four 60-byte packets.
- `ChipDrivers/Src/LT8920.c`: the LT8920 default register profile uses `EF_CODE_CONST`, keeping the
  170-byte table out of the constrained HCONST area.
- `board_wireless_receive()` still copies payloads to caller buffers, so App/User code does not see
  XDATA pointers or LT8920 packet storage.

Latest command-line C251 verification:

```text
Program Size: data=11.7 edata+hdata=3873 xdata=380 const=3597 code=61343
".\list\STC32G-LIB" - 0 Error(s), 0 Warning(s).
```

Public interface documentation was also tightened for this C251-ready baseline. The App,
BoardDevices, ChipDrivers/parser and MCU abstraction headers touched by the port now describe
ownership, blocking behavior or timing units where relevant, parameter validity and return codes in
Chinese Doxygen comments. This makes the layer boundary explicit at the API surface instead of only
in the implementation files.

## 2026-05 protocol, control and storage layering note

To keep the old wireless/control behavior without breaking the v2 layering boundary, the current design is:

- `BoardDevices/board_gps`: owns UART2 byte intake and GNSS state snapshot.
- `ChipDrivers/gnss_nmea`: owns NMEA parsing and legacy-coordinate compatibility fields.
- `BoardDevices/board_power`: owns `P0.0 / ADC_CH8` sampling, battery level conversion, and the centralized `BOARD_POWER_BAT_SCALE_NUM/DEN` calibration point.
- `BoardDevices/board_storage`: owns STC EEPROM/IAP access.
- `BoardDevices/board_spi_ps`: owns the SPI-PS peer link resources and RX/TX buffering.
- `Services/parameter_store`: owns compact config byte save/load over `board_storage`.
- `App/app`: owns lifecycle polling, including the SPI-PS service/read bridge and protocol-event drain.
- `App/ship_protocol`: owns old wireless payload packing, command dispatch, `0x12` field order, key/power/SPI-PS observation event queue, link timeout and `0x16` diagnostic cadence.
- `App/ship_control`: owns motor output and all manual/cruise/GPS yaw-hold modes.
- `App/autodrive`: owns return-home/goto-point planning, fish-point table, target heading, arrival and diagnostics.

This means the old protocol-visible behavior is restored in `App/Src/ship_protocol.c`, while physical resources stay behind BoardDevices:

- old protocol behavior: kept
- current hardware ADC pin/channel: `P0.0 / ADC_CH8`
- old-board ADC channel: not reused

The current `0x12`, power and AutoDrive chain should therefore be understood as:

1. `board_gps_poll()` updates GNSS state.
2. `ship_protocol_run_scheduler()` runs every `10 ms`.
3. `ship_protocol_low_power_check()` performs down-sampled `board_power_read()` servicing.
4. `ship_protocol_send_gps_once()` packs old-format `0x12`.
5. `payload[13]` reports the cached old-style power level.
6. `payload[14]` reports `AutoDrive_InActive()`.
7. `0x13/0x14/0x15` call AutoDrive real entries; AutoDrive submits movement through `ShipControl`.
8. B/C/D key edges publish `SHIP_PROTOCOL_EVENT_KEY_ACTION` with `B_NOOP`, `C_NOOP`, or `D_NOOP`; A remains the existing A-light `KEY_EDGE` log path and does not drive hardware.
9. Valid power samples publish `SHIP_PROTOCOL_EVENT_POWER_SAMPLE`; level changes publish `SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED`.
10. Low power latches only when `power_level == 0`, more than `600` scheduler ticks have elapsed, AutoDrive mode is `AUTO_DRIVE_CLOSE`, and manual accelerator is below `10`; the latch publishes `SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED`.
11. `app_spi_ps_poll()` publishes `SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX` for completed SPI-PS RX frames when SPI-PS initialization succeeded.
12. `app_ship_event_poll()` drains all queued protocol events in FIFO order once per main loop.
13. Link timeout can trigger AutoDrive/ShipControl through explicit state-machine APIs.

## 烧录后业务逻辑

当前固件烧录进去后就是船端控制程序：

- 启动 UART1 `115200 8N1` 日志，便于看 bring-up 和协议状态。
- 初始化 GPS、QMI8658、QMC6309、电源 ADC、LT8920/KCT8206、电机和参数存储。
- 在 `0x7F` 信道发送 `PAIR_REQ(0x10)`，派生 `work_rx=13`、`work_tx=77` 后进入工作 RX；状态/诊断回包实际使用旧工作 RX 信道。
- 手动控制采用 v1.1 当前运行档位：遥控轴满量程按中心 `100 +/- 60` 映射，yaw 自稳采用
  `20%` 手动进入门限、2 帧稳定确认、`320 permille` 差速限幅、`4096` gyro 阻尼和
  `384/0/96` PID。
- 接收旧遥控/上位机帧：`0x11` 控制手动/巡航，`0x13` 返航点，`0x14` 钓点，`0x15` 返航开关。
- 通过 `ShipControl` 统一写电机，通过 `AutoDrive` 处理返航、去点、对齐、到达和低电返航。
- 回发旧格式 `0x12` GPS/status，主动发 `0x16` AutoDrive 诊断；两者都不替换旧遥控器依赖的 15 字节 `0x12`。

兼容边界：

- 兼容旧 `AA | len | cmd | payload | xor | BB` 空口帧。
- 兼容旧 `0x12` 固定 15 字节 payload；`payload[13]` 是 `0..4` 电量，`payload[14]` 是 AutoDrive 状态。
- 新增 `0x16` 只做诊断，不替代旧上位机/遥控依赖的 `0x12`。
- `bat_mv uncal` 只说明真实电池毫伏值分压比例待标定，不影响旧协议电量等级字段。
