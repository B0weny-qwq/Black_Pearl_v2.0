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
      -> board_gps_init()
      -> board_imu_init()
      -> board_mag_init()
      -> board_power_init()
      -> board_wireless_init()
      -> ship_protocol_init()
      -> board_motor_init()
  -> loop:
      -> platform_scheduler_run()
      -> app_loop()
          -> board_gps_poll()
          -> board_wireless_poll()
          -> ship_protocol_run_scheduler()
          -> board_wireless_search_signal_poll()
          -> app_ahrs_poll()
          -> board_motor_service()
```

其中 `log_init()` 仅在 `board_console_init()` 成功时执行；设备 bring-up 始终会继续，
只是控制台异常时不会输出串口日志。

## 迁移数据记录

- 旧 `User/` 层已移除。
- `App/` 当前保留生命周期编排和真实业务状态机，不直接访问 STC 寄存器、ADC/GPIO/EEPROM 驱动或裸电机外设。
- `BoardDevices/` 当前已有 UART1 控制台、UART2 GPS 接收链路、SPI-PS 对等通信，
  以及 QMI8658 板级 IMU、QMC6309 板级磁力计、LT8920 板级无线 bring-up 和
  `board_wireless` 链路管理、双电机 PWM、`board_power` 电池采样和 `board_storage`
  EEPROM/IAP 封装。
- `Services/` 当前已有 `logger` 和 `parameter_store`；AutoDrive 配置保存通过
  `parameter_store -> board_storage` 完成。
- `App/` 当前已有 `ship_protocol` 协议状态机。运行状态变量统一为
  `ship_protocol_runtime_t ship_protocol_rt`，关键字段包括 `lr`、`ud`、`key`、
  `last_key`、`paired`、`work_rx_configured`、`pair_left`、`wait_ticks`、
  `pair_wait_rsp_ticks`、`last_proto_rx_ms`、`last_throttle_rx_ms`、`remote_online`
  和 `throttle_online`。
- `App/` 当前已有 `ship_control` 船体控制状态机。所有手动、电机停机、E 键巡航、
  GPS 对齐和 GPS 导航输出都统一通过 `ShipControl_*` 写入 `board_motor`。
- `App/` 当前已有 `autodrive` GPS 自动驾驶状态机。`0x13/0x14/0x15` 真实分发到
  AutoDrive，返航配置通过服务层持久化，钓点表为会话内 RAM 表。
- `BoardDevices/Src/board_sensor_bus.c` 已提供板级 DMA IIC 适配，当前固定：
  - 引脚组：P1.4/P1.5
  - 速度：100 kHz
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
  - 当前已完成板上移植验证；初始化链路使用 P1.4/P1.5、100 kHz，先初始化 IMU，再初始化同总线上的 QMC6309
  - QMI8658 bring-up 保留可靠上电等待、初始化重试和 `STATUS0` ready 轮询，避免配置后立即读数
- QMC6309 当前状态：
  - 芯片寄存器驱动已在 `ChipDrivers/` 独立维护
  - 板级 `board_mag` 已复用 `board_sensor_bus` 并返回显式初始化/读数状态
- LT8920 当前状态：
  - 板级 `board_lt8920` 已完成 SPI 路由、RST、KCT8206 前端控制绑定
  - `board_wireless` 复用 `board_lt8920` 的实例绑定，所有 SPI、RST、RXEN、TXEN、
    ANT_SEL 细节仍留在 `BoardDevices/Src`
  - `App/` 当前通过 `board_wireless_init()` 完成无线 bring-up，成功后初始化
    `ship_protocol`
- 无线协议 Level 1.5 当前状态：
  - 链路状态：`SHIP_PROTOCOL_STATE_BOOT_WAIT`、`SHIP_PROTOCOL_STATE_PAIR_SEND`、
    `SHIP_PROTOCOL_STATE_PAIR_WAIT_RSP`、`SHIP_PROTOCOL_STATE_WORK_RX`
  - 解析状态：`SHIP_PARSE_WAIT_HEAD`、`SHIP_PARSE_READ_LEN`、`SHIP_PARSE_READ_BODY`、
    `SHIP_PARSE_DISPATCH`
  - 无线模式：`BOARD_WIRELESS_MODE_IDLE`、`BOARD_WIRELESS_MODE_RX`、
    `BOARD_WIRELESS_MODE_TX`
  - pair channel：`0x7F`
  - pair seed：`65 65 A0 65`
  - pair send count：`10`
  - 调度节拍：约 `10ms`
  - 派生工作 RX 信道：`13`
  - 派生工作 TX 信道：`77`，即 `13 + 0x40`，保留旧 `RF_Channel[2]`
  - 派生 key0/key1：`32/30`
  - 派生 sync reg36/reg39：`0x2020/0x1E1E`
  - `PAIR_REQ(0x10)` 帧：`AA 06 10 65 65 A0 65 C3 BB`
  - `PAIR_RSP(0x0F)` 响应窗口：最后一次 `PAIR_REQ` 后开启 `500` 个约 10ms tick，
    约 5 秒；窗口外 `0x0F` 只记录忽略，不置配对成功。
  - 帧格式：`AA | len | cmd | payload | xor | BB`，`len = 2 + payload_len`
  - `GPS_REPORT(0x12)`：`0x12` 是 cmd，不属于 payload；payload 固定 15 字节，
    数据源为 `board_gps_get_state()`
  - `GPS_REPORT(0x12)` payload[0]：卫星数，优先 `satellites_used_gsa`，为 0 时用
    `satellites_used`，最大 24
  - `GPS_REPORT(0x12)` payload[1..2]：航向角，`course_deg_x100 / 100`，大端
  - `GPS_REPORT(0x12)` payload[3]：经度半球兼容固定字节 `'E'`
  - `GPS_REPORT(0x12)` payload[4..5]：旧格式经度 lon1，大端
  - `GPS_REPORT(0x12)` payload[6..7]：旧格式经度 lon2，大端
  - `GPS_REPORT(0x12)` payload[8]：纬度半球兼容固定字节 `'W'`
  - `GPS_REPORT(0x12)` payload[9..10]：旧格式纬度 lat1，大端
  - `GPS_REPORT(0x12)` payload[11..12]：旧格式纬度 lat2，大端
  - `GPS_REPORT(0x12)` payload[13]：电量等级，来自 `board_power`，范围 `0..4`
  - `GPS_REPORT(0x12)` payload[14]：自动驾驶状态，来自 `AutoDrive_InActive()`，
    `0=无自动驾驶`、`1=返航`、`2=去定点/钓点`
  - `0x11`：payload[0]=`lr`，payload[1]=`ud`，payload[2]=`key`；解析后生成
    `SHIP_PROTOCOL_EVENT_THROTTLE`，按键变化时生成 `SHIP_PROTOCOL_EVENT_KEY_EDGE`；
    通过开机/航向 ready 保护后提交给 `ShipControl_UpdateManualInput()`
  - `0x13`：payload[0..9] 为返航点，格式 `ew lon1 lon2 ns lat1 lat2`，其中
    `lon1/lon2/lat1/lat2` 均为大端 u16；解析后生成 `SHIP_PROTOCOL_EVENT_RETURN_HOME`，
    并调用 `AutoDrive_SetReturnPositionRaw()`
  - `0x14`：payload[0..9] 为钓点坐标，格式同 `0x13`；解析后生成
    `SHIP_PROTOCOL_EVENT_FISH_POINT`。旧工程的“首次保存、重复过滤、匹配后去钓点”
    是 `AutoDrive_SetFishPositionRaw()` 根据 RAM 钓点表判断的结果，不是空口 action 字节；
    当前会记录结果码和命中 index。
  - `0x15`：payload[0]=自动返航开关状态；payload[1..10] 可选携带返航点，
    格式同 `0x13`；解析后生成 `SHIP_PROTOCOL_EVENT_RETURN_SWITCH`。旧工程默认/关闭值为
    `0x30`，当前会调用 `AutoDrive_SetSwitchRaw()` 保存配置；开关不为 `0x30` 时由
    AutoDrive 尝试返航。
  - 当前协议事件通过 `ship_protocol_get_event_snapshot()` 或 `ship_protocol_take_event()`
    暴露给日志、联调或后续观察；真实电机所有权仍在 `ShipControl`，自动驾驶规划仍在
    `AutoDrive`，协议层不直接写 `board_motor`

## 旧无线命令对照表

本表来自 `old/WIRELESS/ship_protocol.h`、`old/WIRELESS/ship_protocol.c`、
`old/WIRELESS/README.md` 和 `old/AutoDrive/autodrive.c` 的分发链路。

| Cmd | 旧名 | 方向 | payload | 当前状态机入口 | 当前动作 |
|-----|------|------|---------|----------------|----------|
| `0x0F` | `PAIR_RSP` | 遥控器 -> 船 | 通常 4 字节 | `ship_protocol_handle_pair_rsp()` | 仅 `PAIR_WAIT_RSP` 窗口内确认配对，进入 `WORK_RX`，随后统一回 `0x12` |
| `0x10` | `PAIR_REQ` | 船 -> 遥控器 | 4 字节 seed | `ship_protocol_try_pair_send()` | 在 `0x7F` 配对信道 burst 发送 10 次，帧为 `AA 06 10 65 65 A0 65 C3 BB` |
| `0x11` | `THROTTLE` | 遥控器 -> 船 | `lr, ud, key` | `ship_protocol_handle_throttle()` -> `ShipControl_UpdateManualInput()` | 开机/航向保护内只保活；AutoDrive busy 时不抢电机；正常时更新手动控制；E 键可进入/退出巡航 |
| `0x12` | `GPS_REPORT` | 船 -> 遥控器 | 固定 15 字节 | `ship_protocol_send_gps_once()` | 任意合法 `0x0F/0x11/0x13/0x14/0x15` 分发后回包；方向字节继续固定 `E/W` |
| `0x13` | `RETURN_HOME` | 遥控器 -> 船 | 10 字节旧点位 | `AutoDrive_SetReturnPositionRaw()` | 保存返航点到配置并尝试进入返航；随后统一回 `0x12` |
| `0x14` | `GOTO_POINT` | 遥控器 -> 船 | 10 字节旧点位 | `AutoDrive_SetFishPositionRaw()` | RAM 5 点表查重；未知点存储/拒绝，命中已知点才距离检查并尝试启动去点；日志输出结果码和 index |
| `0x15` | `RETURN_SWITCH` | 遥控器 -> 船 | `switch` + 可选 10 字节返航点 | `AutoDrive_SetSwitchRaw()` | 保存返航开关和可选返航点；开关不为 `0x30` 时尝试返航；随后统一回 `0x12` |
| `0x16` | `AUTODRIVE_DIAG` | 船 -> 遥控器/调试 | 固定 36 字节 | `ship_protocol_send_autodrive_diag_once()` | 船端主动诊断上报 AutoDrive 状态、模式、原因、距离、当前点和目标点；不替代 `0x12` |

10 字节点位格式固定为：

| Byte | 字段 | 说明 |
|------|------|------|
| `0` | `lon_ew` | 经度方向字节 |
| `1..2` | `lon_whole` | 大端 `u16` |
| `3..4` | `lon_frac` | 大端 `u16` |
| `5` | `lat_ns` | 纬度方向字节 |
| `6..7` | `lat_whole` | 大端 `u16` |
| `8..9` | `lat_frac` | 大端 `u16` |

## 无线运行日志验收

无线、控制和 AutoDrive 联调时，串口日志至少应覆盖以下关键路径：

| 阶段 | 预期日志关键词 | 说明 |
|------|----------------|------|
| RF bring-up | `WL init ok` | `board_wireless_init()` 完成 LT8920/KCT8206 板级初始化 |
| 配对发送 | `pair req sent` | `PAIR_REQ(0x10)` 按固定 seed 在 `0x7F` 信道发送，目标次数为 10 |
| 配对参数 | `pair req start` | 日志应包含 seed、`work_rx=13`、`work_tx=77`、`key=32/30`、`reg36=0x2020`、`reg39=0x1E1E` |
| 配对帧 | `pair req frame=AA 06 10 65 65 A0 65 C3 BB` | XOR 范围保持旧协议 `len/cmd/payload` |
| 响应窗口 | `pair rsp window open` | 最后一次 `PAIR_REQ` 后打开 500 tick 响应窗口 |
| 配对成功 | `pair ok` | 收到合法 `PAIR_RSP(0x0F)` 或任意合法旧帧后刷新 `paired` |
| 工作信道 | `enter work-state` | 已切入旧算法派生工作信道 RX |
| 手动输入 | `rc cmd=0x11` | 解析 `lr/ud/key`，通过保护条件后提交给 `ShipControl` |
| 开机保护 | `manual boot block` / `manual boot ready` | 上电短暂阻塞手动电机输出，可等待航向 ready |
| 巡航状态 | `cruise enter` / `cruise exit` / `cruise reject` | E 键巡航准入、退出和拒绝原因 |
| 电机输出 | `CTRL out mode=` | `ShipControl` 统一输出 mode、motion、base、diff、left、right |
| 返航点事件 | `0x13 return-home event` | 解析 10 字节返航点并调用 AutoDrive 返航入口 |
| 钓点事件 | `0x14 fish-point event` | 解析 10 字节钓点坐标，并进入 5 点表保存/查重/启动状态机 |
| 钓点诊断 | `0x14 rx fl=` | 打印 frame/payload/xor/result/index，便于联调 0x14 结果码 |
| 返航开关事件 | `0x15 return-switch event` | 解析 `switch_state` 和可选返航点，保存配置并按开关触发返航 |
| GPS 回包 | `tx cmd=0x12` | 任意合法协议帧分发后发送固定 15 字节 GPS payload |
| AutoDrive 诊断 | `tx cmd=0x16` | 主动上报 state、mode、switch、reason、GPS、卫星数和距离 |
| 配置保存 | `ADCFG load` / `ADCFG save` | AutoDrive 配置经 `parameter_store -> board_storage` 读写 |
| 电源采样 | `POWER init ok` / `adc raw=` / `low power latched` | 电源底层初始化、电量采样和低电返航触发 |
| 链路异常 | `timeout` / `bad xor` / `queue full` | 用于确认超时、CRC/XOR 拒包和 RF payload 队列溢出 |

这些日志属于 `Services/logger` 诊断输出。`App/` 可以调用日志宏，但无线硬件访问仍必须通过
`BoardDevices/`，不得在协议层引入 STC 寄存器头、裸 GPIO 或官方驱动 API。
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
- `RVMDK/STC32G-LIB.uvproj` 已加入：
  - `App/Src/ship_protocol.c`
  - `App/Inc/ship_protocol.h`
  - `App/Src/ship_control.c`
  - `App/Inc/ship_control.h`
  - `App/Src/autodrive.c`
  - `App/Inc/autodrive.h`
  - `App/Src/autodrive_config.c`
  - `App/Inc/autodrive_config.h`
  - `BoardDevices/Src/board_wireless.c`
  - `BoardDevices/Inc/board_wireless.h`
  - `BoardDevices/Src/board_power.c`
  - `BoardDevices/Inc/board_power.h`
  - `BoardDevices/Src/board_storage.c`
  - `BoardDevices/Inc/board_storage.h`
  - `Services/Src/parameter_store.c`
  - `Services/Inc/parameter_store.h`
- 当前验证记录：
  - `App/`、`Components/` 禁用 vendor/platform 头文件 include 扫描通过。
  - `RVMDK/STC32G-LIB.uvproj` 已通过 XML 解析检查。
  - Keil 命令行构建通过：`0 Error(s), 0 Warning(s)`。
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

## 当前实现状态清单

### 已实现底层

- 无线底层：`board_wireless` 已支持 LT8920/KCT8206 初始化、信道切换、sync regs、
  收发队列、TX/RX 和天线扫描。
- 电机底层：`board_motor` 已支持左右电机 PWM 初始化、目标速度、停机、周期刷新和
  PWM 快照。
- 传感器底层：`board_gps`、`board_imu`、`board_mag` 已接入；AHRS、MagCompass 和
  HeadingEstimator 已提供航向 ready/角速度/航向角数据。
- 电源底层：`board_power` 已封装 `P0.0 / ADC_CH8`、ADC 初始化、采样、毫伏值和
  `0..4` 电量等级。
- 存储底层：`board_storage` 已封装 STC EEPROM/IAP 读写和扇区擦除。
- 服务层：`logger` 和 `parameter_store` 已实现；AutoDrive 配置通过
  `parameter_store -> board_storage` 保存。

### 未实现底层

- A/B/C/D 键对应的灯控、蜂鸣器或其它板级外设未确认；当前只保留按键事件名和日志，
  不驱动硬件。
- 电池分压电阻真实比例未确认；`board_power` 的 `bat_mv` 使用 `1:1` 占位比例，
  但 `0x12 payload[13]` 的旧版电量等级已可用于遥控器兼容。

### 能实现但当前未实现

- A/B/C/D 对应外设确认后，可以按 `BoardDevices/board_light`、`board_buzzer` 等板级
  API 接入，并由协议按键状态机调用；不能在 `ship_protocol.c` 里直接写 GPIO。
- `board_power` 的真实电池毫伏值校准可在确认分压电阻后补齐，只需修改 BoardDevices
  内部换算，不影响 `0x12` 协议字段布局。

## v1.1 AHRS/MAG migration data

- Source reference: `C:\Users\S\Desktop\STC_PROJECT\Black_Pearl_v1.1\Code_boweny\Function\AHRS`.
- New components:
  - `Components/Inc/AHRS.h`, `Components/Src/AHRS.c`
  - `Components/Inc/HeadingEstimator.h`, `Components/Src/HeadingEstimator.c`
  - `Components/Inc/MagCompass.h`, `Components/Src/MagCompass.c`
- Runtime periods: `AHRS_IMU_PERIOD_MS=17`, `AHRS_MAG_PERIOD_MS=100`, `AHRS_DT_MAX_MS=50`.
- Magnetic compass parameters kept from v1.1: ready count 5, IIR divisor 8, jump gate 3000 cd, norm track 25%, norm reject 80%, horizontal minimum 40, install offset 21930 cd, direction sign -1.
- App polling path: `board_imu_service()` -> `board_imu_read()` -> `AHRS_UpdateRaw6Axis()`; `board_mag_read()` -> `Filter_MagLowPass()` -> `AHRS_UpdateRawMag()` and `MagCompass_Update()`; stable heading then feeds `Heading_Update()`.
- Exported app snapshots: `app_get_attitude_state()`, `app_get_heading_ready()`, `app_get_heading_deg100()`, `app_get_heading_relative_deg100()`.
- Timing source: `platform_scheduler_get_tick_ms()` reads the 1 ms Timer0 scheduler tick with interrupt-safe snapshot.
- Current limitation: v2 static heading gate uses AHRS gyro-bias/acc/gyro-still state only. v1.1 motor-stop and ship-control-mode gates will be restored when the control layer is ported.

## 2026-05 GNSS and 0x12 full-field alignment record

Current `0x12` status/GPS payload alignment in `App/Src/ship_protocol.c`:

- `payload[0]`: `satellites_used_gsa` first, fallback `satellites_used`, clamp `<= 24`
- `payload[1..2]`: `(course_deg_x100 / 100) % 360`, big-endian
- `payload[3]`: legacy fixed marker byte `'E'`
- `payload[4..5]`: `legacy_lon1`, fallback converted from `lon_deg1e7`
- `payload[6..7]`: `legacy_lon2`, fallback converted from `lon_deg1e7`
- `payload[8]`: legacy fixed marker byte `'W'`
- `payload[9..10]`: `legacy_lat1`, fallback converted from `lat_deg1e7`
- `payload[11..12]`: `legacy_lat2`, fallback converted from `lat_deg1e7`
- `payload[13]`: old-version discrete battery level `0..4`, sourced from `board_power`
- `payload[14]`: `AutoDrive_InActive()` status, `0=idle`, `1=return-home`, `2=goto-point`

Current GNSS parser-to-protocol field bridge is complete for the old upper-layer dependent GPS fields:

- `legacy_coord_valid`
- `legacy_lon1`, `legacy_lon2`, `legacy_lat1`, `legacy_lat2`
- `lat_deg1e7`, `lon_deg1e7`
- `course_deg_x100`
- `satellites_used`
- `satellites_used_gsa`
- `update_sequence`
- `fix_valid`

## 2026-05 power/storage/control alignment record

Current board-side ADC facts:

- Voltage detect pin: `P0.0`
- ADC channel used by the current board: `ADC_CH8`
- Old-board historical channel: `ADC_CH9`
- Current hardware owner: `BoardDevices/Src/board_power.c`
- App read point: `board_power_read()` in `App/Src/ship_protocol.c`

Current runtime sampling chain:

- Scheduler cadence: `10 ms` per `ship_protocol_run_scheduler()`
- ADC update divider: `SHIP_POWER_SAMPLE_DIVIDER = 100`
- Effective ADC update period: about `1000 ms`
- Runtime-tracked period field: `ship_protocol_rt.power_sample_period_ms`

Current cached power sample fields in `ship_protocol_rt`:

- `power_level`
- `power_adc_ready`
- `power_sample_divider_count`
- `lowpower_check_ticks`
- `lowpower_return_latched`
- `power_sample.raw`
- `power_sample.adc_mv`
- `power_sample.bat_mv`
- `power_sample.report`
- `power_sample.valid`

Power-level mapping remains aligned to the old discrete thresholds:

- non-`BOARD_12V`: `1710 / 1630 / 1530 / 1420`
- `BOARD_12V`: `2000 / 1900 / 1730 / 1620`

Important current limitation:

- `bat_mv` scaling still uses the placeholder divider ratio `SHIP_BAT_DIV_NUM = 1`, `SHIP_BAT_DIV_DEN = 1` until the real current-board resistor divider values are reintroduced or confirmed from hardware data.
- Because of that, old-style remote compatibility is already restored for `payload[13]` power level, but the internal `bat_mv` engineering value should not yet be treated as calibrated.

AutoDrive persistent configuration now follows this chain:

```text
AutoDrive_SetReturnPositionRaw()/AutoDrive_SetSwitchRaw()
  -> AutoDriveCfg_Save()
  -> parameter_store_save_autodrive()
  -> board_storage_write()
  -> STC32G_EEPROM driver
```

`App/` and `Services/` do not include STC EEPROM headers directly. `parameter_store` only stores
validated bytes; AutoDrive owns the business structure and default values.
