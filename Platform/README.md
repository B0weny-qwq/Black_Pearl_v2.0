# Platform 层说明

`Platform/` 负责芯片启动后的基础平台配置：时钟、GPIO 复用、全局初始化、1 ms 调度 tick 和公共类型。

## 目录

- `Inc/config.h`、`Inc/stc32g.h`：STC 官方/平台配置入口，只允许底层使用。
- `Inc/type_def.h`：工程基础类型定义。
- `Inc/platform.h`：平台初始化入口。
- `Inc/platform_scheduler.h`：调度 tick 和毫秒时间接口。
- `Src/platform.c`：时钟、GPIO 模式、外设基础初始化。
- `Src/platform_scheduler.c`：Timer0 1 kHz tick 和 `platform_scheduler_get_tick_ms()`。

## 使用规则

- App 可以调用 `platform_scheduler_get_tick_ms()` 做非阻塞定时。
- 具体硬件设备初始化仍应放在 BoardDevices。
- 不在本层写业务状态机。
