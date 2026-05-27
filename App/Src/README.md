# App/Src 说明

这里放 App 层实现，是外包业务和竞赛逻辑的主要阅读入口。

- `app.c`：App 薄入口，调用拆分后的 bring-up、主循环和事件轮询。
- `app_bringup.c`：按 v1.1 迁移顺序初始化板级设备和 App 状态机。
- `app_ahrs_core.c` / `app_ahrs_poll.c`：IMU/MAG/AHRS/Heading 融合与日志。
- `app_mag_event.c`：独立地磁观察、SPI-PS 事件桥和协议事件分发。
- `app_state.c`：应用层 AHRS/航向快照 getter。
- `app_extension.c`：外包新增业务首选位置。
- `ship_protocol.c` / `ship_protocol_state.c`：无线旧协议运行态和公共工具。
- `ship_protocol_link.c` / `ship_protocol_parse.c`：RF 收包、旧帧解析和配对/工作信道。
- `ship_protocol_cmd.c`：`0x0F/0x11/0x13/0x14/0x15` 命令分发，E 键巡航入口在这里。
- `ship_protocol_tx.c` / `ship_protocol_power.c` / `ship_protocol_event.c` / `ship_protocol_points.c`：0x12/0x16 回包、电量、事件队列和点位日志。
- `ship_control.c` / `ship_control_core.c`：控制状态、日志和电机写入唯一入口。
- `ship_control_manual.c`：手动开环和手动 yaw hold 入口。
- `ship_control_yaw.c`：航向保持、E 键巡航、GPS 对齐/导航 yaw 控制；阻尼使用 v1.1 的 `gyro_z_dps100`。
- `autodrive.c`：AutoDrive 运行态、初始化、停止和链路保活。
- `autodrive_cmd.c` / `autodrive_points.c`：`0x13/0x14/0x15` 命令入口、旧坐标和钓点表。
- `autodrive_nav.c` / `autodrive_control.c` / `autodrive_poll.c`：旧整数距离/方位算法、目标航向控制和 START/ALIGN/RUN 状态机。
- `autodrive_gps.c` / `autodrive_diag.c`：GPS ready/current 快照和 AutoDrive 诊断快照。
- `autodrive_config.c`：AutoDrive 配置保存/加载。
- `main.c`：只做平台初始化和 App 调度。

本目录可以调用 BoardDevices/Components/Services，但不能直接访问寄存器或裸引脚。
