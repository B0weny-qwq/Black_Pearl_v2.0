# App/Src 说明

这里放 App 层实现，是外包业务和竞赛逻辑的主要阅读入口。

- `app.c`：bring-up、主循环、AHRS/MAG/Heading、事件分发。
- `app_extension.c`：外包新增业务首选位置。
- `ship_protocol.c`：无线旧协议、配对、电量、GPS 回包、事件队列。
- `ship_control.c`：手动/巡航/GPS 电机输出控制。
- `autodrive.c`：返航、钓点、目标对准和 GPS 导航。
- `autodrive_config.c`：AutoDrive 配置保存/加载。
- `main.c`：只做平台初始化和 App 调度。

本目录可以调用 BoardDevices/Components/Services，但不能直接访问寄存器或裸引脚。
