# Services 服务层

`Services/` 保存工程级轻量服务。服务层不直接访问 STC 官方驱动、裸寄存器或裸
引脚；需要硬件能力时，通过 `BoardDevices/` 提供的板级 API 完成。

## logger 日志服务

旧版 `Code_boweny/Function/Log/` 已迁移为当前 `logger` 服务。

### 文件

```text
Services/
├── Inc/
│   ├── logger.h    # 新日志 API
│   ├── Log.h       # 旧 include 兼容入口
│   └── parameter_store.h
└── Src/
    ├── logger.c    # 日志实现
    └── parameter_store.c
```

底层输出链路：

```text
logger -> board_console -> ef_uart -> STC32G_UART/STC32G_NVIC
```

### 硬件资源

日志输出使用 `BoardDevices/board_console`：

- UART：UART1
- TXD/RXD：P3.1/P3.0
- 波特率：115200
- 数据格式：8N1
- 单条缓冲：128 字节
- 最大消息长度：127 字符，不含末尾 `\r\n`；超长内容会被截断。
- 当前有界格式化支持 `%d`、`%i`、`%u`、`%ld`、`%lu`、`%x`、`%X`、`%s`、
  `%c`、`%%` 和常用宽度/零填充，例如 `%02X`。

### 初始化顺序

`App/Src/app.c` 当前按下面顺序启用日志：

```text
board_console_init()
log_init()
```

`log_init()` 只打开日志状态并输出启动横幅，不再负责 UART 初始化。这样和 v1.0
里的 `UART_config() -> log_init()` 语义保持一致，只是 UART 初始化被替换成了
板级 API。

### API

```c
void log_init(void);
void log_info(u8 *tag, u8 *fmt, ...);
void log_warn(u8 *tag, u8 *fmt, ...);
void log_error(u8 *tag, u8 *fmt, ...);
void log_debug(u8 *tag, u8 *fmt, ...);
void log_printf(u8 *fmt, ...);
```

常用宏：

```c
LOGI("SYS", "init ok");
LOGW("I2C", "bus busy");
LOGE("IMU", "read failed status=%u", status);
LOGD("CTRL", "target=%d", target);
```

### 输出格式

```text
[tag] I: message\r\n
[tag] W: message\r\n
[tag] E: message\r\n
[tag] D: message\r\n
message\r\n
```

### 迁移注意

- v1.0 代码原来包含 `Log.h` 的地方可以先保持不动，当前 `Services/Inc/Log.h`
  会转发到 `logger.h`。
- 新代码优先包含 `logger.h`。
- 不要在 `App/` 里包含 `STC32G_UART.h` 或调用 `PrintString1()`。
- STC32G 无 FPU，日志格式不要使用 `%f`；当前 logger 会把不支持的格式按原样
  输出，浮点小数应先转成整数部分和小数部分。

## parameter_store 参数服务

`parameter_store` 是当前工程的轻量配置服务，只保存 App 层传入的配置字节，
不理解 AutoDrive 结构体语义。底层链路为：

```text
AutoDriveCfg_Save/Load -> parameter_store -> board_storage -> STC32G_EEPROM
```

当前用途：

- 保存 10 字节旧格式返航原点。
- 不保存钓点；`0x14` 钓点由遥控器每次下发，船端只保留当前运行态目标。
- 不保存自动返航开关；`0x15` 的开关值只影响本次运行态，带返航原点时才触发 flash 写入。
- 通过 `ADCFG ld origin/ld def`、`ADCFG sv origin/sv fail` 日志确认原点读写结果。

服务层仍然不直接包含 STC EEPROM 头文件；EEPROM/IAP 细节只允许出现在
`BoardDevices/Src/board_storage.c`。

## 当前联调日志关键词

无线、控制和 AutoDrive 联调时，重点看这些 tag/关键词：

- `[SHIP] scheduler init`：协议状态机初始化。
- `[SHIP] pair req sent` / `[SHIP] pair ok` / `[SHIP] enter work-state`：配对和工作信道。
- `[SHIP] rc cmd=0x11`：手动遥控帧输入。
- `[SHIP] boot blk` / `[SHIP] boot ready`：开机保护和航向 ready 门控。
- `[DATA] cruise enter` / `[DATA] cruise exit` / `[SHIP] cruise reject`：E 键巡航。
- `[CTRL] out mode=`：最终左右电机输出，含 mode、motion、base、diff、left、right。
- `[SHIP] 0x14 rx fl=`：去定点命令诊断，含 frame/payload/xor/result/index。
- `[SHIP] tx cmd=12`：旧遥控器固定状态回包。
- `[SHIP] tx16 st=`：AutoDrive 主动诊断上报。
- `[ADCFG] ld` / `[ADCFG] sv`：返航配置读写。
- `[SHIP] adc raw=` / `[SHIP] low power latched`：电源采样和低电返航触发。

## 当前事件/日志对齐

- `[SHIP] key edge key=`：按键边沿日志。A/E/unknown 保持 `KEY_EDGE`；B/C/D 分别记录
  `B-noop`、`C-noop`、`D-noop`，并发布 `SHIP_PROTOCOL_EVENT_KEY_ACTION`。
- B/C/D 的 `key_action` 分别为 `SHIP_PROTOCOL_KEY_ACTION_B_NOOP`、
  `SHIP_PROTOCOL_KEY_ACTION_C_NOOP`、`SHIP_PROTOCOL_KEY_ACTION_D_NOOP`，不驱动硬件。
- A 仍保持现有 `A-light` 日志和 `KEY_EDGE` 语义；当前没有 `board_light` 或 GPIO 调用。
- `[SHIP] adc raw=` 对应电源采样。有效采样发布 `POWER_SAMPLE`，电量等级变化发布
  `POWER_LEVEL_CHANGED`。
- `[SHIP] low power latched` 只会在 `power_level == 0`、超过 `600` 个 scheduler tick、
  AutoDrive 为 close、手动油门小于 `10` 后出现，并发布 `LOW_POWER_LATCHED`。
- `[EVT] ...` 来自 `app_ship_event_poll()`，它每轮从 8 深度协议事件队列 FIFO
  drain 事件；高频 throttle/power sample 默认不额外刷日志。
- `[SPI-PS] init ok` 表示 `app_loop()` 会轮询 `board_spi_ps_service()`；完整 RX 帧或截断帧会发布
  `SPI_PS_FRAME_RX`。SPI-PS 初始化失败时保持静默。
- `[SPI-PS] disabled shared SPI` 表示生成配置仍标记 SPI-PS 与 LT8920
  共用 STC SPI 外设，`board_spi_ps_init()` 已用资源护栏阻止误启用。
