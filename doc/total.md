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
└── doc/
```

## 各层职责

`App/`

- 负责 `main()`、应用生命周期和高层业务编排。
- 当前调用链为 `platform_init()`、`app_init()`、`platform_scheduler_run()`、
  `app_loop()`。
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

`BoardDevices/`

- 保存板级设备 API，例如 LED、按键、显示屏、电机、传感器、总线和通信口。
- 当前已有 `board_console`，绑定 UART1/P3.1/P3.0 作为日志控制台。
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
- 服务层不能直接调用 STC 官方驱动或裸寄存器；硬件输出通过 `BoardDevices/`
  或抽象接口完成。

`Startup/`

- 保存启动和中断向量相关汇编文件。

`RVMDK/`

- 保存 Keil 工程文件。
- 当前有效分组包括 `Startup`、`Platform`、`App`、`BoardDevices`、
  `Components`、`McuAbstraction`、`Services`、`Drivers`、`Drivers_ISR`、
  `Drivers_LIB`。

## 依赖方向

推荐依赖方向：

```text
App -> BoardDevices -> McuAbstraction -> Drivers -> Platform/STC registers
App -> Services -> BoardDevices
App -> Components
Platform -> Drivers/STC registers
McuAbstraction -> Drivers -> Platform config/STC registers
Drivers -> Platform config/STC registers
```

## 迁移原则

从 v1.0 或本地参考代码迁移时，不要把 sample 代码直接粘到 `App/`。应先
识别它实际代表的硬件能力，再放入 `BoardDevices/` 或 `Platform/`；`App/`
只保留高层调用顺序和业务状态。

## 当前模块综述

### UART1 控制台与日志

旧版 `Code_boweny/Function/Log/` 已迁移为三段：

- `McuAbstraction/Inc/ef_uart.h`、`McuAbstraction/Src/ef_uart.c`：UART 统一封装。
- `McuAbstraction/Inc/ef_iic.h`、`McuAbstraction/Src/ef_iic.c`：DMA IIC 统一封装，供
  `BoardDevices/` 绑定具体板级传感器使用。
- `Components/Inc/Filter.h`、`Components/Src/Filter.c`：三轴传感器一阶低通
  滤波组件，保留旧 `Filter_*` 接口命名，便于后续接入 QMI8658/QMC6309。
- `BoardDevices/Inc/board_console.h`、`BoardDevices/Src/board_console.c`：
  板级 console，绑定 UART1/P3.1/P3.0/115200 8N1。
- `BoardDevices/Inc/board_imu.h`、`BoardDevices/Src/board_imu.c`：
  QMI8658 板级 IMU 入口占位，底层传输未接入。
- `Services/Inc/logger.h`、`Services/Src/logger.c`：轻量日志服务。

兼容入口：

- `Services/Inc/Log.h` 仅转发到 `logger.h`，用于降低 v1.0 代码迁移时的 include
  修改量。

调用顺序：

```text
app_init()
  -> board_console_init()
  -> log_init()
```

`App/` 可以调用日志宏，但不能直接初始化 UART，也不能包含 `STC32G_UART.h`。

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
