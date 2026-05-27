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

## 外包对接指南

本工程按 `App -> BoardDevices -> McuAbstraction/ChipDrivers/Drivers` 和
`App -> Components` 分层。外包人员新增业务时先确认改动属于哪一层：

- 只改按键动作、状态联动、LED 闪烁节奏、提示逻辑：改 `App/Src/app_extension.c`。
- 新增真实硬件，例如 LED、蜂鸣器、舵机：先在 `BoardDevices/` 增加 `board_xxx` API，再由 `app_extension.c` 调用。
- 改电机控制策略：改 `App/Src/ship_control.c`，最终输出仍必须走 `board_motor_set_both_speed()`。
- 改无线协议、按键语义、GPS 回包：改 `App/Src/ship_protocol.c`。
- 改返航、钓点、GPS 定点逻辑：改 `App/Src/autodrive.c`。
- 改 AHRS、滤波、PID、航向算法：改 `Components/`，不要访问硬件。

### 保留的扩展 API

外包插入点固定在 `App/Inc/app_extension.h` 和 `App/Src/app_extension.c`：

- `app_extension_init()`：扩展状态初始化。
- `app_extension_poll(now_ms)`：主循环轮询，定时动作必须用 `now_ms` 非阻塞驱动。
- `app_extension_on_ship_event(event)`：协议事件观察回调，按键、返航、钓点、电量、SPI-PS 事件都会经过这里。

例子：后续要做“按 A 键 LED 闪烁”，不要在 `ship_protocol.c` 或 `app.c` 里直接写引脚。
正确路径是：

```text
BoardDevices/Inc/board_led.h
BoardDevices/Src/board_led.c
  -> board_led_init()
  -> board_led_set()

App/Src/app.c
  -> bring-up 阶段调用 board_led_init()

App/Src/app_extension.c
  -> app_extension_on_ship_event() 识别 SHIP_PROTOCOL_KEY_A_LIGHT
  -> app_extension_poll(now_ms) 按时间翻转 board_led_set()
```

### 上位机卡片到底层链路

当前上位机入口是 `tools/ship_log_viewer/ship_log_viewer.html`。每张卡片的数据源如下：

| 卡片 | 固件日志 | App 源头 | 底层来源 |
| --- | --- | --- | --- |
| 控制模式 | `[CTRL] I: event=mode old=... new=...` | `ShipControl_LogModeEvent()` | `ShipControl_SetMode()` |
| 电机输出 | `[CTRL] I: out m=... mo=... th=... base=... st=... df=... l=... r=...` | `ShipControl_LogMotorOutput()` | `board_motor_set_both_speed()` |
| 角度闭环 | 同上，模式 `MANUAL_YAW_HOLD/CRUISE_HEADING_HOLD/GPS_NAV_HEADING_HOLD` | `ShipControl_ApplyYawHoldTargetEx()` | `app_get_heading_*()` + `PID` |
| 遥控输入 | `[SHIP] I: rc cmd=0x11 lr=... ud=...` | `ship_protocol_handle_throttle()` | `board_wireless_receive()` |
| 按键状态 | `[SHIP] I: key edge key=... action=0..3`、`[EVT] I: key/act ...` | `ship_protocol_handle_key_edge()`、`app_dispatch_ship_event()` | 0x11 payload key 字节 |
| 电量采样 | `[SHIP] I: adc raw=... mv=... bat=... p=...` | `ship_protocol_log_power_sample()` | `board_power_read()` |
| AHRS R/P/Y | `[AHRS] I: rpy=... gy=... flg=...` | `app_ahrs_log()` | `board_imu_read()` + `AHRS_UpdateRaw6Axis()` |
| 地磁数据 | `[MAG] I: raw=... norm=... yaw=... self=...` | `app_ahrs_log()` | `board_mag_read()` + `MagCompass_Update()` |
| 船头朝向/HDG | `[HDG] I: abs=... rel=... mag=...` | `app_ahrs_log()` | `Heading_Update()` |
| GPS 状态回包 | `0x12` payload、verbose 档位下的 `[SHIP] I: tx cmd=12 ...` | `ship_protocol_build_gps_payload()` | `board_gps_get_state()` |
| AutoDrive | `[SHIP] I: tx16 st=... md=...`、`0x13/0x14/0x15` 日志 | `AutoDrive_GetDebugSnapshot()`、`AutoDrive_*Raw()` | `board_gps_get_state()` + `ShipControl_RequestGps*()` |

扩展到页面全部显示项的追踪关系如下：

| 页面显示项 | 日志/事件来源 | 底层追踪 |
| --- | --- | --- |
| 配对状态 | `pair req sent`、`pair ok ...`、`enter work-state` | `ship_protocol_try_pair_send()` / `ship_protocol_handle_pair_rsp()` -> `board_wireless_*()` |
| 遥控链路 | `rc cmd=0x11`、`remote timeout`、完整 AA...BB 帧 | `ship_protocol_poll_rx_frames()` -> `board_wireless_receive()` |
| 动作 / 巡航 | `rc cmd=0x11`、`cruise enter/exit`、`CTRL out` | `ship_protocol_handle_throttle()` -> `ShipControl_UpdateManualInput()` |
| 自稳定状态 | `CTRL out` 的 v2 模式 `5/6/7` | `ShipControl_ApplyYawHoldTargetEx()` -> `Heading_Update()` |
| 状态回包 | `0x12` payload；verbose 档位下另有 `tx cmd=12 ch=... len=...` | `ship_protocol_send_gps_once()` -> `board_wireless_send_on_channel()` |
| 定位有效 | `gps state fix=...` 或 `gps12 ... fix=...` | `board_gps_get_state()` -> `gnss_nmea` |
| 卫星数 | `gps sat source ...`、`gps12 ... sat=...` | `board_gps_get_state()` |
| 经度/纬度 | `gps state lon=... lat=...`、`gps12 ... lon=... lat=...` | `gnss_nmea` legacy/deg1e7 字段 |
| 航向角 | `gps state angle=...`、`0x12 angle` | `board_gps_get_state()->course_deg_x100` |
| 最近 0x12 | `0x12` payload；verbose 档位下可看 `tx cmd=12` | `ship_protocol_build_gps_payload()` |
| 遥控器旧格式 | `gps payload oldfmt`、`gps payload bytes`、`0x12` payload | `ship_protocol_to_legacy_nmea_coord()` |
| 返航点 | `0x13 ret`、`coord return-home` | `AutoDrive_SetReturnPositionRaw()` |
| 目标点 | `0x14 fish`、`0x14 rx ... save/nav` | `AutoDrive_SetFishPositionRaw()` |
| 返航开关 | `0x15 sw=...` | `AutoDrive_SetSwitchRaw()` -> `parameter_store` |
| 告警 / 错误 | `[... ] W/E: ...` | 对应模块日志，硬件问题继续追 BoardDevices |

如果上位机某张卡片为空，先看对应日志是否出现，再沿表格向下追到底层 API。

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
- App 扩展入口：`App/Src/app_extension.c`，当前保留按键/事件回调和主循环轮询插入点，供外包追加 LED 闪烁等业务。
- 应用运行档位：`App/Inc/app_config.h`，集中保存从 v1.1 迁移来的手动控制、yaw 自稳、协议配对、电源日志节流和 C251 日志瘦身开关。
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
        -> app_extension_init()
  -> loop
     -> platform_scheduler_run()
     -> app_loop()
        -> board_gps_poll()
        -> board_wireless_poll()
        -> ship_protocol_run_scheduler()
        -> board_wireless_search_signal_poll()
        -> app_spi_ps_poll()
        -> app_ship_event_poll()
        -> app_extension_poll()
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
- `App/Inc/app_config.h`：v2 当前应用运行档位来源，集中定义 v1.1 遥控轴量程 `60`、
  yaw 自稳差速限幅 `320 permille`、阻尼/PID 参数、配对 burst 节拍和电源日志周期。
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
- `App/Src/app.c` now polls IMU roughly every 17 ms and MAG roughly every 100 ms, feeds `AHRS_UpdateRaw6Axis()`, `AHRS_UpdateRawMag()`, `MagCompass_Update()`, and `Heading_Update()`, then exposes attitude/heading snapshots through `app_get_*` getters. Static heading gating matches v1.1 by requiring stopped control mode, zero motor speed, gyro-bias ready, valid acceleration, and still gyro readings before magnetometer heading can seed or correct the fused heading.
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
  - transmit channel uses the old work RX channel path `rf_channel[0]`; derived `work_tx=77` remains a compatibility parameter/log value
- `0x11` now feeds `ShipControl_UpdateManualInput()` after boot/heading guards, and E-key cruise uses heading hold.
- `0x11` key-edge handling keeps A/E/unknown on the existing `SHIP_PROTOCOL_EVENT_KEY_EDGE` path. B/C/D key edges now publish `SHIP_PROTOCOL_EVENT_KEY_ACTION` with `SHIP_PROTOCOL_KEY_ACTION_B_NOOP`, `SHIP_PROTOCOL_KEY_ACTION_C_NOOP`, or `SHIP_PROTOCOL_KEY_ACTION_D_NOOP`; these are semantic no-op events and do not drive hardware.
- C251 默认使用短日志档位：`SHIP_PROTOCOL_VERBOSE_LOG_ENABLE=0`、`SHIP_APP_BRINGUP_VERBOSE_LOG_ENABLE=0`。默认日志保留上位机卡片必需的 `[CTRL]`、`[AHRS]`、`[HDG]`、`adc raw`、`rc cmd=0x11`、`key edge key=... action=0..3`、`0x14 rx save=... nav=... idx=...` 和错误日志；打开 verbose 可恢复发送成功、pair payload、0x14 frame/payload 等长日志，但会增加 CODE/HCONST 占用。
- Manual control and yaw-hold tuning now come from `App/Inc/app_config.h`, aligned with the current v1.1 runtime profile: `SHIP_RC_AXIS_MAX_DELTA=60`, `SHIP_MANUAL_YAW_HOLD_DIFF_PERCENT=20`, `SHIP_YAW_HOLD_DIFF_LIMIT_PERMILLE=320`, `SHIP_YAW_HOLD_DERATE_START_CD=1000`, `SHIP_YAW_HOLD_DERATE_FULL_CD=2000`, `SHIP_YAW_HOLD_GYRO_DAMP_Q10=4096`, `SHIP_YAW_HOLD_KP_Q10=384`, and `SHIP_YAW_HOLD_KD_Q10=96`. Manual yaw hold also uses the v1.1 diff-ratio gate and 2-frame stable gate instead of requiring exact zero steering.
- `ship_protocol_event_snapshot_t` now exposes `key_action`, `power`, and `spi_ps` observation fields for App-side or external subscribers.
- Protocol events are no longer a single overwritten slot: `ship_protocol_publish_event()` pushes snapshots into an 8-entry ring queue, `ship_protocol_take_event()` drains them FIFO, and `ship_protocol_get_event_snapshot()` remains a latest-snapshot helper.
- `App/Src/app.c` now drains protocol events in `app_ship_event_poll()` each loop. High-rate throttle/power sample events stay mostly quiet, while key/action/point/power-latch/SPI-PS/error events have explicit dispatch logs.
- `0x13/0x14/0x15` now dispatch to `AutoDrive_SetReturnPositionRaw()`,
  `AutoDrive_SetFishPositionRaw()`, and `AutoDrive_SetSwitchRaw()`.
- `0x14` logs save and navigation results separately. A new unknown point is stored while the RAM table has space and then immediately attempts goto when GPS/distance allow; quick duplicate frames are suppressed.
- `0x16` sends the 36-byte AutoDrive diagnostic payload on state/reason changes or periodic diagnostic cadence.

Power reporting is also aligned back to the old discrete behavior for the current board:

- The actual board voltage-detect pin is `P0.0 / ADC_CH8`.
- ADC/GPIO access is inside `BoardDevices/Src/board_power.c`; `App/` only calls `board_power_read()`.
- Power sampling runs inside the 10 ms `ship_protocol_run_scheduler()` path, but the battery sample itself is down-sampled with `SHIP_POWER_SAMPLE_DIVIDER = 100`, so the effective ADC update period is about `1000 ms`; normal ADC log output is throttled by `SHIP_POWER_LOG_PERIOD_MS = 10000 ms`.
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
