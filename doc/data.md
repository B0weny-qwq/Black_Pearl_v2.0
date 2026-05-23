# 工程数据

## 基本信息

- 工程名称：Black Pearl v2.0
- 来源基线：Black Pearl v1.0 与 STC32G 官方示例
- 工程目的：移植、解耦、重构
- 架构级别：EmbedForge Level 1.5
- Keil 工程：`RVMDK/STC32G-LIB.uvproj`

## 平台数据

- MCU 系列：STC32G
- vendor 寄存器头：`Platform/Inc/stc32g.h`
- 公共类型定义：`Platform/Inc/type_def.h`
- 平台配置文件：`Platform/Inc/config.h`
- 启动汇编：`Startup/isr.asm`

## 时钟数据

- 基准时钟宏：`MAIN_Fosc = 24000000L`
- 平台运行时钟配置：
  - 使用 `EAXSFR()` 开启扩展 SFR 访问。
  - 使用 `HIRCClkConfig(0)` 配置 HIRC，不分频。
- 主时钟策略：
  - 保留官方 24 MHz 时序基准。
  - 不全局把 `MAIN_Fosc` 切换为 PLL 频率。
  - 高速 PLL 只在具体高速外设或板级设备初始化中单独配置。

## 当前构建边界

Keil 当前 include path：

```text
..\Platform\Inc;..\Drivers\Inc;..\McuAbstraction\Inc;..\BoardDevices\Inc;..\Components\Inc;..\Services\Inc;..\App\Inc
```

Keil 当前工程分组：

- `Startup`
- `Platform`
- `App`
- `BoardDevices`
- `Components`
- `McuAbstraction`
- `Services`
- `Drivers`
- `Drivers_ISR`
- `Drivers_LIB`

`Examples/STC32G_Official/` 下的官方示例不参与当前构建。

## 当前应用生命周期

```text
main()
  -> platform_init()
      -> platform_clock_config()
      -> Timer_config()
      -> Switch_config()
      -> EA = 1
  -> app_init()
      -> board_console_init()
      -> log_init()
  -> loop:
      -> platform_scheduler_run()
      -> app_loop()
```

## 迁移数据记录

- 官方 `APP_*` 示例文件已移动到 `Examples/STC32G_Official/`。
- 旧 `User/` 层已移除。
- `App/` 当前只保留最小生命周期壳。
- `BoardDevices/` 当前已有 UART1 控制台，并预留 QMI8658 板级 IMU API；
  QMI8658 底层传输未接入。
- `Components/` 当前已有 `PID` 和 `Filter` 两个纯算法组件，后续继续承接
  v1.0 算法模块迁移。
- 旧版 LOG 模块已迁移为 `ef_uart`、`board_console`、`logger` 三层。
- `ef_uart`、`ef_iic`、`ef_spi` 已从 `Drivers/` 拆分到 `McuAbstraction/`，
  便于和 STC 官方 SDK 文件分开查找。
- `ef_iic` 已按官方 `APP_DMA_I2C` 示例迁移为 DMA IIC 封装：
  - 默认接口：`ef_iic_init()`、`ef_iic_write_regs()`、`ef_iic_read_regs()`。
  - 支持 P1.4/P1.5、P2.4/P2.5、P7.6/P7.7、P3.3/P3.2 四组 IIC 复用脚。
  - 读写过程带超时返回，不使用官方示例中的无限等待。
- `Filter` 组件已从 `need to do/Function/Filter/` 迁移到 `Components/`：
  - 头文件：`Components/Inc/Filter.h`
  - 源文件：`Components/Src/Filter.c`
  - 保留 `Filter_ResetGyroLowPass()`、`Filter_ResetMagLowPass()`、
    `Filter_GyroLowPass()`、`Filter_MagLowPass()` 四个旧接口名。
  - 依赖从 `config.h` 收敛到 `type_def.h`，不再把平台配置头带进组件层。
- UART1 控制台固定资源：
  - UART：UART1
  - TXD/RXD：P3.1/P3.0
  - 波特率：115200
  - 数据格式：8N1
- 官方 `APP_SPI_PS` 已迁移为 `ef_spi` 和 `board_spi_ps` 两层。
- SPI-PS 固定资源：
  - SS：P2.2
  - MOSI：P2.3
  - MISO：P2.4
  - SCLK：P2.5
  - 默认模式：从机，SS 由引脚决定
  - 位序：MSB first
  - 时钟：CPOL Low，CPHA 2Edge，Fosc/4
- `RVMDK/list/` 是历史构建输出，不应作为源码维护。

## Include 边界检查

生产代码中的 `App/`、`Components/` 和 `BoardDevices/Inc` 不应包含：

- `config.h`
- `stc32g.h`
- `STC32G_*.h`
- 裸端口或寄存器宏，例如 `P0`、`P10`、`EA`、`CLKSEL`

这些内容属于 `Platform/`、`Drivers/` 或 `BoardDevices/Src` 的私有实现。

`Services/` 不直接访问裸硬件。若服务需要输出、存储或通信，应通过
`BoardDevices/` 或明确的抽象接口完成。
