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
      -> board_spi_ps_init()
      -> board_motor_init()
  -> loop:
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
  - 初始化成功前会实际读取一帧首样本。若寄存器配置看起来正确但 accel/gyro 数据路径不出样，`board_imu_init()` 会返回 `BOARD_IMU_ERR_DATA` 并进入错误状态，避免启动日志误报 `init ok`
  - 热路径读取与 v1.1 对齐：六轴数据从 `AX_L` 连续读 12 字节，温度从 `TEMP_L` 单独尝试读取，仅作为可选字段
- QMC6309 当前状态：
  - 芯片寄存器驱动已在 `ChipDrivers/` 独立维护
  - 板级 `board_mag` 已复用 `board_sensor_bus` 并返回显式初始化/读数状态
- LT8920 当前状态：
  - 板级 `board_lt8920` 已完成 SPI 路由、RST、KCT8206 前端控制绑定
  - SPI 固定资源为 `P3.5=CS`、`P3.4=MOSI`、`P3.3=MISO`、`P3.2=SCLK`
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
  - `PAIR_REQ(0x10)` 帧：`AA 06 10 65 65 A0 65 D3 BB`
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
    `SHIP_PROTOCOL_EVENT_THROTTLE`；A/E/unknown 按键变化仍生成 `SHIP_PROTOCOL_EVENT_KEY_EDGE`；
    B/C/D 按键变化生成 `SHIP_PROTOCOL_EVENT_KEY_ACTION`，并通过 `key_action` 区分
    `B_NOOP` / `C_NOOP` / `D_NOOP`；通过开机/航向 ready 保护后提交给
    `ShipControl_UpdateManualInput()`
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
  - 当前协议事件通过 8 深度环形队列暴露给日志、联调或后续观察。
    `ship_protocol_take_event()` 按 FIFO 消费，`ship_protocol_get_event_snapshot()` 只查看最近事件；
    `App/Src/app.c` 的 `app_ship_event_poll()` 已在主循环中 drain 队列。
    真实电机所有权仍在 `ShipControl`，自动驾驶规划仍在 `AutoDrive`，协议层不直接写 `board_motor`

## 旧无线命令对照表

本表来自 `old/WIRELESS/ship_protocol.h`、`old/WIRELESS/ship_protocol.c`、
`old/WIRELESS/README.md` 和 `old/AutoDrive/autodrive.c` 的分发链路。

| Cmd | 旧名 | 方向 | payload | 当前状态机入口 | 当前动作 |
|-----|------|------|---------|----------------|----------|
| `0x0F` | `PAIR_RSP` | 遥控器 -> 船 | 通常 4 字节 | `ship_protocol_handle_pair_rsp()` | 仅 `PAIR_WAIT_RSP` 窗口内确认配对，进入 `WORK_RX`，随后统一回 `0x12` |
| `0x10` | `PAIR_REQ` | 船 -> 遥控器 | 4 字节 seed | `ship_protocol_try_pair_send()` | 在 `0x7F` 配对信道 burst 发送 10 次，帧为 `AA 06 10 65 65 A0 65 D3 BB` |
| `0x11` | `THROTTLE` | 遥控器 -> 船 | `lr, ud, key` | `ship_protocol_handle_throttle()` -> `ShipControl_UpdateManualInput()` | 开机/航向保护内只保活；AutoDrive busy 时不抢电机；正常时更新手动控制；A/E/unknown 保持 `KEY_EDGE`；B/C/D 发布 `KEY_ACTION_*_NOOP`；E 键可进入/退出巡航 |
| `0x12` | `GPS_REPORT` | 船 -> 遥控器 | 固定 15 字节 | `ship_protocol_send_gps_once()` | 任意合法 `0x0F/0x11/0x13/0x14/0x15` 分发后回包；方向字节继续固定 `E/W` |
| `0x13` | `RETURN_HOME` | 遥控器 -> 船 | 10 字节旧点位 | `AutoDrive_SetReturnPositionRaw()` | 保存返航点到配置并尝试进入返航；随后统一回 `0x12` |
| `0x14` | `GOTO_POINT` | 遥控器 -> 船 | 10 字节旧点位 | `AutoDrive_SetFishPositionRaw()` | RAM 5 点表查重；未知点在表未满时先存储并立即尝试去点，快速重复帧抑制；日志拆分 save/nav 结果和 index |
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
| 配对帧 | `pair req frame=AA 06 10 65 65 A0 65 D3 BB` | XOR 范围保持旧协议 `len/cmd/payload` |
| 响应窗口 | `pair rsp win` | 最后一次 `PAIR_REQ` 后打开 500 tick 响应窗口 |
| 配对成功 | `pair ok` | 收到合法 `PAIR_RSP(0x0F)` 或任意合法旧帧后刷新 `paired` |
| 工作信道 | `enter work-state` | 已切入旧算法派生工作信道 RX |
| 手动输入 | `rc cmd=0x11` | 解析 `lr/ud/key`，通过保护条件后提交给 `ShipControl` |
| 按键动作 | `key edge key=` | A/E/unknown 仍是 `KEY_EDGE`；B/C/D 日志为 `B-noop`/`C-noop`/`D-noop` 并发布 `KEY_ACTION_*_NOOP` |
| 开机保护 | `boot blk` / `boot ready` | 上电短暂阻塞手动电机输出，可等待航向 ready |
| 巡航状态 | `cruise enter` / `cruise exit` / `cruise reject` | E 键巡航准入、退出和拒绝原因 |
| 电机输出 | `CTRL out mode=` | `ShipControl` 统一输出 mode、motion、base、diff、left、right |
| 返航点事件 | `0x13 ret` | 解析 10 字节返航点并调用 AutoDrive 返航入口 |
| 钓点事件 | `0x14 fish` | 解析 10 字节钓点坐标，并进入 5 点表保存/查重/启动状态机 |
| 钓点诊断 | `0x14 rx fl=` | 打印 frame/payload/xor/save/nav/index，便于联调 0x14 存点和导航结果 |
| 返航开关事件 | `0x15 sw=` | 解析 `switch_state` 和可选返航点，保存配置并按开关触发返航 |
| GPS 回包 | `tx cmd=12` | 任意合法协议帧分发后发送固定 15 字节 GPS payload |
| AutoDrive 诊断 | `tx16 st=` | 主动上报 state、mode、switch、reason、GPS、卫星数和距离 |
| 配置保存 | `ADCFG ld` / `ADCFG sv` | AutoDrive 配置经 `parameter_store -> board_storage` 读写 |
| 电源采样 | `POWER init ok` / `adc raw=` / `low power latched` | 电源底层初始化、电量采样、`POWER_SAMPLE` / `POWER_LEVEL_CHANGED` 观察事件和低电返航触发；`bat_mv` 未标定时启动日志会提示 `bat_mv uncal` |
| 协议事件消费 | `[EVT] ...` | `app_ship_event_poll()` 从 `ship_protocol_take_event()` FIFO drain 事件；高频 throttle/power sample 默认不额外刷日志 |
| SPI-PS RX | `SPI-PS init ok` / `disabled shared SPI` | SPI-PS 初始化成功后，`app_loop()` 轮询 RX 完整帧并发布 `SPI_PS_FRAME_RX`；当前生成配置默认禁用，且共用 SPI 护栏会阻止误启用 |
| 链路异常 | `timeout` / `bad xor` / `queue full` | 用于确认超时、CRC/XOR 拒包和 RF payload 队列溢出 |

这些日志属于 `Services/logger` 诊断输出。`App/` 可以调用日志宏，但无线硬件访问仍必须通过
`BoardDevices/`，不得在协议层引入 STC 寄存器头、裸 GPIO 或官方驱动 API。

## 烧录后行为与上位机兼容

当前固件烧录后会直接进入船端业务主循环，不是单纯驱动 demo：

- UART1 `P3.1/P3.0` 以 `115200 8N1` 输出日志。
- UART2 `P1.1/P1.0` 接收 GPS NMEA，解析卫星数、航向和旧坐标拆分字段。
- QMI8658/QMC6309 进入 AHRS、磁罗盘和 HeadingEstimator 数据链路。
- LT8920/KCT8206 先在 `0x7F` 配对信道发送 `PAIR_REQ(0x10)`，再进入旧算法派生工作信道。
- 收到 `0x11` 后进入 `ShipControl` 手动/巡航控制；收到 `0x13/0x14/0x15` 后进入 AutoDrive 返航、钓点或返航开关逻辑。
- 手动控制和 yaw 自稳参数来自 `App/Inc/app_config.h`，当前对齐 v1.1 运行档位：
  轴量程 `60`、yaw 差速限幅 `320 permille`、降额阈值 `1000/2000 cd`、
  gyro 阻尼 `4096`、PID `KP=384/KI=0/KD=96`。手动 yaw 自稳进入条件也按
  v1.1 恢复为“左右电机差速低于当前输入的 `20%`，并连续稳定 2 帧”。
- 任意合法旧协议帧分发后都会在旧工作 RX 信道回发旧格式 `GPS_REPORT(0x12)`；船端还会在同一工作信道主动发送 `AUTODRIVE_DIAG(0x16)` 供调试观察。
- 电源等级通过当前板子的 `P0.0 / ADC_CH8` 转成旧协议 `0..4` 等级。`bat_mv` 仍未按真实分压电阻标定，因此仅作为工程诊断值。
  电源采样仍约 `1000 ms` 更新一次，常规 ADC 日志按 `SHIP_POWER_LOG_PERIOD_MS=10000 ms` 节流。

上位机/遥控兼容面：

- 保留旧 `AA | len | cmd | payload | xor | BB` 帧格式和 `len/cmd/payload` XOR。
- 兼容旧命令 `0x10/0x0F/0x11/0x12/0x13/0x14/0x15`。
- `0x12` payload 固定 15 字节，字段顺序保持旧上位机/遥控期望；`payload[13]` 是电量等级，`payload[14]` 是 AutoDrive 状态。
- `0x16` 是额外诊断帧，用于新工具或日志查看，不改变旧 `0x12` 的兼容行为。
- `tools/ship_log_viewer` 是唯一保留的本地兼容日志查看工具；固件空口协议仍按旧帧工作。
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
- SPI-PS 当前生成配置：
  - `EF_BOARD_SPI_PS_ENABLED = 0U`
  - `EF_BOARD_SPI_PS_SHARES_LT8920_SPI = 1U`
  - 如果未确认独立 SPI 或仲裁策略就启用 SPI-PS，`board_spi_ps_init()` 会返回
    `BOARD_SPI_PS_ERR_RESOURCE`，避免在无线初始化后把 STC SPI 改成 SPI-PS 从机模式。
- LT8920 板级固定资源：
  - SPI 路由：P3.5=CS、P3.4=MOSI、P3.3=MISO、P3.2=SCLK
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
  `0..4` 电量等级；`bat_mv` 当前仍按 `BOARD_POWER_BAT_SCALE_NUM/DEN = 1/1`
  输出占位值。
- 存储底层：`board_storage` 已封装 STC EEPROM/IAP 读写和扇区擦除。
- 服务层：`logger` 和 `parameter_store` 已实现；AutoDrive 配置通过
  `parameter_store -> board_storage` 保存。

### 未实现底层

- A/B/C/D 键对应的灯控、蜂鸣器或其它板级外设未确认；当前不驱动硬件。A 保持现有
  A-light `KEY_EDGE` 日志语义；B/C/D 已保留明确业务事件
  `SHIP_PROTOCOL_EVENT_KEY_ACTION`，分别为 `B_NOOP`、`C_NOOP`、`D_NOOP`。
- 电池分压电阻真实比例未确认；`board_power` 的 `bat_mv` 使用 `1:1` 占位比例，
  但 `0x12 payload[13]` 的旧版电量等级已可用于遥控器兼容。

### 能实现但当前未实现

- A 对应灯控脚确认后，可以按 `BoardDevices/board_light` 板级 API 接入；本轮未新增 A
  `KEY_ACTION`，也未接硬件。若后续 B/C/D 需要真实外设，也必须通过 `BoardDevices/`
  API 接入；不能在 `ship_protocol.c` 里直接写 GPIO。
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
- Static heading gate matches v1.1: the fused heading only accepts the magnetometer heading when ShipControl is stopped, both motor speeds are zero, gyro bias is ready, acceleration is valid, and gyro readings are still.

### C251 memory layout note

The C251 target uses a 4 KB EDATA/HDATA window plus external XDATA. Large state is therefore
kept out of EDATA when it is not latency-critical:

- `Components/Src/AHRS.c`: `ahrs_ctx` is `AHRS_Context_t EF_LARGE_DATA` and is placed in XDATA.
- `BoardDevices/Src/board_wireless.c`: the 4 x 60-byte RF payload queue is `EF_LARGE_DATA` and is placed in XDATA.
- `ChipDrivers/Src/LT8920.c`: the default LT8920 register profile uses `EF_CODE_CONST` and is placed in CODE instead of HCONST.
- The wireless queue remains private to BoardDevices. `App/` still receives copied payloads through
  `board_wireless_receive()`, so no XDATA pointer or LT8920 detail crosses the layer boundary.

Current verified map evidence from `RVMDK/list/STC32G-LIB.map`:

- EDATA used: `000F21H` bytes.
- C251 stack: `?STACK`, `000100H` bytes, placed at `000E2DH..000F2CH`.
- HCONST used: `000E0DH` bytes.
- XDATA used: `00017CH` bytes.
- XDATA segments: `?XD?BOARD_WIRELESS` and `?XD?AHRS`.
- CODE constant segment: `?CO?LT8920` for the LT8920 default register profile.

Current verified build evidence from `RVMDK/list/STC32G-LIB.build_log.htm`:

```text
Program Size: data=11.7 edata+hdata=3873 xdata=380 const=3597 code=61343
".\list\STC32G-LIB" - 0 Error(s), 0 Warning(s).
```

Public API documentation checkpoint:

- `App/Inc`: lifecycle, AutoDrive, ShipControl and protocol event APIs document ownership,
  timing units and observable side effects.
- `BoardDevices/Inc`: GPS, LT8920 and wireless APIs document hidden UART/SPI/GPIO resources,
  blocking behavior and return codes.
- `ChipDrivers/Inc/gnss_nmea.h` documents the parser-only boundary and keeps UART ownership out of
  the parser layer.
- `McuAbstraction/Inc/ef_iic.h` uses Doxygen comments for IIC init, transfer, recovery and
  diagnostic APIs.

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
- Each valid sample publishes a protocol observation event. If the discrete level changed, the event is
  `SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED`; otherwise it is `SHIP_PROTOCOL_EVENT_POWER_SAMPLE`.
- Low-power return latches only after `power_level == 0`, more than `600` scheduler ticks,
  `AutoDrive_GetMode() == AUTO_DRIVE_CLOSE`, and `ShipControl_GetManualAccelerator() < 10`.
  The latch publishes `SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED` once and then triggers
  `AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_LOW_POWER)`.

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
- `power_sample.sampled`
- `power_sample.status`

`sampled == 0` 表示启动后还没有真正执行过下采样 ADC 读取，此时日志为 `adc pending`；
`sampled != 0 && valid == 0` 表示已经尝试读取但底层返回 not-ready/sample 错误，此时日志为
`adc not-ready rc=...`。低电返航计数只在 `valid != 0` 后累计，避免上电初始缓存误触发。

Current protocol event queue:

- Queue depth: `SHIP_PROTOCOL_EVENT_QUEUE_DEPTH = 8`
- Producer: `ship_protocol_publish_event()` copies the current `ship_protocol_event_snapshot_t`
  into the ring queue after assigning type/state/cmd/len/sequence/tick.
- Consumer: `ship_protocol_take_event()` returns queued events FIFO and clears the latest-event
  pending flag only when the queue becomes empty.
- Latest snapshot: `ship_protocol_get_event_snapshot()` remains a non-consuming view of the most
  recent event.
- Overflow behavior: when the queue is full, the oldest queued event is dropped and the newest
  event is inserted. This keeps current control observations moving without blocking the scheduler.
- App drain: `app_ship_event_poll()` is called once per `app_loop()` after `app_spi_ps_poll()`.

Current protocol event snapshot extensions:

- `key_action`: valid for `SHIP_PROTOCOL_EVENT_KEY_ACTION`; values are `SHIP_PROTOCOL_KEY_ACTION_B_NOOP`,
  `SHIP_PROTOCOL_KEY_ACTION_C_NOOP`, and `SHIP_PROTOCOL_KEY_ACTION_D_NOOP`. A does not use this path.
- `power`: filled for `SHIP_PROTOCOL_EVENT_POWER_SAMPLE`,
  `SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED`, and `SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED`; fields are
  `raw`, `adc_mv`, `bat_mv`, `level`, and `valid`.
- `spi_ps`: filled for `SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX`; fields are `status`, `len`,
  `stored_len`, and the first `16` bytes in `bytes`.

Current SPI-PS App bridge:

- `app_init()` calls `board_spi_ps_init()` after wireless bring-up; if initialization fails,
  `app_spi_ps_poll()` exits silently.
- `app_loop()` calls `app_spi_ps_poll()`, which runs `board_spi_ps_service()`, then
  `board_spi_ps_read()` for completed RX frames.
- Successful reads and overflow/truncated reads publish `SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX`; the
  `spi_ps.status` field preserves `BOARD_SPI_PS_OK` or `BOARD_SPI_PS_ERR_OVERFLOW`.

Power-level mapping remains aligned to the old discrete thresholds:

- non-`BOARD_12V`: `1710 / 1630 / 1530 / 1420`
- `BOARD_12V`: `2000 / 1900 / 1730 / 1620`

Important current limitation:

- `bat_mv` scaling still uses the placeholder board-power ratio `BOARD_POWER_BAT_SCALE_NUM = 1`,
  `BOARD_POWER_BAT_SCALE_DEN = 1` until the real current-board resistor divider values are
  reintroduced or confirmed from hardware data.
- `BOARD_POWER_BAT_MV_UNCALIBRATED = 1U` intentionally keeps the startup warning visible.
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
