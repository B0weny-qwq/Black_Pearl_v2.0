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
- 板级 IMU：`BoardDevices/Src/board_imu.c`，当前已完成 QMI8658 板级绑定，支持初始化、ready 刷新和原始采样读取。
- 板级磁力计：`BoardDevices/Src/board_mag.c`，当前已完成 QMC6309 板级绑定并复用传感器 IIC 总线。
- 板级传感器总线：`BoardDevices/Src/board_sensor_bus.c`，当前封装 DMA IIC，固定 P1.4/P1.5、400 kHz，供传感器链路复用。
- 板级 LT8920：`BoardDevices/Src/board_lt8920.c`，当前已封装 LT8920 + KCT8206 的最小 bring-up 和寄存器校验。
- 板级 SPI-PS：`BoardDevices/Src/board_spi_ps.c`
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
        -> board_imu_init()
        -> board_mag_init()
        -> board_lt8920_init()
     -> board_gps_init()
  -> loop
     -> platform_scheduler_run()
     -> app_loop()
        -> board_gps_poll()
        -> board_imu_service()
```

其中 `log_init()` 仅在 `board_console_init()` 成功时执行；设备 bring-up 本身不依赖
日志初始化，控制台异常时仍会继续初始化 IMU、磁力计和 LT8920。

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
  板级传感器总线私有适配层，当前固定使用 P1.4/P1.5、400 kHz，不对 `App/`
  暴露 IIC 细节。
- `ChipDrivers/Inc/QMI8658.h`、`ChipDrivers/Src/QMI8658.c`：QMI8658 寄存器层驱动，
  已支持地址探测、最小初始化、状态读取和原始数据读取。
- `BoardDevices/Inc/board_imu.h`、`BoardDevices/Src/board_imu.c`：板级 IMU 接口，
  当前已绑定 `QMI8658` + `board_sensor_bus`，初始化和读数都返回显式状态码。
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
