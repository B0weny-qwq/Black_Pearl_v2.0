# WIRELESS 模块说明

本文档描述根目录当前工程 `Code_boweny/Device/WIRELESS/` 的真实实现状态。无线遥控器程序不可修改，因此本模块的首要目标不是设计新协议，而是兼容旧工程 `ship_Gps_V2.1_20260406-115200/App/Wireless` 的空口格式、命令号、配对节奏和回包字段。

说明原则：

- 本文只记录当前工作区源码已经实现的行为，不把未实现的旧版功能写成“已完成”。
- 空口协议必须优先保证旧遥控器能接收、能解析、能继续下发命令。
- 当前工程和旧工程不一致的地方统一写在“当前已知差异”，后续改代码时先看那里。

## 1. 模块定位

`WIRELESS` 目录同时包含四层逻辑：

| 层级 | 文件 | 职责 |
|------|------|------|
| 板级适配 | `wireless_port.h/.c` | GPIO、SPI、RST、RXEN、TXEN、ANT_SEL、延时 |
| 芯片驱动 | `lt8920.h/.c` | LT8920 寄存器、FIFO、TX/RX 模式、RSSI、CRC 状态 |
| 无线管理 | `wireless.h/.c` | 初始化、收发队列、天线扫描、信道和同步字切换 |
| 旧协议业务 | `ship_protocol.h/.c` | 旧遥控器配对、协议截帧、命令分发、`0x12` 状态回传 |

核心边界：

- `Wireless_Receive()` 返回的是一次 LT8920 射频载荷，不保证刚好等于完整 `AA...BB` 协议帧。
- 旧协议的逐字节找帧、长度判断、XOR 校验和命令分发只在 `ship_protocol.c` 内完成。
- 后续不要在 `wireless.c` 里解析 `0x11/0x12/0x13` 这类业务命令。
- 后续不要向旧遥控器协议新增字段、改变命令号或改变已有载荷顺序。

## 2. 当前功能开关

当前运行档以 `User/FeatureSwitch.h` 为准：

```c
#define ENABLE_WIRELESS_MODULE         1
#define ENABLE_GPS_MODULE              1
#define ENABLE_MAG_MODULE              1
#define ENABLE_IMU_MODULE              1
#define ENABLE_MAG_STANDALONE_POLL     0
#define ENABLE_IMU_AHRS_POLL           1
#define ENABLE_IMU_BASIC_POLL          0

#define ENABLE_SHIP_PROTOCOL_SCHED     1
#define SHIP_PROTOCOL_POLL_ENABLE      1
#define SHIP_PROTOCOL_COMPAT_ENABLE    0

#define SHIP_THROTTLE_PWM_ENABLE       1
#define SHIP_YAW_HOLD_ENABLE           1
#define SHIP_YAW_HOLD_MANUAL_ENABLE    1

#define WIRELESS_FORCE_ANT_MODE        WIRELESS_FORCE_ANT_1
#define WIRELESS_FRONTEND_BYPASS_TEST  0
#define WIRELESS_RX_TRACE_ENABLE       0
#define WIRELESS_TX_TRACE_ENABLE       0
#define SHIP_PROTOCOL_DIAG_ENABLE      0
#define SHIP_PROTOCOL_ERROR_LOG_ENABLE 0
```

含义：

- 无线、GPS、磁力计、IMU/AHRS 当前都启用。
- 磁力计单独刷屏入口关闭，磁力计由 AHRS 路径使用。
- 主路径使用 `ShipProtocol_RunScheduler()`，不使用兼容轮询 `ShipProtocol_Poll()`。
- `0x11` 的空口载荷仍是旧版 `lr/ud/key`，但当前应用层打开了手动航向保持；只有在 `SHIP_YAW_HOLD_MANUAL_ENABLE=1`、前进油门成立且左右输出差小于 20% 时，才会叠加 yaw-hold，因此控制手感不是旧版纯开环。
- 当前强制使用天线 1；即使初始化会扫描 RSSI，最终也被 `WIRELESS_FORCE_ANT_1` 固定。

## 3. 硬件连接

当前 Black Pearl v1.1 无线硬件映射：

| STC32G 引脚 | 方向 | 无线侧信号 | 说明 |
|-------------|------|------------|------|
| `P3.2` | 输出 | `SPI_CLK` | SPI4 SCLK |
| `P3.3` | 输入 | `MISO` | SPI4 MISO |
| `P3.4` | 输出 | `MOSI` | SPI4 MOSI |
| `P3.5` | 输出 | `SPI_SS/CS` | GPIO 手动片选 |
| `P5.0` | 输出 | `RST` | LT8920 复位 |
| `P5.1` | 输出 | `ANT_SEL` | KCT8206L 天线选择 |
| `P1.3` | 输出 | `RXEN` | KCT8206L 接收前端使能 |
| `P5.4` | 输出 | `TXEN` | KCT8206L 发射前端使能 |
| `P0.0 / ADC_CH8` | 输入 | 电池电压检测 | `0x12` 电量等级来源 |

前端时序：

- RX 模式：`TXEN=0`，`RXEN=1`，再打开 LT8920 RX。
- TX 模式：兼容旧版 `LT8920_TxData()`，保持 `RXEN=1`，先 `TXEN=0`，短延时后拉高 `TXEN=1` 发包。
- IDLE 模式：`TXEN=0`，`RXEN=0`，LT8920 进入 idle。

SPI 配置：

- 默认硬件 SPI：`WIRELESS_SPI_USE_SOFT=0`。
- SPI 路由：`SPI_SW(SPI_P35_P34_P33_P32)`。
- SPI 速度：`WIRELESS_HW_SPI_SPEED_CFG=SPI_Speed_16`。
- SPI 模式：主机、MSB first、`CPOL=Low`、`CPHA=2Edge`。

## 4. 启动与主循环

系统启动中与无线相关的顺序：

```text
SYS_Init()
  -> APP_config()
  -> log_init()
  -> Wireless_Init()
  -> GPS_Init()
  -> Sensor_I2C_prepare()
  -> AHRS_Reset()
  -> QMC6309_Init()
  -> QMI8658_Init()
```

主循环源码顺序如下，实际是否进入受 `FeatureSwitch.h` 开关控制：

```text
MainLoop_RunOnce()
  -> GPS_Poll()
  -> Wireless_Poll()
  -> ShipProtocol_RunScheduler()
     -> ShipControl_Tick()
  -> Wireless_SearchSignalPoll()
  -> MAG_StandalonePoll()        // 当前 ENABLE_MAG_STANDALONE_POLL=0，不进入
  -> IMU_ServicePoll()
  -> IMU_AhrsPoll()
  -> Task_Pro_Handler_Callback()
```

调用约束：

- `Wireless_Poll()` 负责把 LT8920 收到的射频载荷推入软件队列。
- `ShipProtocol_RunScheduler()` 负责从无线队列取 payload，并按旧协议找帧、分发、回 `0x12`；同时它也会内部执行 `AutoDrive_Init()`、`AutoDrive_LinkAliveTick()` 和 `AutoDrive_Poll()`。
- 手动开环、手动 yaw 自稳、E 键定速巡航和 GPS 航向保持的最终电机目标统一由 `ShipControl` 仲裁输出。
- 同一主循环不要同时开启 `ShipProtocol_RunScheduler()` 和 `ShipProtocol_Poll()`，否则会重复消费无线队列。

## 5. LT8920 管理层行为

`wireless.c` 的主要职责：

- 初始化 `wireless_port` 和 LT8920 默认寄存器。
- 扫描 ANT1/ANT2 的 RSSI、成功包数、CRC 错误数。
- 管理一个深度为 `WIRELESS_RX_QUEUE_DEPTH=4` 的射频载荷队列。
- TX 后根据调用接口决定是否自动恢复 RX。
- 对 CRC 错误、队列溢出、TX FIFO 异常和超时进行计数和日志输出。

两个发送接口的区别：

| 接口 | 行为 | 使用场景 |
|------|------|----------|
| `Wireless_Send()` | 发完后自动回 RX | 普通无线管理层发送 |
| `Wireless_SendOnChannel()` | 先切指定信道，发完保持空闲态，不自动回 RX | 旧协议配对和 `0x12` 回包，由协议层自己重开 RX |

同步字接口：

| 接口 | 行为 | 使用场景 |
|------|------|----------|
| `Wireless_SetSyncRegs()` | 写 `reg36/reg39` 后自动开 RX | 普通工作态同步字切换 |
| `Wireless_SetSyncRegsIdle()` | 只写 `reg36/reg39` 并保持空闲态 | 对齐旧版 `RF_Encrypt_Config()` |

注意：旧版 `RF_Encrypt_Config()` 实际是同步字配置，不是加密。当前 `LT8920_SetSyncRegs()` 只重写 `reg36/reg39`，`reg37=0x0380`、`reg38=0x5A5A` 保持老版默认值，不能随意清零。

## 6. 旧协议帧格式

旧遥控器协议帧固定格式：

```text
AA | len | cmd | payload... | xor | BB
```

字段定义：

| 字段 | 长度 | 说明 |
|------|------|------|
| `AA` | 1 | 帧头 |
| `len` | 1 | `2 + payload_len` |
| `cmd` | 1 | 命令号 |
| `payload` | N | 命令载荷 |
| `xor` | 1 | 从 `len` 到 payload 末尾所有字节异或 |
| `BB` | 1 | 帧尾 |

示例：

```text
PAIR_REQ:
AA 06 10 seed0 seed1 seed2 seed3 xor BB

THROTTLE:
AA 05 11 lr ud key xor BB

GPS_REPORT:
AA 11 12 <15字节载荷> xor BB
```

实现细节：

- `ShipProtocol_ReceiveHandle()` 会在射频载荷内逐字节找 `0xAA`。
- 找到帧头后按 `len` 字段收够一帧。
- 收到完整 `AA ... BB` 头尾包就说明遥控空口有活动，上位机“遥控链路显示在线”按这个刷新，不要求必须是 `0x11`。
- `AA ... BB` 只能作为显示层在线/链路活动依据；业务分发仍必须校验 `len`、`xor` 和帧尾 `0xBB`。
- 校验 `xor` 和帧尾 `0xBB` 通过后才调用 `ShipProtocol_Dispatch()`。
- 固件电机安全保活只认有效 `0x11` 控制帧，不能用 `0x0F`、`0x12` 或其它非控制帧代替。
- 任何合法帧分发结束后都会立即回发一次 `0x12`。

## 7. 命令号总表

| 命令 | 名称 | 方向 | 载荷 | 当前处理 |
|------|------|------|---------|----------|
| `0x0F` | `PAIR_RSP` | 遥控器 -> 船 | 通常 4 字节 | 仅在配对响应窗口内置 `paired=1` |
| `0x10` | `PAIR_REQ` | 船 -> 遥控器 | 4 字节 seed | 船端配对广播，固定发 10 次 |
| `0x11` | `THROTTLE` | 遥控器 -> 船 | `lr, ud, key` | 手动控制、电机输出、按键处理 |
| `0x12` | `GPS_REPORT` | 船 -> 遥控器 | 固定 15 字节 | GPS、航向、电量、自动驾驶状态回传 |
| `0x13` | `SET_RETURN` | 遥控器 -> 船 | 10 字节点位 | 设置返航点并尝试进入返航 |
| `0x14` | `SET_DESTINATION` | 遥控器 -> 船 | 10 字节点位 | 按收到顺序保存/匹配 1..5 号钓点；匹配已保存点后才尝试进入定点巡航 |
| `0x15` | `SWITCH_AUTO_RETURN` | 遥控器 -> 船 | `switch + 可选 10 字节点位` | 更新 RAM 自动返航开关和返航点 |
| `0x16` | `AUTODRIVE_DIAG` | 船 -> 遥控器/上位机 | 固定 36 字节 | 自动驾驶状态、原因、距离、当前点和目标点诊断上报 |

遥控器命令对齐结论：

| 遥控器命令 | 旧版入口 | 当前入口 | 空口兼容结论 |
|------------|----------|----------|--------------|
| `0x0F` | `WIRELESS_CMD_PAIR_RSP` | `ShipProtocol_HandlePairRsp()` | 命令号和窗口判断保留；当前不再触发旧版 `Compass_calib_start()` |
| `0x11` | `WirelessProtocal_Accelerator_Resolve()` | `ShipProtocol_HandleThrottle()` | 帧格式和三字节载荷对齐；应用层手感因当前手动航向保持不完全等同 |
| `0x13` | `autoDrive_Set_ReturnPosition()` | `AutoDrive_SetReturnPositionRaw()` | 10 字节点位格式对齐，收到后尝试返航 |
| `0x14` | `autoDrive_Set_FishPosition()` | `AutoDrive_SetFishPositionRaw()` | 10 字节点位格式对齐；当前增加钓点编号鉴别，未知点先保存，已保存点才尝试去目标点 |
| `0x15` | `autoDrive_Set_Switch()` | `AutoDrive_SetSwitchRaw()` | 完整 11 字节旧遥控器帧兼容；当前对短包增加长度保护 |

任意合法 `0x0F/0x11/0x13/0x14/0x15` 帧分发后都会立即发送一次 `0x12` 回包。`0x16` 是船端主动诊断上报，不改变旧遥控器必需的 `0x12` 固定回包节奏。旧遥控器串口接收依赖固定命令号和固定字段位置，所以这些字节位置不能改。

## 8. 配对流程

当前配对参数：

| 参数 | 当前值 | 来源 |
|------|--------|------|
| 配对信道 | `0x7F` | `PAIR_CHANNEL` / `SHIP_PAIR_CHANNEL_DEFAULT` |
| seed | `65 65 A0 65` | `SHIP_PAIR_SEED0..3`，当前未使用芯片 ID |
| 配对发送次数 | `10` | `SHIP_PAIR_SEND_TIMES` |
| 配对发送间隔 | `30` 个协议调度节拍 | `SHIP_WAIT_TICKS_DEFAULT` |
| 协议调度节拍 | 约 `10ms` | `ShipProtocol_RunScheduler()` |
| 配对响应窗口 | `500` 个协议调度节拍 | `SHIP_PAIR_WAIT_RSP_TICKS` |

由 seed 派生出的当前工作参数：

```text
seed      = 65 65 A0 65
work_ch   = 13
key0      = 32
key1      = 30
reg36     = 0x2020
reg39     = 0x1E1E
```

派生算法保持旧版：

```c
key0 = low_nibble(seed0) + ((seed3 >> 2) + (seed3 % 0x03));
key1 = low_nibble(seed1) + ((seed2 >> 3) + (seed0 % 0x06));

channel = (((seed3 + 0x06) % 0x40)
         + ((seed2 >> 3) * 0x08)
         + (((seed1 | seed0) % 0x08) / 2)) % 0x40;
```

配对时序：

```text
BOOT_WAIT
  -> 每 30 个调度节拍发送一次 PAIR_REQ(0x10)
  -> 共发送 10 次
  -> 写 reg36/reg39，并保持空闲态
  -> 切到 work_ch RX
  -> 打开 500 个调度节拍的 PAIR_RSP 窗口
  -> 收到合法 0x0F 后 paired=1
  -> 进入 WORK_RX
```

配对成功后不把信道/key 写入 flash；每次上电仍按 seed 重新配对和派生工作参数。

## 9. `0x11` 手动控制

空口载荷格式：

```text
payload[0] = lr   // 左右摇杆，中心 100
payload[1] = ud   // 前后油门，中心 100
payload[2] = key  // 按键码
```

按键码：

| 按键 | 值 | 当前行为 |
|------|----|----------|
| 无按键 | `0xA0` | 不动作 |
| E | `0xA1` | 带符号油门 `raw_ud-100 >= +60`、`abs(raw_lr-100) <= 8` 且 `abs(gyro_z) <= 8dps` 时进入定速巡航；巡航中再按一次 E 退出；巡航中带符号油门 `raw_ud-100 <= -50` 退出；收到 E 但油门不够会打印 `cruise ignore`，油门够但船体还在转会打印 `cruise ignore reason=not-straight`；同时关闭自动驾驶模式 |
| A | `0xA3` | 保留船灯入口，但当前未绑定真实灯控 GPIO，只记录 `light-unbound` |
| B | `0xA5` | 不处理 |
| C | `0xA7` | 不处理 |
| D | `0xA9` | 不处理 |

当前控制路径：

- 收到 `0x11` 后刷新 `last_throttle_rx_ms` 和 `last_proto_rx_ms`。
- 上位机“遥控链路显示在线”按 `AA ... BB` 包活动刷新；这里的 `0x11` 只负责控制输入和电机安全保活。
- 若 AutoDrive 正忙，只处理按键和链路保活，不接管手动电机。
- 非 AutoDrive 状态下，`0x11` 只把 `lr/ud/key` 提交给 `ShipControl_UpdateManualInput()`。
- 当前工作区的手动控制不是旧版纯开环：`ShipControl` 会做轴滤波、死区曲线、油门/转向混合，并在 `SHIP_YAW_HOLD_MANUAL_ENABLE=1`、前进油门成立且左右输出差小于 20% 时叠加手动航向保持。
- 如果要求应用层行为严格等同旧版 `WirelessProtoca_Motor_Control()`，需要关闭 `SHIP_YAW_HOLD_MANUAL_ENABLE`。

E 键定速巡航当前行为：

- 进入门槛：`raw_ud - 100 >= 60`、`abs(raw_lr - 100) <= 8`、`abs(MainLoop_GetGyroZDps100()/100) <= 8dps`，并且航向 ready。
- 进入时调用 `ShipControl_RequestCruise(MainLoop_GetHeadingDeg100(), 760)`，锁定进入瞬间的融合航向。
- 控制层会做 `1.8s` 软启动，`base` 从约 `520` 拉到 `760`，避免起步一段弧线。
- 运行时不使用 GPS 位置，只按 IMU/AHRS 融合航向做 yaw-hold。
- 退出：巡航中再次按 E，或巡航中 `raw_ud - 100 <= -50`。
- 若油门够但左右杆未回中或船体仍有明显角速度，会拒绝进入并打印 `cruise ignore reason=not-straight ...`。

安全保护：

| 条件 | 行为 |
|------|------|
| 收到完整 `AA ... BB` 头尾包 | 上位机显示层刷新遥控链路在线/活动时间 |
| 超过 `SHIP_THROTTLE_TIMEOUT_MS=1500ms` 未收到有效 `0x11` | 固件判定控制帧超时，停机 |
| 配对后工作信道超过 `SHIP_THROTTLE_RECOVER_MS=3000ms` 没有任何合法协议帧 | 重新整理默认 RF 参数并重开工作 RX |
| 任意合法协议帧处理结束 | 立即回发一次 `0x12` |

## 10. `0x12` GPS/状态回传

`0x12` 载荷固定 15 字节，不能新增字段：

| offset | 字段 | 长度 | 说明 |
|--------|------|------|------|
| 0 | `sat` | 1 | 卫星数，优先 GSA PRN 计数，否则 GGA 卫星数，最大 24 |
| 1..2 | `angle` | 2 | 航向角整数度，高字节在前 |
| 3 | `E` | 1 | 固定字符 `E`，对齐旧版 `nmea41_Get_EW()` |
| 4..5 | `lon1` | 2 | 经度整数段 `dddmm`，高字节在前 |
| 6..7 | `lon2` | 2 | 经度小数段，小数点后 4 位，高字节在前 |
| 8 | `W` | 1 | 固定字符 `W`，对齐旧版 `nmea41_Get_NS()` |
| 9..10 | `lat1` | 2 | 纬度整数段 `ddmm`，高字节在前 |
| 11..12 | `lat2` | 2 | 纬度小数段，小数点后 4 位，高字节在前 |
| 13 | `power_level` | 1 | 电量等级 `0..4` |
| 14 | `autodrive_status` | 1 | `0=无自动驾驶`，`1=返航`，`2=去目标点` |

重要兼容点：

- 半球字段固定 `E/W` 是旧遥控器兼容要求，不要改成真实 `E/W/N/S`。
- 旧版 `nmea41_Get_EW()` 固定返回 `'E'`，`nmea41_Get_NS()` 固定返回 `'W'`；当前源码按这两个字节对齐旧遥控器。
- 真实半球只用于日志中的 `gps state`，不进入 `0x12` 载荷半球字节。
- 坐标优先使用 RMC 原始 NMEA 拆出的 `legacy_lon1/lon2/lat1/lat2`。
- RMC 原始字段不可用时，才从 `deg1e7` 转成旧版 `dddmm.mmmm/ddmm.mmmm`。
- 航向角发送整数度，即 `course_deg_x100 / 100`。
- 当前 `ship_protocol.c` 对所有 `u16` 回包字段按高字节在前写入。不要在文档或代码里把字段顺序改成主机内存布局。

典型日志：

```text
[SHIP] I: tx cmd=0x12 ch=13 payload_len=15 sat=... angle=... power=0x.. auto=0x..
[SHIP] I: gps payload oldfmt ew=E lon1=... lon2=... ns=W lat1=... lat2=...
[SHIP] I: gps payload bytes=...
```

## 11. 电量采样与低电返航

电量采样当前使用当前工程已有检测电压通道：

```c
adc_raw = Get_ADCResult(ADC_CH8);
```

板级差异必须说明清楚：

- 旧工程 `power_Adc.c` 使用 `ADC_CH9`，对应旧板电池检测脚。
- 当前 Black Pearl v1.1 板上检测电压接在 `P0.0 / ADC_CH8`，上位机和遥控器看到的电量字段都应来自这个通道。
- 当前移植保留旧版阈值、`0..4` 等级、100 次采样节拍和低电返航条件；ADC 通道按当前硬件改为 `ADC_CH8`。

回传给遥控器的是旧版电量等级，不是原始 ADC：

| 非 12V 阈值 | 等级 |
|-------------|------|
| `adc >= 1710` | `4` |
| `adc >= 1630` | `3` |
| `adc >= 1530` | `2` |
| `adc >= 1420` | `1` |
| `< 1420` | `0` |

| `BOARD_12V` 阈值 | 等级 |
|------------------|------|
| `adc >= 2000` | `4` |
| `adc >= 1900` | `3` |
| `adc >= 1730` | `2` |
| `adc >= 1620` | `1` |
| `< 1620` | `0` |

采样节拍：

- `SHIP_POWER_SAMPLE_DIVIDER=100`，约每 100 个协议调度节拍更新一次电量等级。
- 低电检查每个协议调度节拍进入一次。
- `SHIP_LOWPOWER_CHECK_TICKS=600`，持续低电约 600 个调度节拍后触发检查。

低电自动返航触发条件：

```text
power_level == 0
AutoDrive_GetMode() == AUTO_DRIVE_CLOSE
ShipControl_GetManualAccelerator() < 10
```

满足条件后调用 `AutoDrive_TriggerReturnWithReason(AUTODRIVE_DIAG_REASON_LOW_POWER)`。是否真正进入返航，还要看已保存返航点是否合法、当前 GPS 是否可用、距离是否满足 AutoDrive 激活条件。

## 12. `0x13/0x14/0x15` 点位与自动驾驶

旧版点位空口格式固定 10 字节：

| offset | 字段 | 长度 | 说明 |
|--------|------|------|------|
| 0 | `lon_dir` | 1 | 经度方向字符 |
| 1..2 | `lon_whole` | 2 | 经度整数段，高字节在前 |
| 3..4 | `lon_frac` | 2 | 经度小数段，高字节在前 |
| 5 | `lat_dir` | 1 | 纬度方向字符 |
| 6..7 | `lat_whole` | 2 | 纬度整数段，高字节在前 |
| 8..9 | `lat_frac` | 2 | 纬度小数段，高字节在前 |

命令行为：

| 命令 | 载荷 | 当前行为 |
|------|---------|----------|
| `0x13` | 10 字节点位 | `AutoDrive_SetReturnPositionRaw()`，设置返航点并尝试进入返航 |
| `0x14` | 10 字节点位 | `AutoDrive_SetFishPositionRaw()`，按收到顺序保存/匹配 `1..5` 号钓点；匹配已保存点后尝试进入目标点巡航 |
| `0x15` | `switch + 可选 10 字节点位` | 更新 RAM 自动返航开关；若长度至少 11 字节，同时更新 RAM 返航点；开关不为 `0x30` 时立即尝试返航 |

与旧版差异：

- 旧版 `0x15` 直接从开关字节后拷贝 10 字节点位。
- 当前实现加了长度保护：短包只更新开关，不更新返航点。
- 正常旧遥控器发送完整 11 字节载荷时，空口行为兼容。
- 为匹配遥控器“返航指令”的现场语义，`0x15` 开启状态现在会立即调用 `AutoDrive_TriggerReturnWithReason()` 尝试返航；如果 GPS、卫星数、距离门槛不满足，日志会打印 AutoDrive 快照说明原因。
- `0x14` 不是一次接收 5 个钓点，而是最多先后接收 5 次，每次 1 个 10 字节点位，按收到顺序分配为 `1..5`。
- 如果只有 1 号钓点有效，停过短时间去重窗口后再次收到同一坐标即可尝试去 1 号，不需要等待 2..5 号。
- 同一坐标连续重发会返回 `dup-wait`，用于过滤遥控器/RF 重发，不触发去点。
- 钓点保存后在本次上电期间一直有效；去过一次、到点停车、手动停止、超时失败或模式切换不会删除该编号。
- 5 个钓点槽位已满后，未匹配任何已保存钓点的新坐标会被拒绝，防止误去未知点。

`0x14` 数据日志写到 `DATA` tag，便于遥控器日志串口过滤：

```text
[DATA] I: 0x14 rx fl=15 pl=10 xor=38/38 res=1(store) idx=1
[DATA] I: 0x14 point ew=0x45 lon=12156.6298 ns=0x57 lat=3725.3437
[DATA] I: 0x14 payload=45 2F 7C 18 9A 57 0E 8D 0D 6D
[DATA] I: 0x14 frame=AA 0C 14 ...
```

`res` 含义：

| 值 | 名称 | 含义 |
|----|------|------|
| `0` | `busy` | AutoDrive 当前不在 `IDLE`，忽略本次去点命令 |
| `1` | `store` | 新钓点已保存，`idx` 为自动分配编号 |
| `2` | `dup-wait` | 同一坐标短时间连续重发，已过滤，不触发去点 |
| `3` | `reject-unknown` | 槽位已满且坐标不属于已保存钓点 |
| `4` | `reject-distance` | 已匹配钓点，但 GPS/距离等激活条件不满足 |
| `5` | `start` | 已匹配钓点并进入去钓点流程 |
| `6` | `invalid` | 载荷坐标无效 |

AutoDrive 激活条件：

- 当前状态必须是 `AUTO_DRIVE_IDLE`。
- 目标点经度方向必须是 `E` 或 `W`。
- 目标点经度整数段不能为 0。
- 当前 GPS 可用，卫星数至少 7，当前经纬度非 0。
- 当前点到目标点距离必须大于 10m 且小于 800m。

## 13. `0x16` 自动驾驶诊断上报

`0x16` 是当前工程新增的船端诊断上报，不属于旧遥控器必须解析的控制命令。它用于上位机或调试工具观察 AutoDrive 状态，不改变 `0x12` 的固定 15 字节回包。

诊断载荷固定 36 字节：

| offset | 字段 | 长度 | 说明 |
|--------|------|------|------|
| 0 | `version` | 1 | 当前为 `0x01` |
| 1 | `state` | 1 | AutoDrive 状态 |
| 2 | `mode` | 1 | AutoDrive 模式 |
| 3 | `auto_ret_onoff` | 1 | 自动返航开关保存值 |
| 4 | `fail_flag` | 1 | 最近一次激活失败标志 |
| 5 | `last_reason` | 1 | 最近一次诊断原因 |
| 6 | `gps_ready` | 1 | GPS 是否满足自动驾驶条件 |
| 7 | `sat_count` | 1 | 当前卫星数 |
| 8 | `can_activate_target` | 1 | 当前目标是否可激活 |
| 9 | `reserved` | 1 | 保留，当前为 0 |
| 10..11 | `distance_to_target_m` | 2 | 到目标点距离，米，高字节在前 |
| 12..13 | `current_heading_deg` | 2 | 当前航向整数度，高字节在前 |
| 14..15 | `target_heading_deg` | 2 | 目标航向整数度，高字节在前 |
| 16..25 | `current_point` | 10 | 当前点，沿用旧版 10 字节点位格式 |
| 26..35 | `target_point` | 10 | 目标点，沿用旧版 10 字节点位格式 |

发送节奏：

- `SHIP_AUTODRIVE_DIAG_ENABLE=1` 时启用。
- 配对完成后才发送。
- 状态、模式、原因、开关或失败标志变化时，满足 `SHIP_AUTODRIVE_DIAG_MIN_GAP_MS=200ms` 最小间隔即可上报。
- 有跟踪状态时，即使没有变化，也会按 `SHIP_AUTODRIVE_DIAG_PERIOD_MS=1000ms` 周期上报。

## 14. 自动返航配置存储

当前自动返航配置是 RAM-only，不再写 STC flash/EEPROM。
`AutoDriveCfg_Save()` 只是更新 `autodrive_cfg.c` 内部 RAM 配置，复位或重新上电后回到默认值。

默认值：

- `auto_ret_onoff = 0x30`
- 返航点全部清零

影响：

- `0x13`、`0x15` 更新的返航点和开关只在本次上电期间有效。
- 低电、链路超时或 `0x15` 触发返航时，使用的是当前 RAM 中的返航点。
- 钓点列表同样只保存在 RAM 中，本次上电期间不会因去点/到点/停车而清空；重新上电后需要遥控器重新下发。

## 15. 诊断日志

默认诊断开关当前较保守：

| 开关 | 当前值 | 说明 |
|------|--------|------|
| `SHIP_PROTOCOL_DIAG_ENABLE` | `0` | 关闭协议详细日志 |
| `SHIP_PROTOCOL_ERROR_LOG_ENABLE` | `0` | 关闭协议错误扩展日志 |
| `WIRELESS_RX_TRACE_ENABLE` | `0` | 关闭无线收包详细转储 |
| `WIRELESS_TX_TRACE_ENABLE` | `0` | 关闭无线发包详细转储 |
| `SHIP_ADC_LOG_ENABLE` | `1` | ADC 日志开关，但仍受 `SHIP_PROTOCOL_DIAG_ENABLE` 门控 |

现场联调时建议临时打开：

```c
#define SHIP_PROTOCOL_DIAG_ENABLE      1
#define WIRELESS_RX_TRACE_ENABLE       1
```

重点看这些日志：

```text
[WL]   init ok ch=...
[WL]   scan ant1 rssi=... ant2 rssi=... final=...
[SHIP] scheduler init ...
[SHIP] pair req start ...
[SHIP] pair req frame=AA 06 10 ...
[SHIP] pair ok by aa-bb-frame cmd=... work_rx=... key=...
[SHIP] manual control online by cmd=0x11
[SHIP] rc cmd=0x11 lr=... ud=... key=...
[SHIP] tx cmd=0x12 ch=... payload_len=15 ...
[SHIP] gps payload bytes=...
[SHIP] tx cmd=0x16 state=... mode=... sw=... reason=... gps=... sat=... dist=...
[DATA] I: 0x14 rx fl=... pl=10 xor=.../... res=...(...) idx=...
[DATA] I: 0x14 point ew=0x.. lon=... ns=0x.. lat=...
[SHIP] remote link online by aa-bb-frame cmd=...
[SHIP] remote link timeout by aa-bb-frame, dt=...ms
[SHIP] manual control timeout by cmd=0x11, dt=...ms
```

## 16. 联调验收清单

配对：

- 能看到 `Wireless_Init()` 成功。
- 能看到 10 次 `PAIR_REQ(0x10)` 发送。
- seed、工作信道、key 与预期一致：`65 65 A0 65`、`work_ch=13`、`key=32/30`。
- 收到 `PAIR_RSP(0x0F)` 后 `paired=1`。

遥控：

- 上位机“遥控链路”显示收到完整 `AA ... BB` 头尾包就应刷新在线，不要只盯 `0x11`。
- 遥控器动作时能看到 `rc cmd=0x11 lr=... ud=... key=...`。
- 有效 `0x11` 失联超过 `1500ms` 后电机停机。
- `A/B/C/D/E` 按键日志和动作符合本文档表格。

GPS 回传：

- 任意合法帧后都会回 `0x12`。
- `0x12` 载荷长度固定 15。
- 半球字节固定为 `E/W`。
- 经纬度字段是旧版 `dddmm.mmmm/ddmm.mmmm` 拆分格式。
- 电量字段是 `0..4` 等级，不是 ADC 原始值。

自动驾驶诊断：

- 配对后 AutoDrive 状态变化时可看到 `0x16` 诊断上报。
- `0x16` 载荷长度固定 36。
- 诊断点位字段沿用旧版 10 字节格式，不影响 `0x12` 回包字段。

点位和返航：

- `0x13/0x14` 载荷必须至少 10 字节。
- `0x15` 正常完整载荷为 11 字节。
- `0x14` 首次未知点应看到 `[DATA] ... res=1(store) idx=1..5`。
- 同一波重发应看到 `[DATA] ... res=2(dup-wait) idx=对应编号`。
- 停过短时间去重窗口后再次发送已保存点，应看到 `[DATA] ... res=5(start) idx=对应编号`，若距离/GPS 不满足则为 `res=4(reject-distance)`。
- 到点或手动停止后再次发送同一已保存点，仍应看到同一 `idx`，不会重新变成 `store` 或丢失。
- 自动返航配置只保存到 RAM，不再检查 `0x0001F800` flash 区。
- 低电返航只在电量等级为 0、自动驾驶关闭、油门低于 10 时触发入口。

## 17. 禁止修改项

为了保持旧遥控器兼容，以下内容不要随意改：

- 不要改 `AA | len | cmd | payload | xor | BB` 帧格式。
- 不要改 `len = 2 + payload_len`。
- 不要改 `xor` 计算范围。
- 不要把上位机“显示在线”和固件 `0x11` 控制保活混成同一个状态；显示层按 `AA ... BB` 包活动，电机安全按有效 `0x11`。
- 不要改命令号 `0x0F~0x15`。
- 不要向 `0x12` 增加字段。
- 不要把 `0x16` 诊断字段塞进 `0x12`；旧遥控器依赖 `0x12` 固定 15 字节布局。
- 不要把 `0x12` 半球字段改成真实 `N/S/E/W`。
- 不要把 `0x12` 电量字段改回 ADC 原始值。
- 不要把当前板级电量采样通道改回旧板 `ADC_CH9`，除非硬件也改回旧板接法。
- 不要清零 LT8920 `reg37/reg38`。
- 不要把 `Wireless_Receive()` 的射频载荷当成完整协议帧直接分发。

## 18. 当前已知差异

| 差异 | 当前处理 | 影响 |
|------|----------|------|
| `0x11` 应用层不是旧版纯开环 | 当前打开 `SHIP_YAW_HOLD_MANUAL_ENABLE=1`，且仅在前进油门、左右输出差小于最大单侧输出 20% 时叠加 yaw-hold | 空口兼容，但手感不完全等同旧版 |
| `AutoDrive` 入口隐藏在协议调度器内 | `ShipProtocol_RunScheduler()` 会初始化并轮询 `AutoDrive` | 阅读主循环时容易误以为 AutoDrive 未接入 |
| A 键灯控引脚未确认 | 只记录 `light-unbound`，不绑定 GPIO | 遥控器能下发，船灯不动作 |
| `0x0F` 配对响应不再触发旧版罗盘校准 | 当前只置 `paired=1` 并进入工作信道 | 配对协议兼容，但旧版 `Compass_calib_start()` 副作用未恢复 |
| 电量 ADC 通道不同 | 旧板是 `ADC_CH9`，当前板检测电压是 `ADC_CH8` | 协议字段和阈值对齐旧版，采样脚按当前硬件 |
| `0x15` 短包防御 | 短包只保存开关，不更新返航点 | 正常 11 字节旧遥控器帧不受影响 |
| 自动返航配置不写 flash | `0x13/0x15` 只更新 RAM 配置 | 掉电后返航点和开关恢复默认值 |
| `0x14` 钓点鉴别 | 最多先后保存 5 个钓点，已保存坐标才触发去点 | 防止新未知坐标直接误触发去点 |
| 配对参数不持久化 | 每次上电重新按 seed 派生 | 上电需重新跑配对节奏 |
| 自动驾驶可用性受 GPS 条件限制 | 点位合法、卫星数、距离都要满足 | 收到命令不等于一定进入巡航 |

## 19. 相关文件

- `Code_boweny/Device/WIRELESS/wireless_port.h`
- `Code_boweny/Device/WIRELESS/wireless_port.c`
- `Code_boweny/Device/WIRELESS/lt8920.h`
- `Code_boweny/Device/WIRELESS/lt8920.c`
- `Code_boweny/Device/WIRELESS/wireless.h`
- `Code_boweny/Device/WIRELESS/wireless.c`
- `Code_boweny/Device/WIRELESS/ship_protocol.h`
- `Code_boweny/Device/WIRELESS/ship_protocol.c`
- `Code_boweny/Device/AutoDrive/autodrive.c`
- `Code_boweny/Device/AutoDrive/autodrive_cfg.c`
- `User/FeatureSwitch.h`
- `User/MainLoop.c`
- `User/System_init.c`

旧版对照：

- `ship_Gps_V2.1_20260406-115200/App/Wireless/wirelessProtocal.c`
- `ship_Gps_V2.1_20260406-115200/App/Wireless/wirelessProtocal.h`
- `ship_Gps_V2.1_20260406-115200/App/Gps/nmea41_protocal.c`
- `ship_Gps_V2.1_20260406-115200/App/ADC/power_Adc.c`
- `ship_Gps_V2.1_20260406-115200/App/AutoDrive/autoDrive.c`

## 20. 版本记录

| 日期 | 版本 | 说明 |
|------|------|------|
| 2026-05-23 | v1.9 | 同步 E 键定速巡航进入门槛、760 请求速度、1.8s 软启动和 `not-straight` 拒绝日志，说明起步弧线与 S 型回摆相关调参。 |
| 2026-05-23 | v1.8 | 同步 `0x14` 钓点 1..5 自动编号鉴别、`DATA` tag 串口日志和 AutoDrive RAM-only 返航配置。 |
| 2026-05-23 | v1.7 | 同步 `ship_protocol.c` 当前行为，补充 `0x16` AutoDrive 诊断上报、低电返航原因接口和工作信道静默恢复说明。 |
| 2026-05-13 | v1.6 | 补充 `AutoDrive` 实际接入路径、`0x11` 手动 yaw-hold 触发条件和当前主链说明，收口到根目录代码真实状态。 |
| 2026-05-13 | v1.5 | 补齐遥控器命令对齐表、ADC_CH8/ADC_CH9 板级差异说明、主循环条件入口和禁止修改项，统一中文表述。 |
| 2026-05-13 | v1.4 | 按当前工作区真实代码重写 README，补全硬件、开关、配对、帧格式、命令、GPS 回传、电量、点位、flash 存储、联调验收和已知差异。 |
| 2026-05-07 | v1.3 | 二次复核老版无线业务，补回 `0x12` 回传后工作 RX 恢复、长静默恢复节奏和左右转极性说明。 |
| 2026-05-07 | v1.2 | 记录开环控船联调版、`0x11`、`0x12` 和按键入口。 |
| 2026-05-06 | v1.1 | 完成 LT8920 管理层、旧协议配对链路和基础状态回传接入。 |
| 2026-04-26 | v1.0 | 新建 `WIRELESS` 模块，完成 `LT8920 + KCT8206L` 板级驱动框架。 |
