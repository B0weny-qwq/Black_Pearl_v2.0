# 变更日志

## Unreleased

- 2026-06-06 AutoDrive persistence narrowed to return origin:
  - Stopped treating `0x14` fishing points as a local point table. The boat now keeps only the current runtime target from the latest remote command and duplicate debounce state.
  - Narrowed AutoDrive flash storage to the 10-byte return origin. `0x15` switch-only packets update runtime state without erasing/writing flash.
  - Renamed the compact `0x14` short log field from `save=` to `rt=`. The ship log viewer accepts both old `save=` and new `rt=` logs.

- 2026-05-28 App decoupling and v1.1 evidence pass:
  - Split App-layer large files so each `App/*.c` and `App/*.h` is at most 300 lines by strict line count.
  - Split `app.c`, `ship_control.c`, `ship_protocol.c`, and `autodrive.c` into focused App modules and added the new files to the Keil project.
  - Added Chinese Doxygen/internal comments for the new App headers and concise Chinese comments in App `.c` files.
  - Documented the v1.1 yaw damping evidence: old `ShipControl_ApplyYawHoldDamping()` uses `MainLoop_GetGyroZDps100()`, which is sourced from `att->gyro_z_dps100`; current `ship_control_yaw.c` keeps `gyro_z_dps100`.
  - Updated handoff docs to point outsourced logic to the correct split files while keeping `app_extension.c` as the stable key/event/LED insertion API.
  - Kept the duplicate root `ship_log_viewer/` removed; the canonical viewer documentation is `tools/ship_log_viewer/README.md`.
  - Verified Keil C251 command-line build after the split: `Program Size: data=11.7 edata+hdata=3901 xdata=380 const=3092 code=60973`, `0 Error(s), 0 Warning(s)`.

- 2026-05-27 host-viewer telemetry simplification:
  - Removed GX/GY/GZ from App-side AHRS/IMU logs and from host-viewer parsing; `[AHRS]` now reports `rpy/flg`, and `[IMU] raw` reports accel only.
  - Removed the host-viewer gyro card. The self-stabilize card now follows E-key cruise heading hold: `ON` after E + throttle `> 60`, `OFF` after E-toggle or reverse throttle `< -40`.
  - Kept cruise/action cards stable by treating `CTRL out` as authoritative and by preventing auxiliary manual/gate/key logs from clearing motor output before a confirmed stop output arrives.

- 2026-05-28 cruise self-stabilize display correction:
  - Corrected the host-viewer self-stabilize card from a fixed `OFF` placeholder to the actual E-key cruise heading-hold state.
  - Adjusted cruise entry to require throttle `> 60` and reverse-throttle exit to require `< -40`, matching the requested field behavior.

- 2026-05-27 cruise and power ADC field fix:
  - Fixed E-key cruise speed retention in `ShipControl_Tick()`: the requested cruise speed stays in `throttle_speed`, while `base_speed` remains the ramped motor-frame output, so cruise no longer collapses to the ramp minimum after the first frame.
  - Cleared `ADC_START`/`ADC_FLAG` around polled ADC conversions and added a `board_power` recovery retry that reconfigures P0.0/ADC_CH8 after invalid `4096` samples.
  - Aligned the host viewer cruise-speed constant with firmware `SHIP_CRUISE_KEY_SPEED = 760`.
  - Updated README notes for diagnosing `adc not-ready rc=-3` and for interpreting `CTRL out th=760 base=520..760` during cruise ramp-up.

- 2026-05-27 outsourcing handoff and host-viewer traceability:
  - Added `App/Inc/app_extension.h` and `App/Src/app_extension.c` as the stable App-layer extension point for future outsourced key actions, LED blinking and non-blocking event-driven additions.
  - Wired `app_extension_init()`, `app_extension_poll()` and `app_extension_on_ship_event()` into the App bring-up, main loop and protocol-event drain path.
  - Re-enabled `SHIP_CONTROL_LOG_ENABLE` and changed the control output log to the viewer-compatible `CTRL out m/mo/th/base/st/df/l/r` format.
  - Changed control mode logs to the explicit `event=mode old=... new=... reason=... yaw=... tgt=...` format so the host viewer can label v2 modes correctly.
  - Added low-rate `[MAG] raw=... norm=... yaw=... self=...` output and allowed `[AHRS]`/`[HDG]` logs before heading seed completes, so AHRS, gyro, flags, MAG and HDG cards no longer stay blank during magnetometer settling.
  - Forced an initial power-sample log after protocol initialization so the power card has a startup data source.
  - Updated `tools/ship_log_viewer` mode mapping for the current v2 `ShipControl_Mode_t` enum.
  - Added README files for the main source folders and their `Inc`/`Src` subfolders, plus tool/doc/startup/project directories, to make the outsourcing boundary visible from every code folder.
  - Updated root README, `doc/total.md`, `doc/data.md` and `tools/ship_log_viewer/README.md` with card-to-firmware-to-bottom-layer traceability and the integration workflow.

- 2026-05-27 protocol cadence and v1.1 compatibility follow-up:
  - Added `App/Inc/app_config.h` as the centralized v1.1 runtime profile for manual-control, yaw-hold, pairing cadence and power-log throttling values.
  - Aligned `ship_control` tuning with the current v1.1 profile: RC axis range `60`, yaw diff limit `320 permille`, derate thresholds `1000/2000 cd`, gyro damping `4096`, and PID `384/0/96`.
  - Restored the v1.1 manual yaw-hold entry gate: forward throttle, low left/right motor diff under `20%` of current input, and 2 stable frames before locking the heading target.
  - Restored the v1.1 heading-static gate by requiring stopped ShipControl mode and zero motor speeds before magnetometer heading can seed/correct the fused heading.
  - Defaulted `SHIP_CONTROL_LOG_ENABLE` to `0` and shortened local format strings to keep C251 `HCONST` usage within the Keil link range without changing motor-control behavior.
  - Restored normal ADC log throttling to the v1.1 profile value `SHIP_POWER_LOG_PERIOD_MS=10000 ms`.
  - Corrected the LT8920 SPI pin documentation to `P3.5=CS`, `P3.4=MOSI`, `P3.3=MISO`, `P3.2=SCLK`.
  - Gated `ship_protocol_run_scheduler()` with the real `platform_scheduler_get_tick_ms()` 10 ms cadence while still draining wireless RX every call.
  - Restored v1.1 pairing behavior: response-window timeout retries the seed burst, and a 60 s fallback forces work-RX mode if no pair response arrives.
  - Restored old work-channel compatibility for `0x12` GPS/status and `0x16` AutoDrive diagnostics: runtime sends them on `rf_channel[0]`; derived `work_tx=77` remains a compatibility parameter/log value.
  - Changed `0x14` unknown fish-point handling so a newly stored point immediately attempts goto when GPS/distance allow; quick duplicate frames are still suppressed.
  - Split `0x14` diagnostics into save and navigation results, matching the v1.1 `save=... nav=... idx=...` observability.
  - Updated `Switch_config()` to use generated board-resource mux macros for console UART, GPS UART, sensor I2C and LT8920 SPI.
  - Made `tools/ship_log_viewer` the canonical viewer, added parsing for compact current firmware logs, and removed the duplicate root-level `ship_log_viewer/` copy.

- 2026-05-27 runtime bring-up diagnostics and v1.1 sensor alignment:
  - Aligned `QMI8658_ReadRawSample()` with the v1.1 hot path: six-axis samples are now read as 12 bytes from `AX_L`, while temperature is read separately as optional diagnostic data.
  - Added a board-level first-valid-sample gate in `board_imu_init()`. A QMI8658 that only accepts register writes but does not produce accel/gyro samples is now reported as an IMU init/data failure instead of a misleading `init ok`.
  - Added `board_imu_service()` and data-ready checks before App-side one-shot/AHRS reads, and extended the AHRS warning with the actual `board_imu_read()` return code.
  - Matched the ADC bring-up path with the v1.1 style by disabling the ADC interrupt through `NVIC_ADC_Init(DISABLE, Priority_0)` after ADC power-up.
  - Split the protocol power cache into "sample not attempted yet" and "sample attempted but not ready" states. Startup now logs `adc pending` before the first down-sampled ADC read instead of reporting `raw=0` as a false failure, and low-power latching only counts valid samples.
  - Corrected the documented `PAIR_REQ(0x10)` example frame XOR to `AA 06 10 65 65 A0 65 D3 BB`, matching the runtime log and the `len/cmd/payload` XOR calculation.
  - Moved the LT8920 default register table to C251 `CODE` constants through `EF_CODE_CONST`, and shortened non-protocol diagnostic strings so HCONST fits again.
  - Added default short-log gates for C251 (`SHIP_PROTOCOL_VERBOSE_LOG_ENABLE=0`, `SHIP_APP_BRINGUP_VERBOSE_LOG_ENABLE=0`) while keeping the upper-computer card-critical logs enabled.
  - Verified Keil C251 command-line build after the runtime fixes: `Program Size: data=11.7 edata+hdata=3890 xdata=380 const=2919 code=60331`, `0 Error(s), 0 Warning(s)`.
  - Documented the burn-in behavior and upper-computer/remote compatibility surface for the current firmware.

- 2026-05-27 C251 build and memory-layout fix:
  - Fixed the C251 qualifier order for `Components/Src/AHRS.c` so the AHRS context is declared as `AHRS_Context_t EF_LARGE_DATA`, which C251 places in XDATA.
  - Moved the 4 x 60-byte RF receive payload queue in `BoardDevices/Src/board_wireless.c` to XDATA; the queue remains BoardDevices-private and App still receives copied payloads through `board_wireless_receive()`.
  - Kept Components pure: AHRS/HeadingEstimator remain free of board, STC, GPIO, I2C and logging dependencies.
  - Added Chinese Doxygen comments for AHRS and HeadingEstimator public APIs, including ownership, timing units, side effects and return meanings.
  - Completed Chinese Doxygen coverage for the touched App, BoardDevices, ChipDrivers and MCU abstraction public headers, including `app`, `autodrive`, `ship_control`, `board_gps`, `board_lt8920`, `board_wireless`, `gnss_nmea` and `ef_iic`.
  - Documented the C251 EDATA/XDATA layout and verification evidence in `README.md`, `doc/total.md`, `doc/data.md`, and `embedforge.yaml`.
  - Verified Keil C251 command-line build: `0 Error(s), 0 Warning(s)`.
  - Verified architecture boundary scan: App/Components/Services still do not include vendor/platform headers or old hardware APIs directly.

- 2026-05-24 protocol event follow-up:
  - Added B/C/D semantic key-action events in `ship_protocol`: B maps to `SHIP_PROTOCOL_KEY_ACTION_B_NOOP`, C maps to `SHIP_PROTOCOL_KEY_ACTION_C_NOOP`, and D maps to `SHIP_PROTOCOL_KEY_ACTION_D_NOOP`.
  - Kept A on the existing A-light `KEY_EDGE` log path only; no A `KEY_ACTION`, no `board_light`, and no GPIO/light hardware drive were added.
  - Added protocol observation events for power sampling: `SHIP_PROTOCOL_EVENT_POWER_SAMPLE`, `SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED`, and `SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED`.
  - Aligned low-power return gating to `power_level == 0`, `> 600` scheduler ticks, `AutoDrive_GetMode() == AUTO_DRIVE_CLOSE`, and `ShipControl_GetManualAccelerator() < 10` before latching return-home.
  - Extended `ship_protocol_event_snapshot_t` with `key_action`, `power`, and `spi_ps` fields so subscribers can observe key semantics, power sample values, and SPI-PS frame metadata.
  - Replaced the previous single pending-event slot with an 8-entry ring queue; `ship_protocol_take_event()` now drains events FIFO while `ship_protocol_get_event_snapshot()` remains a latest-snapshot view.
  - Added `app_ship_event_poll()` so the main loop actually consumes queued protocol events and logs key/action/point/power-latch/SPI-PS/error observations.
  - Added an App-level SPI-PS event bridge: `app_loop()` polls `board_spi_ps_service()`, reads completed RX frames, and publishes `SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX` when SPI-PS is enabled.
  - Added a generated SPI-PS resource guard, `EF_BOARD_SPI_PS_SHARES_LT8920_SPI`, so future SPI-PS enablement cannot silently reconfigure the STC SPI peripheral used by LT8920.
  - Updated docs/log notes to reflect that B/C/D are event-only no-op semantics, A light hardware is still pending pin confirmation, and `bat_mv` remains an uncalibrated `1:1` placeholder through `BOARD_POWER_BAT_SCALE_NUM/DEN`.

- 2026-05-24 state-machine wireless/control/autodrive port:
  - Added BoardDevices `board_power` and `board_storage`, so App no longer includes STC ADC/GPIO/EEPROM drivers directly.
  - Added `Services/parameter_store` for compact AutoDrive config persistence through `board_storage`.
  - Added `App/ship_control` as the single motor-output owner for manual control, E-key cruise, GPS align/nav yaw hold, failsafe stop and unified `CTRL out` logs.
  - Added `App/autodrive` and `App/autodrive_config` for return-home, goto-point, align/run/arrive states, RAM fish-point table, duplicate suppression, persistent return config and debug snapshots.
  - Updated `ship_protocol` from event-only parsing to explicit state-machine dispatch:
    - wireless link state includes `BOOT_WAIT -> PAIR_SEND -> PAIR_WAIT_RSP -> WORK_RX`
    - parser state includes `WAIT_HEAD -> READ_LEN -> READ_BODY -> DISPATCH`
    - `0x11` now drives `ShipControl` after boot/heading guard and handles E-key cruise entry/exit
    - `0x13/0x14/0x15` call AutoDrive real entries
    - `0x12 payload[13]` reports `board_power` level and `payload[14]` reports `AutoDrive_InActive()`
    - `0x16` now sends the 36-byte AutoDrive diagnostic payload
  - Updated `RVMDK/STC32G-LIB.uvproj` and `embedforge.yaml` for the new BoardDevices, Services and App modules.
  - Updated `README.md`, `doc/total.md`, `doc/data.md`, and `Services/README.md` to remove old event-only/stub wording and document protocol/log verification.
  - Verified architecture boundary scan for `App/` and `Components`: no direct `STC32G_*`, ADC, GPIO, PWM or EEPROM access.
  - Verified Keil command-line build: `0 Error(s), 0 Warning(s)`.

- 2026-05 GNSS/0x12/ADC old-version alignment update:
  - Moved `board_gps_init()` into the startup bring-up path and added explicit init return logging: `GPS init ok ready=%u` / `GPS init fail rc=%d`.
  - Kept the current GNSS parser chain (`board_gps` -> `gnss_nmea`) while aligning the exported data used by the old upper layer, including `legacy_*`, `lat/lon_deg1e7`, `course_deg_x100`, and `satellites_used_gsa`.
  - Updated `App/Src/ship_protocol_tx.c` `0x12` payload build logic to match old handheld expectations:
    - `payload[0]`: satellite count, prefer `satellites_used_gsa`, fallback `satellites_used`, clamp to 24.
    - `payload[1..2]`: heading degrees from `course_deg_x100 / 100`, big-endian.
    - `payload[4..7]` and `payload[9..12]`: prefer `legacy_lon1/lon2/lat1/lat2`, fallback to runtime conversion from `deg1e7` into old NMEA split fields.
    - `payload[3]` and `payload[8]`: keep legacy fixed marker bytes for remote compatibility.
    - Historical note: the follow-up above corrected the TX channel wording and runtime behavior to the old work RX channel path `rf_channel[0]`.
  - Restored the old power-sampling/reporting chain inside `ship_protocol` for the current board hardware:
    - ADC sample pin is the current board `P0.0 / ADC_CH8`, not the old-board `ADC_CH9`.
    - `ship_protocol_init()` now performs the ADC-related board-side init and first sample read.
    - Scheduler keeps the old 10 ms cadence, with a dedicated down-sampled ADC update divider `SHIP_POWER_SAMPLE_DIVIDER = 100`.
    - Added runtime-tracked sample period `ship_protocol_rt.power_sample_period_ms = 1000`.
    - `payload[13]` now carries the real old-style discrete power level `0..4` derived from ADC thresholds instead of a placeholder.
  - Added runtime power sample cache fields for `raw`, `adc_mv`, `bat_mv`, `report`, `valid`, low-power tick accumulation, and ADC-ready state.
  - Historical note for that step: at the time `payload[14]` and low-power auto-return were still blocked by missing AutoDrive/ShipControl. They are restored by the 2026-05-24 state-machine port above.

- Ported the v1.1 IMU/MAG data-solving path without reverting the v2 layer split:
  - Added `AHRS` quaternion attitude solver under `Components`.
  - Added `HeadingEstimator` under `Components`.
  - Added `MagCompass` under `Components` for QMC6309 compass heading, install offset, norm/jump gates, and IIR ready filtering.
  - Added `platform_scheduler_get_tick_ms()` for 1 ms tick snapshots.
  - Wired the App AHRS polling path to poll IMU at about 17 ms and MAG at about 100 ms, feed AHRS/MagCompass/HeadingEstimator, expose attitude/heading getters, and print low-rate `AHRS`/`HDG` diagnostics.
  - Added the new component files to the Keil project; command-line build passes with `0 Error(s), 0 Warning(s)`.

- 将工程整理为 Black Pearl v2.0，作为 v1.1 的移植、解耦和重构基线。
- 移除旧版本中混杂的 `User/` 层。
- 将真实应用入口移动到 `App/Src/main.c`。
- 在 App 层加入最小应用生命周期钩子。
- 将平台启动、时钟配置和调度胶水迁移到 `Platform/`。
- 将 STC 启动汇编移动到 `Startup/`。
- 将官方 STC 驱动目录从 `Driver/` 重命名为 `Drivers/`。
- 将自定义 MCU 统一抽象层从 `Drivers/` 中拆出为 `McuAbstraction/`，
  避免与 STC 官方 SDK 文件混放。
- 更新 Keil 工程分组和 include path，以匹配新目录结构。
- 增加显式平台时钟配置：
  - 使用 `EAXSFR()` 开启扩展 SFR 访问。
  - 使用 `HIRCClkConfig(0)` 选择 HIRC 不分频。
  - 保持 `MAIN_Fosc = 24000000L`，兼容官方示例分频语义。
- 增加中文工程文档：
  - 根目录 `README.md`
  - `doc/total.md`
  - `doc/data.md`
  - `doc/CHANGELOG.md`
- 迁移旧版 `Code_boweny/Function/Log/`：
  - 新增 `McuAbstraction/Inc/ef_uart.h`、`McuAbstraction/Src/ef_uart.c`，封装 STC UART。
  - 新增 `BoardDevices/Inc/board_console.h`、`BoardDevices/Src/board_console.c`，
    固定 UART1/P3.1/P3.0/115200 8N1。
  - 新增 `Services/Inc/logger.h`、`Services/Src/logger.c`，提供
    `LOGI/LOGW/LOGE/LOGD/log_printf`。
  - 新增 `Services/Inc/Log.h` 作为旧 include 兼容入口。
  - `logger` 使用 128 字节有界缓冲，超长日志截断，不再依赖 `vsprintf()`。
  - App bring-up 当前负责 `board_console_init()`、`log_init()`、设备 bring-up
    和 GPS 初始化的高层顺序。
- 更新 Keil 工程分组和 include path，加入 `Services`、`BoardDevices`、
  `McuAbstraction` 和 `logger` 相关文件。
- 新增 `McuAbstraction/Inc/ef_iic.h`、`McuAbstraction/Src/ef_iic.c`，按
  STC DMA IIC 参考时序迁移寄存器连续读写封装，并加入超时返回。
- 新增 `BoardDevices/Src/board_sensor_bus.c`、`BoardDevices/Src/board_sensor_bus.h`，
  在 BoardDevices 私有层固定 P1.4/P1.5、100 kHz，复用 DMA IIC 访问。
- 新增 `ChipDrivers/Inc/QMI8658.h`、`ChipDrivers/Src/QMI8658.c`，
  完成 QMI8658 地址探测、最小初始化、状态读取和原始采样读取。
- 将 `BoardDevices/Inc/board_imu.h`、`BoardDevices/Src/board_imu.c` 从占位推进为
  实际板级绑定，初始化、周期服务和读数路径均返回显式状态码。
- 修复并验证 QMI8658 IIC 初始化链路：
  - 恢复旧版可靠 bring-up 时序：上电等待 500 ms、初始化重试 3 次、重试间隔 200 ms。
  - 在 QMI8658 配置完成后轮询 `STATUS0`，确认 accel/gyro ready 后再进入 initialized 状态。
  - 调整应用 bring-up 顺序为 IMU 优先初始化，再初始化 QMC6309 和 LT8920。
  - 已完成板上移植验证，并通过 Keil 命令行编译，结果为 0 Error(s)、0 Warning(s)。
- 新增 `BoardDevices/Inc/board_mag.h`、`BoardDevices/Src/board_mag.c`，
  完成 QMC6309 板级磁力计绑定并复用 `board_sensor_bus`。
- 将 `QMC6309`、`LT8920`、`KCT8206` 芯片驱动整理进 `ChipDrivers/`，
  板级资源绑定收敛到 `BoardDevices/`。
- 新增 `BoardDevices/Inc/board_lt8920.h`、`BoardDevices/Src/board_lt8920.c`，
  完成 LT8920 + KCT8206 板级 bring-up、SPI 路由和寄存器校验诊断。
- `McuAbstraction/Inc/ef_spi.h`、`McuAbstraction/Src/ef_spi.c` 补充
  `ef_spi_transfer_byte()`，供 LT8920 全双工寄存器访问使用。
- App bring-up 在日志初始化后输出版本号，并串口打印 QMI8658、QMC6309、
  LT8920 的初始化结果；LT8920 校验失败时追加寄存器诊断信息。
- 从本地待迁移参考代码迁移低通滤波模块：
  - 新增 `Components/Inc/Filter.h`、`Components/Src/Filter.c`。
  - 保留旧 `Filter_*` API，供后续 QMI8658/QMC6309 读数路径直接接入。
  - 去除对 `config.h` 的依赖，改为仅依赖 `type_def.h`，保持 Components
    层纯算法边界。
- 迁移 SPI-PS 参考实现：
  - 新增 `McuAbstraction/Inc/ef_spi.h`、`McuAbstraction/Src/ef_spi.c`，封装 STC SPI 初始化、
    主从切换、字节收发和从机 ISR 接收缓冲。
  - 新增 `BoardDevices/Inc/board_spi_ps.h`、`BoardDevices/Src/board_spi_ps.c`，
    固定 P2.2/P2.3/P2.4/P2.5，并保留“默认从机、SS 空闲抢主发送、发完退回
    从机”的官方 SPI-PS 行为。
  - 参考代码里的 UART2 桥接回显逻辑未迁入抽象层。
  - `Examples/` 和 `need to do/` 改为本地参考材料，不再纳入版本管理。
  - 更新 Keil 工程分组，将 `ef_spi.c` 放入 `McuAbstraction`，并加入
    `board_spi_ps.c`、`board_sensor_bus.c`、`board_mag.c`、`board_lt8920.c`、
    `QMI8658.c` 以及迁移后的 `QMC6309`、`LT8920`、`KCT8206`。
- 迁移 v1.1 `Code_boweny/Device/WIRELESS` 无线状态机的 Level 1.5 协议骨架：
  - 新增 `BoardDevices/Inc/board_wireless.h`、`BoardDevices/Src/board_wireless.c`，
    在 LT8920/KCT8206 板级绑定之上提供 `BOARD_WIRELESS_MODE_IDLE/RX/TX`、
    RX/TX、RF payload 队列、工作信道切换、sync 寄存器配置和天线 RSSI 扫描。
  - 新增 `App/Inc/ship_protocol.h`、`App/Src/ship_protocol*.c`，实现
    `SHIP_PROTOCOL_STATE_BOOT_WAIT/PAIR_SEND/WORK_RX` 船端协议状态机。
  - 保留旧协议帧格式 `AA | len | cmd | payload | xor | BB`，固定 pair channel
    `0x7F`、seed `65 65 A0 65`、pair send count `10`，合法帧后发送 cmd `0x12`
    和 15 字节 GPS payload。
  - 补齐老版本 seed 配对功能：固定 `PAIR_REQ` 帧为
    `AA 06 10 65 65 A0 65 D3 BB`，旧算法派生 `work_rx=13`、`work_tx=77`、
    `key=32/30`、`reg36=0x2020`、`reg39=0x1E1E`；最后一次 `PAIR_REQ`
    后写 sync 并开启 500 tick 的 `PAIR_RSP(0x0F)` 响应窗口，窗口外响应忽略。
  - `0x11/0x13/0x14/0x15` 当时先迁移为空口解析和事件状态机：
    `0x11` 输出油门/转向/按键边沿事件，`0x13` 输出返航点事件，`0x14`
    输出钓点坐标事件，`0x15` 输出自动返航开关和可选返航点事件；该阶段未迁移
    `ShipControl/AutoDrive`、电机 PWM、灯控、钓点保存列表或返航闭环，已在
    2026-05-24 状态机移植中补齐除未确认灯控外的主链路。
  - 复核 v1.1 `Code_boweny/Device/WIRELESS/ship_protocol.c`、
    `Code_boweny/Device/WIRELESS/README.md`、`Code_boweny/Device/AutoDrive/autodrive.c`
    后修正协议语义：
    保存/去钓点属于 `0x14 -> AutoDrive_SetFishPositionRaw()` 的状态判断结果，
    `0x15` 不是钓点 action，而是旧 `RETURN_SWITCH` 命令。
  - 修正 `ship_protocol_event_snapshot_t` 事件命名：移除错误的
    `SAVE_FISH/RETURN_FISH/FISH_ACTION` 设计，改为 `SHIP_PROTOCOL_EVENT_FISH_POINT`
    和 `SHIP_PROTOCOL_EVENT_RETURN_SWITCH`；`0x15` 通过 `switch_state` 暴露开关字节。
  - 在 `doc/data.md` 增加旧无线命令对照表，记录 `0x0F..0x16` 的方向、payload、
    旧处理入口、旧工程动作和当前 Level 1.5 事件迁移目标。
  - 补充无线运行日志验收口径：启动、配对、工作 RX、协议事件、`0x12` 回包、
    RF 队列/CRC/超时日志均用于联调确认；日志只作为诊断输出，不改变层级依赖边界。
  - App bring-up 改为通过 `board_wireless_init()` 完成 RF bring-up，并在主循环中调用
    `board_wireless_poll()`、`ship_protocol_run_scheduler()` 和
    `board_wireless_search_signal_poll()`。
  - 更新 Keil 工程分组，加入 `board_wireless.c/h` 和 `ship_protocol.c/h`。
- 更新 `README.md`、`doc/total.md`、`doc/data.md`，同步无线链路、协议状态机、
  主循环调度、配对常量、`0x12` GPS 回包和当前未迁移业务边界。
