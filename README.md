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
- 平台初始化：`Platform/Src/platform.c`
- 板级控制台：`BoardDevices/Src/board_console.c`
- 板级 IMU：`BoardDevices/Src/board_imu.c` 已预留 QMI8658 入口，底层传输未接入。
- 板级 SPI-PS：`BoardDevices/Src/board_spi_ps.c`
- 低通滤波组件：`Components/Src/Filter.c`
- MCU 抽象层：`McuAbstraction/Src/`
- 轻量日志：`Services/Src/logger.c`
- 官方 STC 驱动：`Drivers/`

## 分层规则

- `App/` 只放真实项目的应用编排、状态机和主循环。
- `BoardDevices/` 后续用于隐藏板级引脚、极性、总线、通道和外设实例选择。
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

## 迁移流程

1. 从芯片资料、旧版工程或本地参考代码提取需要的逻辑。
2. 将板级硬件访问迁移到 `BoardDevices/`。
3. 将纯算法、状态逻辑和数据处理迁移到 `Components/`。
4. `App/` 只保留初始化顺序、状态机调度和高层控制流。
5. 当板级 API、时钟假设或构建边界变化时，同步更新 `doc/` 文档。

## 当前已迁移模块

### LOG 轻量级日志

旧版 `Code_boweny/Function/Log/` 已按新结构拆分：

- `McuAbstraction/Inc/ef_uart.h`、`McuAbstraction/Src/ef_uart.c`：UART 统一封装，内部调用
  STC 官方 UART/NVIC 驱动。
- `McuAbstraction/Inc/ef_iic.h`、`McuAbstraction/Src/ef_iic.c`：IIC 统一封装，当前采用
  STC DMA IIC 参考时序实现寄存器连续读写。
- `Components/Inc/Filter.h`、`Components/Src/Filter.c`：三轴传感器一阶低通
  滤波组件，保留旧版 `Filter_*` API，供 IMU/地磁计读数链路复用。
- `BoardDevices/Inc/board_console.h`、`BoardDevices/Src/board_console.c`：板级
  console，固定使用 UART1，P3.1=TXD，P3.0=RXD，115200 8N1。
- `Services/Inc/logger.h`、`Services/Src/logger.c`：日志服务，提供
  `LOGI/LOGW/LOGE/LOGD/log_printf`。
- `Services/Inc/Log.h`：旧代码 include 兼容入口，后续迁移 v1.0 代码时可临时
  保持 `#include "Log.h"`。

初始化顺序：

```text
platform_init()
app_init()
  -> board_console_init()
  -> log_init()
```

日志输出格式保持旧版口径：

```text
[tag] I: message\r\n
[tag] W: message\r\n
[tag] E: message\r\n
[tag] D: message\r\n
```

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
