# LOG 轻量级日志模块

`Code_boweny/Function/Log/` 是基于 UART1 的轻量级日志输出模块，提供带标签、分级别的格式化日志功能。

## 基本信息

| 项目 | 说明 |
|------|------|
| 输出硬件 | UART1，P3.1=TXD，P3.0=RXD |
| 波特率 | 115200，8N1 |
| 最大消息长度 | 127 字符，不含 `\r\n` |
| 缓冲区大小 | 128 字节 |

## 文件结构

```text
Code_boweny/Function/Log/
├── Log.h       # API 声明、宏定义、Doxygen 注释
├── Log.c       # 模块实现
└── README.md   # 本文档
```

## 级别与格式

| 函数 | 宏 | 级别 | 输出格式 |
|------|----|------|----------|
| `log_info(tag, fmt, ...)` | `LOGI(tag, fmt, ...)` | INFO | `[tag] I: message\r\n` |
| `log_warn(tag, fmt, ...)` | `LOGW(tag, fmt, ...)` | WARN | `[tag] W: message\r\n` |
| `log_error(tag, fmt, ...)` | `LOGE(tag, fmt, ...)` | ERROR | `[tag] E: message\r\n` |
| `log_debug(tag, fmt, ...)` | `LOGD(tag, fmt, ...)` | DEBUG | `[tag] D: message\r\n` |
| `log_printf(fmt, ...)` | 无 | RAW | `message\r\n` |

输出示例：

```text
[IMU] I: init begin
[I2C] W: bus not idle scl=1 sda=1
[IMU] E: read failed status=2
```

## 快速使用

日志系统应在 UART1 初始化后启用：

```c
UART_config();
log_init();
```

调用示例：

```c
#include "Log.h"

void MyTask(void)
{
    LOGI("MYTK", "task start");
    LOGW("MYTK", "threshold exceeded val=%u", 200);
    LOGE("MYTK", "operation failed");
    LOGD("MYTK", "loop tick");
    log_printf("raw: %d %d %d", a, b, c);
}
```

## 注意事项

- `log_init()` 必须在 `UART_config()` 之后调用。
- STC32G 无 FPU，`vsprintf()` 不应使用 `%f`。
- 如需输出小数，请先手动转换为整数和小数部分。
- 单条日志超过 127 字符会被截断，不会造成缓冲区越界。

## API

```c
void log_init(void);
void log_info(u8 *tag, u8 *fmt, ...);
void log_warn(u8 *tag, u8 *fmt, ...);
void log_error(u8 *tag, u8 *fmt, ...);
void log_debug(u8 *tag, u8 *fmt, ...);
void log_printf(u8 *fmt, ...);
```

## 版本历史

| 日期 | 版本 | 说明 |
|------|------|------|
| 2026-04-22 | v1.0 | 初版实现 |
