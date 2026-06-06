# Graph Report - Black_Pearl_v2.0-main  (2026-06-06)

## Corpus Check
- 207 files · ~110,146 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 1621 nodes · 3748 edges · 157 communities (133 shown, 24 thin omitted)
- Extraction: 84% EXTRACTED · 16% INFERRED · 0% AMBIGUOUS · INFERRED: 590 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `4dad235f`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 2|Community 2]]
- [[_COMMUNITY_Community 3|Community 3]]
- [[_COMMUNITY_Community 4|Community 4]]
- [[_COMMUNITY_Community 5|Community 5]]
- [[_COMMUNITY_Community 6|Community 6]]
- [[_COMMUNITY_Community 7|Community 7]]
- [[_COMMUNITY_Community 8|Community 8]]
- [[_COMMUNITY_Community 9|Community 9]]
- [[_COMMUNITY_Community 10|Community 10]]
- [[_COMMUNITY_Community 11|Community 11]]
- [[_COMMUNITY_Community 12|Community 12]]
- [[_COMMUNITY_Community 13|Community 13]]
- [[_COMMUNITY_Community 14|Community 14]]
- [[_COMMUNITY_Community 15|Community 15]]
- [[_COMMUNITY_Community 16|Community 16]]
- [[_COMMUNITY_Community 17|Community 17]]
- [[_COMMUNITY_Community 18|Community 18]]
- [[_COMMUNITY_Community 19|Community 19]]
- [[_COMMUNITY_Community 20|Community 20]]
- [[_COMMUNITY_Community 21|Community 21]]
- [[_COMMUNITY_Community 22|Community 22]]
- [[_COMMUNITY_Community 23|Community 23]]
- [[_COMMUNITY_Community 24|Community 24]]
- [[_COMMUNITY_Community 25|Community 25]]
- [[_COMMUNITY_Community 27|Community 27]]
- [[_COMMUNITY_Community 28|Community 28]]
- [[_COMMUNITY_Community 29|Community 29]]
- [[_COMMUNITY_Community 30|Community 30]]
- [[_COMMUNITY_Community 31|Community 31]]
- [[_COMMUNITY_Community 32|Community 32]]
- [[_COMMUNITY_Community 33|Community 33]]
- [[_COMMUNITY_Community 34|Community 34]]
- [[_COMMUNITY_Community 35|Community 35]]
- [[_COMMUNITY_Community 36|Community 36]]
- [[_COMMUNITY_Community 37|Community 37]]
- [[_COMMUNITY_Community 38|Community 38]]
- [[_COMMUNITY_Community 39|Community 39]]
- [[_COMMUNITY_Community 40|Community 40]]
- [[_COMMUNITY_Community 41|Community 41]]
- [[_COMMUNITY_Community 42|Community 42]]
- [[_COMMUNITY_Community 43|Community 43]]
- [[_COMMUNITY_Community 45|Community 45]]
- [[_COMMUNITY_Community 46|Community 46]]
- [[_COMMUNITY_Community 47|Community 47]]
- [[_COMMUNITY_Community 48|Community 48]]
- [[_COMMUNITY_Community 50|Community 50]]
- [[_COMMUNITY_Community 51|Community 51]]
- [[_COMMUNITY_Community 52|Community 52]]
- [[_COMMUNITY_Community 54|Community 54]]
- [[_COMMUNITY_Community 55|Community 55]]
- [[_COMMUNITY_Community 56|Community 56]]
- [[_COMMUNITY_Community 57|Community 57]]
- [[_COMMUNITY_Community 58|Community 58]]
- [[_COMMUNITY_Community 59|Community 59]]
- [[_COMMUNITY_Community 60|Community 60]]
- [[_COMMUNITY_Community 61|Community 61]]
- [[_COMMUNITY_Community 62|Community 62]]
- [[_COMMUNITY_Community 64|Community 64]]
- [[_COMMUNITY_Community 65|Community 65]]
- [[_COMMUNITY_Community 66|Community 66]]
- [[_COMMUNITY_Community 67|Community 67]]
- [[_COMMUNITY_Community 68|Community 68]]
- [[_COMMUNITY_Community 69|Community 69]]
- [[_COMMUNITY_Community 70|Community 70]]
- [[_COMMUNITY_Community 71|Community 71]]
- [[_COMMUNITY_Community 72|Community 72]]
- [[_COMMUNITY_Community 73|Community 73]]
- [[_COMMUNITY_Community 74|Community 74]]
- [[_COMMUNITY_Community 75|Community 75]]
- [[_COMMUNITY_Community 77|Community 77]]
- [[_COMMUNITY_Community 78|Community 78]]
- [[_COMMUNITY_Community 79|Community 79]]
- [[_COMMUNITY_Community 80|Community 80]]
- [[_COMMUNITY_Community 82|Community 82]]
- [[_COMMUNITY_Community 83|Community 83]]
- [[_COMMUNITY_Community 84|Community 84]]
- [[_COMMUNITY_Community 85|Community 85]]
- [[_COMMUNITY_Community 86|Community 86]]
- [[_COMMUNITY_Community 87|Community 87]]
- [[_COMMUNITY_Community 88|Community 88]]
- [[_COMMUNITY_Community 94|Community 94]]
- [[_COMMUNITY_Community 95|Community 95]]
- [[_COMMUNITY_Community 96|Community 96]]
- [[_COMMUNITY_Community 98|Community 98]]
- [[_COMMUNITY_Community 99|Community 99]]
- [[_COMMUNITY_Community 100|Community 100]]
- [[_COMMUNITY_Community 101|Community 101]]
- [[_COMMUNITY_Community 102|Community 102]]
- [[_COMMUNITY_Community 103|Community 103]]
- [[_COMMUNITY_Community 104|Community 104]]

## God Nodes (most connected - your core abstractions)
1. `lt8920_t` - 37 edges
2. `u8` - 37 edges
3. `app_ahrs_poll()` - 29 edges
4. `int8` - 29 edges
5. `Black Pearl 串口日志上位机使用说明` - 29 edges
6. `ShipControl_ApplyYawHoldTargetEx()` - 28 edges
7. `AutoDrive_Poll()` - 27 edges
8. `qmi8658_t` - 27 edges
9. `NorthCalib_Poll()` - 25 edges
10. `u8` - 24 edges

## Surprising Connections (you probably didn't know these)
- `app_loop()` --calls--> `board_gps_poll()`  [INFERRED]
  App/Src/app.c → BoardDevices/Src/board_gps.c
- `app_loop()` --calls--> `board_motor_service()`  [INFERRED]
  App/Src/app.c → BoardDevices/Src/board_motor.c
- `app_loop()` --calls--> `platform_scheduler_get_tick_ms()`  [INFERRED]
  App/Src/app.c → Platform/Src/platform_scheduler.c
- `app_get_attitude_state()` --calls--> `AHRS_GetState()`  [INFERRED]
  App/Src/app.c → Components/Src/AHRS.c
- `app_log_mag_read_fail()` --calls--> `board_mag_get_diag()`  [INFERRED]
  App/Src/app_ahrs_core.c → BoardDevices/Src/board_mag.c

## Import Cycles
- None detected.

## Communities (157 total, 24 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.05
Nodes (89): u8, AutoDrive_PointRaw_t, AutoDrive_ReturnConfig_t, u8, AutoDrive_PointRaw_t, int16, int32, u16 (+81 more)

### Community 1 - "Community 1"
Cohesion: 0.06
Nodes (88): board_imu_diag_t, board_imu_sample_t, int8, qmi8658_diag_regs_t, u16, u8, board_mag_sample_t, int8 (+80 more)

### Community 2 - "Community 2"
Cohesion: 0.08
Nodes (85): board_wireless_rx_debug_t, board_wireless_state_t, int8, u16, u32, u8, int8, u16 (+77 more)

### Community 3 - "Community 3"
Cohesion: 0.06
Nodes (70): AHRS_State_t, board_mag_sample_t, int8, u32, u8, AHRS_State_t, mag_compass_state_t, u8 (+62 more)

### Community 4 - "Community 4"
Cohesion: 0.07
Nodes (71): u8, int16, u16, u32, u8, int16, int32, u16 (+63 more)

### Community 5 - "Community 5"
Cohesion: 0.12
Nodes (52): u8, ef_iic_config_t, I2C_InitTypeDef, ef_iic_diag_t, int8, u16, u8, ef_iic_abort_nack() (+44 more)

### Community 6 - "Community 6"
Cohesion: 0.12
Nodes (49): int8, u8, int32, int8, u16, u32, u8, gnss_nmea_state_t (+41 more)

### Community 7 - "Community 7"
Cohesion: 0.19
Nodes (48): board_lt8920_debug_t, int8, u16, u32, u8, lt8920_bus_t, lt8920_t, board_lt8920_get_debug() (+40 more)

### Community 8 - "Community 8"
Cohesion: 0.13
Nodes (45): AHRS_State_t, int16, u16, board_gps_state_t, int16, int32, u16, u32 (+37 more)

### Community 9 - "Community 9"
Cohesion: 0.14
Nodes (43): AHRS_Lpf3_t, int16, int32, int8, u16, u32, u8, AHRS_Abs16() (+35 more)

### Community 10 - "Community 10"
Cohesion: 0.08
Nodes (38): ship_protocol_event_snapshot_t, u32, ship_protocol_event_snapshot_t, ship_protocol_event_type_t, int8, u8, u8, ef_spi_config_t (+30 more)

### Community 11 - "Community 11"
Cohesion: 0.13
Nodes (37): AutoDrive_ReturnConfig_t, u16, u8, int8, u16, u32, u8, u16 (+29 more)

### Community 12 - "Community 12"
Cohesion: 0.11
Nodes (29): u8, Timer0_ISR_Handler(), int32, u32, u8, app_init(), app_extension_init(), board_console_init() (+21 more)

### Community 13 - "Community 13"
Cohesion: 0.13
Nodes (35): board_motor_id_t, board_motor_pwm_snapshot_t, int16, int8, u16, u8, u8, HSPWMx_InitDefine (+27 more)

### Community 14 - "Community 14"
Cohesion: 0.22
Nodes (33): board_mag_diag_t, int16, int8, u16, u8, qmc6309_bus_t, qmc6309_regs_t, qmc6309_t (+25 more)

### Community 15 - "Community 15"
Cohesion: 0.12
Nodes (32): u8, Exti_config(), NVIC_ADC_Init(), NVIC_CAN_Init(), NVIC_DMA_ADC_Init(), NVIC_DMA_I2CR_Init(), NVIC_DMA_I2CT_Init(), NVIC_DMA_LCM_Init() (+24 more)

### Community 16 - "Community 16"
Cohesion: 0.15
Nodes (23): int32, u16, u32, u8, AutoDrive_PointRaw_t, int8, u32, u8 (+15 more)

### Community 17 - "Community 17"
Cohesion: 0.24
Nodes (23): u16, u8, LIN_ISR_Handler(), LIN_InitTypeDef, GetLinError(), LIN_Inilize(), LinReadFrame(), LinReadMsg() (+15 more)

### Community 18 - "Community 18"
Cohesion: 0.16
Nodes (22): ADC_InitTypeDef, board_power_sample_t, int8, u16, u32, u8, u16, u8 (+14 more)

### Community 19 - "Community 19"
Cohesion: 0.18
Nodes (22): u32, u8, int16, ship_protocol_event_snapshot_t, ship_protocol_event_type_t, u8, ship_protocol_event_state_t, ship_protocol_key_action_t (+14 more)

### Community 20 - "Community 20"
Cohesion: 0.09
Nodes (21): 10.1 上电后先看启动, 10.2 看无线配对, 10.3 看遥控在线, 10.4 看控制模式, 10.5 看电机输出, 10.6 看姿态与航向, 10.7 看 GPS, 10. 现场推荐排查顺序 (+13 more)

### Community 21 - "Community 21"
Cohesion: 0.27
Nodes (21): ArgumentParser, Namespace, build_arg_parser(), build_frame(), build_point_payload(), decode_point_from_gps_report(), format_c_array(), format_hex() (+13 more)

### Community 22 - "Community 22"
Cohesion: 0.10
Nodes (19): 2026-05 GNSS and 0x12 full-field alignment record, 2026-05 power/storage/control alignment record, C251 memory layout note, Include 边界检查, v1.1 AHRS/MAG migration data, 基本信息, 工程数据, 已实现底层 (+11 more)

### Community 23 - "Community 23"
Cohesion: 0.29
Nodes (19): bool, block(), emit_header(), field(), gpio_pin(), i2c_pin_group_macro(), i2c_route_id(), i2c_speed_macro() (+11 more)

### Community 24 - "Community 24"
Cohesion: 0.11
Nodes (18): 7.1 联调摘要区, `AHRS Flags`, `AHRS R/P/Y`, `HDG Debug`, `Yaw 状态`, `动作判定`, `地磁数据`, `地磁数据` (+10 more)

### Community 25 - "Community 25"
Cohesion: 0.12
Nodes (16): 2026-05 state-machine wireless/control note, Black Pearl v2.0, SPI-PS 对等通信, v1.1 AHRS/MAG port note, 上位机卡片到底层链路, 传感器总线、IMU/地磁/RF bring-up 与滤波组件, 保留的扩展 API, 分层规则 (+8 more)

### Community 27 - "Community 27"
Cohesion: 0.13
Nodes (14): 2026-05 protocol, control and storage layering note, C251 memory layout and resource discipline, SPI-PS 对等通信, UART1 控制台、日志与 GPS, v1.1 AHRS/MAG solver port, 上位机日志链路, 传感器总线、QMI8658/QMC6309/LT8920 与滤波, 依赖方向 (+6 more)

### Community 28 - "Community 28"
Cohesion: 0.13
Nodes (14): BoardDevices API First, Chinese Doxygen Rules, ChipDrivers Boundary, CMake Boundaries, Competition Layout, Default Mindset, EmbedForge Project Standards, Hardware Description And Generated Config (+6 more)

### Community 29 - "Community 29"
Cohesion: 0.16
Nodes (12): u8, I2C_config(), platform_clock_config(), platform_init(), Switch_config(), Timer_config(), UART_config(), NVIC_I2C_Init() (+4 more)

### Community 30 - "Community 30"
Cohesion: 0.14
Nodes (13): DMA_ADC_InitTypeDef, DMA_I2C_InitTypeDef, DMA_LCM_InitTypeDef, DMA_M2M_InitTypeDef, DMA_SPI_InitTypeDef, DMA_UART_InitTypeDef, u8, DMA_ADC_Inilize() (+5 more)

### Community 31 - "Community 31"
Cohesion: 0.35
Nodes (13): COMx_InitDefine, u8, ef_uart_write_byte(), PrintString1(), PrintString2(), PrintString3(), PrintString4(), putchar() (+5 more)

### Community 32 - "Community 32"
Cohesion: 0.15
Nodes (12): Black Pearl v2.0 持久目标 README, 关键改动清单, 完成判定, 对外接口, 必须完成, 暂不做, 每轮调度优先级, 测试计划 (+4 more)

### Community 33 - "Community 33"
Cohesion: 0.29
Nodes (11): CAN_DataDef, CAN_InitTypeDef, u8, CAN1_ISR_Handler(), CAN2_ISR_Handler(), CAN_Inilize(), CanReadFifo(), CanReadMsg() (+3 more)

### Community 34 - "Community 34"
Cohesion: 0.42
Nodes (12): u16, u8, UASRT_LIN_Configuration(), UsartLinBaudrate(), UsartLinSendByte(), UsartLinSendChecksum(), UsartLinSendData(), UsartLinSendFrame() (+4 more)

### Community 35 - "Community 35"
Cohesion: 0.17
Nodes (11): API, logger 日志服务, parameter_store 参数服务, Services 服务层, 初始化顺序, 当前事件/日志对齐, 当前联调日志关键词, 文件 (+3 more)

### Community 36 - "Community 36"
Cohesion: 0.38
Nodes (11): int8, u16, ship_protocol_apply_work_rx(), ship_protocol_apply_work_sync_idle(), ship_protocol_arm_pair_rsp_window(), ship_protocol_check_timeouts(), ship_protocol_step_pair_send(), ship_protocol_step_work_rx() (+3 more)

### Community 37 - "Community 37"
Cohesion: 0.29
Nodes (11): ship_protocol_power_sample_t, ship_protocol_power_sample_t, u8, ship_protocol_event_queue_reset(), ship_protocol_fill_power_event(), ship_protocol_log_power_sample(), ship_protocol_low_power_check(), ship_protocol_power_init() (+3 more)

### Community 38 - "Community 38"
Cohesion: 0.48
Nodes (11): u8, I2C_Check_ACK(), I2C_Delay(), I2C_ReadAbyte(), I2C_Start(), I2C_Stop(), I2C_WriteAbyte(), S_ACK() (+3 more)

### Community 39 - "Community 39"
Cohesion: 0.24
Nodes (11): ef_uart_config_t, ef_uart_rx_view_t, u8, ef_uart_get_rx_view(), ef_uart_init(), ef_uart_read_rx_byte(), ef_uart_to_stc_port(), ef_uart_write() (+3 more)

### Community 40 - "Community 40"
Cohesion: 0.18
Nodes (10): Application Schedule, AutoDrive, Black Pearl v2.0 State Machines, Command Mapping, Event Priority, Layer Checks, NorthCalib, ShipControl (+2 more)

### Community 41 - "Community 41"
Cohesion: 0.18
Nodes (11): 7.2 GPS 摘要区, `GPS 源数据`, `卫星数`, `定位有效`, `最近 0x12`, `目标点`, `经度 / 纬度`, `航向角` (+3 more)

### Community 42 - "Community 42"
Cohesion: 0.45
Nodes (10): u8, ship_protocol_point_t, NorthCalib_IsBusy(), ship_protocol_log_point(), ship_protocol_parse_point_payload(), ship_protocol_fish_result_name(), ship_protocol_handle_fish_point(), ship_protocol_handle_return_home() (+2 more)

### Community 43 - "Community 43"
Cohesion: 0.33
Nodes (8): build_server(), main(), pick_port(), QuietHandler, Serve files quietly without per-request console spam., ThreadedTCPServer, int, str

### Community 45 - "Community 45"
Cohesion: 0.22
Nodes (8): 1. Build a `0x15` save-point packet directly from a `0x12` GPS report, 2. Build a packet from manual point fields, Important Notes, Output, Payload Meaning, Quick Start, Recommended Field Test Flow, Ship Packet Builder

### Community 46 - "Community 46"
Cohesion: 0.36
Nodes (7): u8, int8, u8, ship_protocol_mark_proto_activity(), ship_protocol_consume_byte(), ship_protocol_parse_frame(), ship_protocol_poll_rx_frames()

### Community 47 - "Community 47"
Cohesion: 0.39
Nodes (7): int8, ship_protocol_event_snapshot_t, u8, ship_protocol_get_event_snapshot(), ship_protocol_is_paired(), ship_protocol_publish_spi_ps_frame(), ship_protocol_take_event()

### Community 48 - "Community 48"
Cohesion: 0.29
Nodes (6): 2026-05 v1.1 对齐补充, App 层说明, 上位机卡片来源, 外包改动入口, 目录, 禁止事项

### Community 50 - "Community 50"
Cohesion: 0.53
Nodes (5): u8, HIRCClkConfig(), HSPllClkConfig(), IRC32KClkConfig(), XOSCClkConfig()

### Community 51 - "Community 51"
Cohesion: 0.40
Nodes (4): BoardDevices 层说明, 新增硬件流程, 现有设备, 目录

### Community 52 - "Community 52"
Cohesion: 0.40
Nodes (4): ChipDrivers 层说明, 当前内容, 目录, 边界

### Community 54 - "Community 54"
Cohesion: 0.40
Nodes (5): 11.1 “已配对”但“遥控离线”, 11.2 遥控输入有变化，但实际输出不变, 11.3 GPS 有卫星，但定位无效, 11.4 地磁漂，但船还能控, 11. 常见现象解释

### Community 55 - "Community 55"
Cohesion: 0.40
Nodes (5): 9.1 系统类, 9.2 姿态和航向类, 9.3 遥控与动作类, 9.4 GPS / 协议类, 9. 当前兼容日志

### Community 56 - "Community 56"
Cohesion: 0.60
Nodes (4): u8, BitTime(), PrintString(), TxSend()

### Community 57 - "Community 57"
Cohesion: 0.40
Nodes (4): CMP_InitDefine, CMP_config(), CMP_Inilize(), NVIC_CMP_Init()

### Community 58 - "Community 58"
Cohesion: 0.50
Nodes (3): Components 层说明, 对接规则, 当前组件

### Community 59 - "Community 59"
Cohesion: 0.50
Nodes (3): doc 文档说明, 当前文档, 更新规则

### Community 60 - "Community 60"
Cohesion: 0.50
Nodes (3): Drivers 层说明, 使用规则, 目录

### Community 62 - "Community 62"
Cohesion: 0.50
Nodes (3): McuAbstraction 层说明, 对接规则, 当前接口

### Community 64 - "Community 64"
Cohesion: 0.50
Nodes (3): Platform 层说明, 使用规则, 目录

### Community 65 - "Community 65"
Cohesion: 0.50
Nodes (3): RVMDK 工程说明, 修改规则, 关键文件

### Community 66 - "Community 66"
Cohesion: 0.50
Nodes (4): 3.1 现场推荐, 3.2 PowerShell, 3.3 启动后会发生什么, 3. 推荐打开方式

### Community 67 - "Community 67"
Cohesion: 0.50
Nodes (4): 5.1 浏览器, 5.2 Python, 5.3 串口线, 5. 使用前准备

### Community 68 - "Community 68"
Cohesion: 0.50
Nodes (3): u8, GPIO_InitTypeDef, GPIO_Inilize()

### Community 69 - "Community 69"
Cohesion: 0.50
Nodes (3): u8, RTC_InitTypeDef, RTC_Inilize()

### Community 70 - "Community 70"
Cohesion: 0.50
Nodes (3): u8, EXTI_InitTypeDef, Ext_Inilize()

### Community 71 - "Community 71"
Cohesion: 0.50
Nodes (3): Startup 层说明, 使用规则, 当前文件

### Community 72 - "Community 72"
Cohesion: 0.50
Nodes (3): tools 工具说明, 子目录, 对接规则

### Community 77 - "Community 77"
Cohesion: 0.67
Nodes (3): 8.1 固定节拍刷新, 8.2 即时刷新, 8. 页面刷新策略

## Knowledge Gaps
- **317 isolated node(s):** `AHRS_State_t`, `u8`, `int16`, `int8`, `u8` (+312 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **24 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `app_bring_up_devices()` connect `Community 1` to `Community 2`, `Community 3`, `Community 37`, `Community 6`, `Community 8`, `Community 10`, `Community 12`, `Community 13`, `Community 18`?**
  _High betweenness centrality (0.123) - this node is a cross-community bridge._
- **Why does `app_ahrs_poll()` connect `Community 3` to `Community 1`, `Community 2`, `Community 4`, `Community 8`, `Community 9`?**
  _High betweenness centrality (0.095) - this node is a cross-community bridge._
- **Why does `app_loop()` connect `Community 2` to `Community 1`, `Community 3`, `Community 4`, `Community 36`, `Community 6`, `Community 8`, `Community 10`, `Community 12`, `Community 13`?**
  _High betweenness centrality (0.094) - this node is a cross-community bridge._
- **Are the 27 inferred relationships involving `app_ahrs_poll()` (e.g. with `AHRS_GetState()` and `AHRS_UpdateRaw6Axis()`) actually correct?**
  _`app_ahrs_poll()` has 27 INFERRED edges - model-reasoned connections that need verification._
- **What connects `AHRS_State_t`, `u8`, `int16` to the rest of the system?**
  _318 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Community 0` be split into smaller, more focused modules?**
  _Cohesion score 0.054982817869415807 - nodes in this community are weakly interconnected._
- **Should `Community 1` be split into smaller, more focused modules?**
  _Cohesion score 0.06291466483642187 - nodes in this community are weakly interconnected._