# App/Inc 说明

这里放 App 层公开头文件。新增 App 级 API 时先写中文 Doxygen，说明调用方、时序、阻塞行为和返回值。

- `app.h`：应用初始化、主循环和姿态/航向快照。
- `app_config.h`：运行档位和调参宏。
- `app_extension.h`：外包扩展回调入口。
- `ship_protocol.h`：旧无线协议状态、事件和 payload 结构。
- `ship_control.h`：船体控制状态机接口。
- `autodrive*.h`：GPS 自动驾驶和配置接口；`autodrive_internal.h` 只给 `App/Src/autodrive*.c` 内部拆分使用。
- `ship_control_internal.h` / `ship_protocol_internal.h`：只给同名 App 状态机拆分文件使用，外部不要 include。

不要在本目录 include STC 官方驱动头。
