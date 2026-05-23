# 工程数据

## 基本信息

- 工程名称：Black Pearl v2.0
- 来源基线：Black Pearl v1.0 与 STC32G 官方驱动资料
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
..\Platform\Inc;..\Drivers\Inc;..\McuAbstraction\Inc;..\BoardDevices\Inc;..\Components\Inc;..\ChipDrivers\Inc;..\Services\Inc;..\App\Inc
```

Keil 当前工程分组：

- `Startup`
- `Platform`
- `App`
- `BoardDevices`
- `Components`
- `ChipDrivers`
- `McuAbstraction`
- `Services`
- `Drivers`
- `Drivers_ISR`
- `Drivers_LIB`

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
      -> 输出版本号
      -> board_imu_init()
      -> board_mag_init()
      -> board_lt8920_init()
      -> board_gps_init()
  -> loop:
      -> platform_scheduler_run()
      -> app_loop()
          -> board_gps_poll()
          -> board_imu_service()
```

其中 `log_init()` 仅在 `board_console_init()` 成功时执行；设备 bring-up 始终会继续，
只是控制台异常时不会输出串口日志。

## 迁移数据记录

- 旧 `User/` 层已移除。
- `App/` 当前只保留最小生命周期壳。
- `BoardDevices/` 当前已有 UART1 控制台、UART2 GPS 接收链路、SPI-PS 对等通信，
  以及 QMI8658 板级 IMU、QMC6309 板级磁力计和 LT8920 板级无线 bring-up。
- `BoardDevices/Src/board_sensor_bus.c` 已提供板级 DMA IIC 适配，当前固定：
  - 引脚组：P1.4/P1.5
  - 速度：400 kHz
  - 接口：`board_sensor_bus_write_reg()`、`board_sensor_bus_read_regs()`
- `ChipDrivers/` 当前已有：
  - `gnss_nmea`：NMEA0183 解析器
  - `QMI8658`：六轴 IMU 寄存器层驱动
  - `QMC6309`、`LT8920`、`KCT8206`：已整理进芯片驱动层，板级绑定按需接入
- `Components/` 当前已有 `PID` 和 `Filter` 两个纯算法组件，后续继续承接
  v1.0 算法模块迁移。
- 旧版 LOG 模块已迁移为 `ef_uart`、`board_console`、`logger` 三层。
- `ef_uart`、`ef_iic`、`ef_spi` 已从 `Drivers/` 拆分到 `McuAbstraction/`，
  便于和 STC 官方 SDK 文件分开查找。
- `ef_iic` 已按 STC DMA IIC 参考时序迁移为 DMA IIC 封装：
  - 默认接口：`ef_iic_init()`、`ef_iic_write_regs()`、`ef_iic_read_regs()`。
  - 支持 P1.4/P1.5、P2.4/P2.5、P7.6/P7.7、P3.3/P3.2 四组 IIC 复用脚。
  - 读写过程带超时返回，不使用官方示例中的无限等待。
- `Filter` 组件已从本地待迁移参考代码迁移到 `Components/`：
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
- UART2 GPS 固定资源：
  - UART：UART2
  - TXD/RXD：P1.1/P1.0
  - 波特率：115200
  - 上层解析：`ChipDrivers/gnss_nmea`
- QMI8658 当前状态：
  - 芯片寄存器驱动已在 `ChipDrivers/` 完成独立封装
  - 板级 `board_imu` 已绑定到 `board_sensor_bus`
  - `App/` 当前已把 IMU 初始化加入启动生命周期，并串口打印初始化结果
- QMC6309 当前状态：
  - 芯片寄存器驱动已在 `ChipDrivers/` 独立维护
  - 板级 `board_mag` 已复用 `board_sensor_bus` 并返回显式初始化/读数状态
- LT8920 当前状态：
  - 板级 `board_lt8920` 已完成 SPI 路由、RST、KCT8206 前端控制绑定
  - `App/` 当前已把 LT8920 初始化加入启动生命周期，并在校验失败时输出寄存器诊断
- SPI-PS 参考实现已迁移为 `ef_spi` 和 `board_spi_ps` 两层。
- `Examples/` 和 `need to do/` 属于本地参考材料，不纳入版本管理。
- SPI-PS 固定资源：
  - SS：P2.2
  - MOSI：P2.3
  - MISO：P2.4
  - SCLK：P2.5
  - 默认模式：从机，SS 由引脚决定
  - 位序：MSB first
  - 时钟：CPOL Low，CPHA 2Edge，Fosc/4
- LT8920 板级固定资源：
  - SPI 路由：P3.5=CS、P3.4=MISO、P3.3=MOSI、P3.2=SCLK
  - RST：P5.0
  - ANT_SEL：P5.1
  - RXEN：P1.3
  - TXEN：P5.4
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
