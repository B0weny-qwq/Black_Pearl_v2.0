---
name: embedforge-project-standards
description: Competition-first EmbedForge embedded project architecture standards. Use when Codex designs, creates, reviews, or refactors EmbedForge projects, examples, board ports, CMake layouts, peripheral abstractions, board device APIs, algorithm components, services, RTOS/OSAL glue, or generated configuration rules; especially for electronics competition projects, student embedded projects, MSPM0/STM32 examples, and avoiding App/User coupling to vendor SDKs.
---

# EmbedForge Project Standards

## Default Mindset

Optimize for electronics competition and student embedded projects first, and product-grade reusable templates second.

- Default to Level 1 for 电赛, quick board work, control projects, small vehicles, boats, sensor demos, and lab boards.
- Do not overbuild. Add architecture only when it reduces coupling, improves reuse, or saves resources.
- Keep vendor SDKs external. Never commit SDKs, generated `build/`, OpenOCD installs, Wine/Keil installs, logs, caches, binaries, or local absolute paths.
- Preserve the core dependency direction: `App/User -> BoardDevices -> Drivers -> vendor SDK`; `App/User -> Components`.

## Project Levels

**Level 0: tiny example**

- Use for blink, UART echo, I2C scan.
- Minimal layout: `embedforge.yaml`, `CMakeLists.txt`, `Platform/`, `App/`, `Startup/`, `linker/`.
- Omit Services, OSAL, device model, generated config, schema, lock, and CI scans.
- Still forbid App from including vendor SDK/HAL/DriverLib headers directly.

**Level 1: competition default**

- Use for most 电赛 projects.
- Required: `Platform/`, `Drivers/`, `BoardDevices/`, `Components/`, `App/`, `Startup/`, `linker/`.
- Add `ChipDrivers/` only when external chips exist.
- Add minimal `Services/logger` or `Services/console` only when useful.
- `embedforge.yaml` records board/chip/resources for readability; generated files are optional.
- Use simple BoardDevices APIs by default, not a full device object model.

**Level 1.5: complex competition project**

- Use for IMU control, balancing vehicles, path tracking, multiple sensors, OLED, communication, storage, or multi-loop control.
- Required: Level 1 plus `ChipDrivers/` when external chips exist.
- Optional: `Services/logger`, `Services/console`, `Services/parameter`.
- Optional: `OSAL/` only when RTOS or cross-platform service code needs it.
- Optional: `build/generated/` when resources become hard to keep consistent.
- Do not require schema, lock, full lifecycle registry, full `ef_device_t`, OTA, event bus, power framework, fault framework, or formal CI size reports.

**Level 2: product-grade project**

- Use only when explicitly requested for reusable/product-grade architecture.
- Require hardware description as source of truth, generated config, feature gates, device model, Services, OSAL when needed, and resource budget notes.

**Level 3: reusable board package/template**

- Use for shared templates, BSPs, or long-term baselines.
- Require Level 2 plus `embedforge.schema.json`, `embedforge.lock`, include-boundary CI scan, size report, and generated-file drift checks.

## Competition Layout

Level 1 / Level 1.5 default:

```text
<project>/
├── embedforge.yaml
├── CMakeLists.txt
├── Platform/
│   ├── Inc/
│   └── Src/
├── Drivers/
│   ├── Inc/
│   └── Src/
├── BoardDevices/
│   ├── Inc/
│   └── Src/
├── Components/
│   ├── Inc/
│   └── Src/
├── App/
│   ├── Inc/
│   └── Src/
├── Startup/
└── linker/
```

Add only when justified:

```text
ChipDrivers/
Services/
OSAL/
RTOS/
build/generated/
```

Layer ownership:

- `Platform/`: clocks, pinmux, vendor-generated init wrappers, board boot glue.
- `Drivers/`: unified MCU peripheral abstraction over SDK/HAL/DriverLib, such as GPIO, I2C, SPI, UART, ADC, CAN, time.
- `ChipDrivers/`: external chip protocol drivers, such as QMI8658C, SSD1306, W25Qxx, QMC6309.
- `BoardDevices/`: board-level device APIs, such as `board_imu_read`, `board_motor_set_pwm`, `board_oled_print`, `board_led_set`.
- `Components/`: pure algorithms, such as PID, low-pass/high-pass filters, estimators, path tracking, ring buffers.
- `Services/`: optional system helpers, usually logger/console/parameter for competition projects.
- `App/`: competition logic, state machine, control loop orchestration.

## Non-Negotiable Coupling Rules

Keep these even for Level 0:

- `App/` and `User/` must not include vendor SDK/HAL/DriverLib headers.
- `Components/` must not include hardware, OS, vendor SDK, BoardDevices, or Drivers headers unless explicitly designed as an adapter.
- `BoardDevices/` hides pins, buses, addresses, polarity, timer channels, and vendor handles from App.
- SDK path is injected through `sdk.env` in `embedforge.yaml`; do not hardcode `/home/<user>` paths.

Forbidden in `App/` and `Components/`:

- `stm32*.h`
- `ti/driverlib/*.h`
- `DL_*.h`
- `main.h`
- `gpio.h`
- `usart.h`
- `syscfg/*.h`
- raw pin/resource macros
- direct HAL/DriverLib calls

## CMake Boundaries

Use target-based CMake for Level 1+.

```cmake
add_library(ef_platform ...)
add_library(ef_drivers ...)
add_library(ef_boarddevices ...)
add_library(ef_components ...)
add_library(ef_app ...)

target_link_libraries(ef_boarddevices PRIVATE ef_drivers)
target_link_libraries(ef_app PRIVATE ef_boarddevices ef_components)
```

Add only when needed:

```cmake
add_library(ef_chipdrivers ...)
target_link_libraries(ef_boarddevices PRIVATE ef_chipdrivers)

add_library(ef_services ...)
target_link_libraries(ef_app PRIVATE ef_services)
```

Rules:

- Avoid global `include_directories()`.
- Vendor SDK include dirs are private to `Platform/` and vendor-specific driver implementation files.
- If `App/` can include HAL/DriverLib because of CMake visibility, the project is incorrectly structured.
- For Level 3, add CI/local include scans for the forbidden include patterns.

## BoardDevices API First

Competition projects default to simple BoardDevices APIs:

```c
app_status_t board_imu_init(void);
app_status_t board_imu_read(imu_sample_t *sample);
app_status_t board_motor_set_pwm(int16_t left, int16_t right);
app_status_t board_oled_print(uint8_t row, uint8_t col, const char *text);
app_status_t board_led_set(bool active);
```

Use full `ef_device_t` only when there are multiple same-kind devices, dynamic lookup is genuinely useful, the project is Level 2+, or the user explicitly asks for a Zephyr-like model.

For Level 2+ product paths, prefer generated symbols/macros over runtime string lookup:

```c
ef_imu_read(EF_DEVICE_IMU0, &sample, 10);
```

`ef_device_get("imu0")` is allowed for shell/debug/dynamic inspection, not as the default hot-path API on small MCUs.

## ChipDrivers Boundary

External chip protocol drivers are optional and added only when useful.

- `ChipDrivers/` must not know board instance names.
- `ChipDrivers/` must not static-bind buses, pins, chip-selects, addresses, or interrupts.
- Invalid: `#define QMI8658C_I2C I2C0`.
- Invalid: `#define QMI8658C_ADDR 0x6B` inside a reusable chip driver.
- Valid: `BoardDevices/` passes bus ops, address, interrupt pin, and mount config into the chip driver.

## Services Minimalism

Competition default Services:

- `Services/logger`
- `Services/console`
- optional `Services/parameter`

Do not default to OTA, event bus, power manager, watchdog manager, telemetry framework, fault recorder, or full storage framework.

Services are system capability helpers, not a second BoardDevices layer.

- Valid: `logger -> board_console -> ef_uart -> vendor SDK`.
- Invalid: `logger -> DL_UART_transmitDataBlocking()`.
- Services must access hardware through BoardDevices or abstract interfaces only.

## OSAL And RTOS

Only add `OSAL/` when RTOS or cross-platform service code needs it.

- `OSAL/` exposes EmbedForge APIs: `ef_mutex`, `ef_queue`, `ef_time`, `ef_thread`, `ef_critical`.
- `RTOS/` contains FreeRTOS/RT-Thread glue or kernel port integration.
- `App/Services` include OSAL only.
- `App/Services` must not include `FreeRTOS.h`, `rtthread.h`, or `zephyr/kernel.h` directly.

## Hardware Description And Generated Config

Level 1:

- `embedforge.yaml` records board/chip/resources for readability and tooling.
- Generated files are optional.

Level 1.5:

- Use `build/generated/` only when resources become complex enough that hand-sync is risky.
- Generated files are build outputs by default and must not be manually edited.

Level 2+:

- `embedforge.yaml` becomes source of truth.
- Generate `ef_config.h`, `ef_board_resources.h`, `ef_build_info.h`, `ef_device_table.c`, and `compile_flags.cmake` under `build/generated/`.
- Level 3 adds `embedforge.schema.json` and `embedforge.lock`.

Generated policy:

- Default output path is `build/generated/`.
- If a teaching example commits generated snapshots, every generated file must start with `DO NOT EDIT`.
- Generated files must not become a second source of truth.

## Resource Rules For Competition

Use practical competition resource discipline:

- State size and purpose for new large arrays.
- Centralize DMA buffers.
- Do not scatter UART/OLED/log buffers across files.
- Avoid `double` unless required.
- Avoid `printf` float formatting by default.
- Feature-gate optional Services and ChipDrivers when easy.
- Do not compile shell/logger/telemetry/storage unless enabled or useful.
- For Level 3 only, require formal `.text/.rodata/.data/.bss` delta reports.

## Chinese Doxygen Rules

Project-owned public headers require Chinese Doxygen.

```c
/**
 * @brief 读取板级 IMU 数据。
 *
 * App 层通过该接口读取姿态相关原始数据，不关心 I2C 总线、地址、中断脚和安装方向。
 *
 * @param sample 输出采样数据，不能为 NULL。
 * @return APP_OK 读取成功。
 * @return APP_ERR_INVALID_ARG 参数为空。
 * @return APP_ERR_NOT_READY IMU 尚未初始化或不可用。
 */
app_status_t board_imu_read(imu_sample_t *sample);
```

Rules:

- Public APIs document ownership, side effects, blocking behavior, timeout unit if present, and error meanings.
- Vendor-generated comments may stay as-is.
- Identifiers stay ASCII English; Chinese is for comments/docs.
- Do not add comments that only repeat assignments.

## Review Checklist

When creating or reviewing an EmbedForge project:

1. Pick the lightest valid level. Default 电赛 to Level 1.
2. Check that App/User cannot include or call vendor SDK APIs.
3. Keep Components pure and reusable.
4. Put concrete board resources behind BoardDevices APIs.
5. Add ChipDrivers only for reusable external chip protocol code.
6. Add Services/OSAL/generated config only when justified.
7. Keep SDK/toolchain/build artifacts out of the repo.
8. Prefer simple competition APIs unless the user explicitly asks for Level 2+ structure.
