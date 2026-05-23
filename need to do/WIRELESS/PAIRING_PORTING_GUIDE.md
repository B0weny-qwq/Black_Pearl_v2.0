# 无线配对移植手册（LT8920）

本文档只覆盖 `App/Wireless` 里的“无线配对 + 进入工作信道”逻辑，目标是把当前 STC8 平台实现拆成可以迁移到任意 MCU/RTOS 的实现说明。

## 1. 适用范围

- 当前工程：`ship_Gps_V2.1_0314-115200`
- 当前无线硬件：双 LT8920（`LT8920_SEND` + `LT8920_REC`）
- 当前调度基准：`10ms` 周期任务（`SysTimer_Get_10MsFlag()`）
- 关键入口：
  - `LT89xx_INIT()`：LT8920 初始化
  - `Radio_progress()`：配对/工作阶段状态推进
  - `WirelessProtocal_Receive_Handle()`：收包解析
  - `WirelessProtocal_Resolve_Handle()`：命令分发

## 2. 先给结论（和代码真实行为一致）

1. `channel` 是有用到的，但不是“配对成功后一次性重配”，而是每次收发时动态写 LT8920 寄存器 7（信道/模式）。
2. 当前实现没有把配对结果（信道/key）写入 EEPROM/Flash 持久化。
3. 当前实现没有发送本端 `PAIR_RSP(0x0F)` ACK，只有“接收并处理” `PAIR_RSP`。

## 3. 配对阶段完整时序

## 3.1 上电后初始化

1. 主循环启动前调用 `LT89xx_INIT()` 初始化两颗 LT8920
3. 进入主循环，每 10ms 调一次 `Radio_progress()`。

## 3.2 `Radio_progress()` 状态推进

`Radio_progress()` 内部静态变量：

- `lt8920_waitTimes = 30`
- `pairSendTimes = 10`
- `rf_send_rec_Times = 0`

行为分两段：

### A. 配对发送段（`pairSendTimes > 0`）

每当 `lt8920_waitTimes` 倒数到 0：

1. `pairSendTimes--`
2. `lt8920_waitTimes = 30`（下一次动作再等约 300ms）
3. 调用 `RF_Pair(Pair_Channel, LT8920_SEND)` 发送一包 `WIRELESS_CMD_PAIR(0x10)`
4. 如果刚好降到 `pairSendTimes == 0`：
   - 调 `RF_Encrypt_Config(LT8920_SEND)`
   - 调 `RF_Encrypt_Config(LT8920_REC)`
   - 设置 `pair_wait_rsp_time = 500`

换算时间（按 10ms tick）：

- 配对包发送间隔：`30 * 10ms = 300ms`
- 共发送 10 次：约 `3s`
- 最后一次配对发送后，开启 `pair_wait_rsp_time` 响应窗口

### B. 工作阶段（`pairSendTimes == 0`）

1. `rf_send_rec_Times++`
2. `rf_send_rec_Times > 80` 时：
   - 发送 `WIRELESS_CMD_UPLOAD_GPS(0x12)`，信道 `wirelessConfig.RF_Channel[2]`
   - `rf_send_rec_Times = 0`
3. 其余时间：
   - 接收模式，信道 `wirelessConfig.RF_Channel[0]`

换算：

- 大约每 `81 * 10ms = 810ms` 发一次 GPS/状态包
- 其它 tick 主要保持接收遥控包

## 3.3 配对请求包内容如何构造

`RF_Pair()` 做了三件事：

1. `RF_key_format(pairData)` 生成 4 字节种子数据
2. 通过这 4 字节派生：
   - `RF_Send_Key[2]`
   - `RF_Channel[3]`
3. 用 `Wireless_Send(..., WIRELESS_CMD_PAIR, pairData, 4, ...)` 发出配对包

### 4 字节种子来源

当前直接读固定地址：

- `0xF4, 0xF5, 0xF6, 0xF7`

这在 8051/STC 上可工作；移植时必须重新定义“这 4 字节从哪里来”（UID、EEPROM、Flash 配置区、出厂参数均可）。

## 3.4 工作信道派生算法

`RF_Pair_Get_Channel()` 逻辑：

```c
channel = (((data3 + 0x06) % 0x40)
         + ((data2 >> 3) * 0x08)
         + (((data1 | data0) % 0x08) / 2)) % 0x40;

RF_Channel[0] = channel;
RF_Channel[1] = channel;
RF_Channel[2] = channel + 0x40;
```

移植要求：此算法若要兼容既有对端，必须保持完全一致（位运算、取模、整除都不能改）。

## 3.5 “配对成功”是如何判定的

接收路径：

1. `RF_Receive()` 读 FIFO 得到 payload
2. `WirelessProtocal_Receive_Handle()` 做 `0xAA ... 0xBB` 帧解析 + XOR 校验
3. `WirelessProtocal_Resolve_Handle()` 根据 `cmdID` 分发

当 `cmdID == WIRELESS_CMD_PAIR_RSP(0x0F)`：

- 仅在 `pair_wait_rsp_time != 0` 时认为有效
- 有效后执行 `Compass_calib_start()`
- 并把 `pair_wait_rsp_time` 清零

`pair_wait_rsp_time` 在 `WirelessProtocal_Accelerator_OutTime_Handle()` 每 10ms 递减 1。

注意：注释写“3 秒内有效”，但代码实际是 `500` tick，即约 `5 秒`。

## 3.6 当前没有做的事（移植时不要误判为漏移植）

- 没有本端 `PAIR_RSP(0x0F)` 回包发送逻辑
- 没有把 `RF_Channel` / `RF_Send_Key` 持久化到 EEPROM/Flash
- 没有“配对成功后立即切固定信道并锁死”的独立状态；收发时每次按传入信道写寄存器 7

## 4. LT8920 层与配对相关的最小行为契约

你移植驱动时，只要保证下面接口行为一致，配对协议就能按原逻辑跑：

1. `LT_WriteReg(reg, H, L, role)`：可稳定写指定角色芯片寄存器
2. `LT_ReadReg(reg, role)`：可稳定读回寄存器
3. `LT_WriteBUF(50, buf, len, role)` 与 `LT_ReadBUF(50, ...)`：FIFO 正常
4. `LT8920_TxData(ch, ...)`：
   - 先 IDLE 到 `ch`
   - 清 FIFO 指针
   - 写 FIFO
   - 切 TX
   - 等 `PKT` 完成
   - 回 IDLE
5. `LT8920_OpenRx(ch, ...)`：
   - IDLE 到 `ch`
   - 清 FIFO 指针
   - 配 RX
   - 切到 RX (`ch | 0x80`)
6. `LT8920_GetPKT(role)`：能稳定给出收发完成标志

## 5. 平台无关改造建议（强烈建议先做）

先把平台相关点抽象到 `wireless_port.*`：

- GPIO 写：`wl_gpio_write(pin, level)`
- GPIO 读：`wl_gpio_read(pin)`
- SPI 传输：`wl_spi_txrx(role, tx, rx, len)`
- 延时：`wl_delay_us/us`、`wl_delay_ms/ms`
- 时间戳：`wl_tick_ms()`
- 日志：`wl_log(...)`
- 临界区：`wl_enter_critical()` / `wl_exit_critical()`

然后 LT8920 驱动只调这些抽象，不直接用 `Pxx`、`delay_xx()`、`printf()`。

## 6. 建议你在新平台实现的配对状态机

即便保留原算法，也建议显式状态机，便于单芯片/双芯片复用。

状态定义：

- `PAIR_BOOT_WAIT`
- `PAIR_TX_BURST`
- `PAIR_APPLY_SYNC`
- `PAIR_WAIT_RSP`
- `WORK_RXTX`

推荐迁移（与现行为等价）：

1. `PAIR_BOOT_WAIT`：300ms 后进入 `PAIR_TX_BURST`
2. `PAIR_TX_BURST`：每 300ms 发一次 `PAIR`，共 10 次
3. `PAIR_APPLY_SYNC`：配置 SEND/REC Sync Word（现函数名 `RF_Encrypt_Config`）
4. `PAIR_WAIT_RSP`：开启 500 tick（5s）窗口
5. 收到 `PAIR_RSP` 且窗口有效 -> 配对成功回调（当前是 `Compass_calib_start()`）
6. 超时未收到 -> 可按产品策略重试（原代码未重试，直接进入工作阶段）
7. `WORK_RXTX`：80:1 接收/上报节奏

## 7. 可直接套用的伪代码

```c
void wireless_10ms_task(void)
{
    radio_progress();                // 配对/收发调度
    wireless_link_timeout_update();  // pair_wait_rsp_time-- 等
}

void radio_progress(void)
{
    static uint16_t wait_ticks = 30;
    static uint16_t pair_left = 10;
    static uint8_t  rx_tx_div = 0;

    if (wait_ticks > 0) {
        wait_ticks--;
        return;
    }

    if (pair_left > 0) {
        pair_left--;
        wait_ticks = 30;

        rf_pair_send(PAIR_CHANNEL);  // cmd=0x10, payload=4-byte seed

        if (pair_left == 0) {
            rf_apply_syncword(SEND_ROLE);
            rf_apply_syncword(RECV_ROLE);
            pair_wait_rsp_time = 500; // 5s
        }
        return;
    }

    rx_tx_div++;
    if (rx_tx_div > 80) {
        rf_send_gps(RF_Channel[2]);
        rx_tx_div = 0;
    } else {
        rf_receive_once(RF_Channel[0]);
    }
}

void on_frame_received(frame_t *f)
{
    if (f->cmd == WIRELESS_CMD_PAIR_RSP) {
        if (pair_wait_rsp_time != 0) {
            on_pair_success();
            pair_wait_rsp_time = 0;
        }
        return;
    }
    // other cmd...
}
```

## 8. 单 LT8920 平台的改法（如果你不是双芯片）

当前工程是双芯片并行收发；单芯片必须改为半双工时分：

1. `PAIR_TX` 发包窗口（短）
2. 立即切 `PAIR_RX` 监听响应窗口（短）
3. 工作态大部分时间 `WORK_RX`
4. 定时切 `WORK_TX_STATUS` 发状态
5. 发完立刻回 `WORK_RX`

最关键约束：

- 发包窗口不能太长，否则会错过遥控控制包
- 失联超时阈值要按新时分策略重算
- 对端如果假设“船端持续接收”，需同步调整对端时序

## 9. 持久化与 ACK（可选增强，不是当前等价移植必需）

如果你希望上电后更快进入工作态，建议新增：

1. 持久化结构（建议）
   - `version`
   - `seed[4]`
   - `rf_channel[3]`
   - `rf_key[2]`
   - `crc16`
2. 启动时先读并校验；有效则直接加载工作参数
3. 后台再做低频重配对（可选）

如果你需要协议更稳，可定义：

- `PAIR_REQ(0x10)` -> `PAIR_RSP(0x0F)` -> `PAIR_ACK(新命令字)` 三次握手

但这会改协议，必须确认对端同步升级。

## 10. 迁移验收清单（配对专项）

1. `PAIR` 包确实在 `Pair_Channel` 发出（抓包或寄存器日志可见）
2. 4 字节 seed 来源明确且稳定
3. `RF_Channel[0..2]` 派生值与旧平台一致
4. SEND/REC 两角色 Sync Word 配置顺序正确
5. `PAIR_RSP` 仅在窗口内生效（5s）
6. 配对成功回调可触发（当前行为：启动罗盘校准）
7. 工作态能稳定收 `0x11`，并约 810ms 上报 `0x12`
8. 长时间运行无 RXBusy 卡死、无 FIFO 异常长度、无状态机失步

## 11. 已知风险与注意事项

1. `Pair_Channel = 0x7F` 的频点解释需结合 LT8920 手册和实测，不要凭 `2402 + channel` 直接下结论。
2. `RF_Encrypt_Config` 实际是 Sync Word 配置，不是加密。
3. 固定地址 `0xF4~0xF7` 在不同 MCU 上不可直接照搬。
4. 现代码注释有少量历史不一致（如“3秒”），移植时以代码执行值为准。

