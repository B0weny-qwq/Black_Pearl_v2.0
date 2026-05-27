# BoardDevices 层说明

`BoardDevices/` 封装“这块板子上具体接了什么硬件”。App 只能看见这里的板级 API，不关心引脚、外设实例、地址、电平极性和芯片复位时序。

## 目录

- `Inc/`：板级设备公开接口，例如 GPS、IMU、MAG、无线、电机、电源、存储。
- `Src/`：接口实现，允许调用 `McuAbstraction/`、`ChipDrivers/` 和 `Drivers/`。

## 现有设备

- `board_console`：UART1 日志控制台。
- `board_gps`：UART2 GPS 与 NMEA 状态。
- `board_imu`：QMI8658 板级初始化和原始采样。
- `board_mag`：QMC6309 板级初始化和原始采样。
- `board_mag_get_diag()` 只暴露地址和关键寄存器快照，供 App 打印现场日志；App 仍不直接包含 `QMC6309.h`。
- `board_motor`：左右电机 PWM 输出。
- `board_power`：电池 ADC 采样和 `0..4` 电量等级。
- `board_wireless` / `board_lt8920`：LT8920/KCT8206 无线链路。
- `board_storage`：EEPROM/IAP 小配置存储。
- `board_spi_ps`：SPI-PS 对等链路，当前受资源保护。

## 新增硬件流程

以 LED 为例：

1. 新建 `BoardDevices/Inc/board_led.h` 和 `BoardDevices/Src/board_led.c`。
2. 在 `.c` 内绑定真实引脚和极性，只暴露 `board_led_init()`、`board_led_set()` 这类简单 API。
3. 更新 `CMakeLists.txt` 和 `RVMDK/STC32G-LIB.uvproj`。
4. 在 `App/Src/app.c` bring-up 调用初始化。
5. 在 `App/Src/app_extension.c` 响应按键事件并调用 `board_led_*()`。

不要把 LED 引脚宏直接写进 `App/`。
