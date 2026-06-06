cmake_minimum_required(VERSION 3.20)

project(Black_Pearl_v2_0 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(ef_platform
    Platform/Src/platform.c
    Platform/Src/platform_scheduler.c
)
target_include_directories(ef_platform PUBLIC Platform/Inc Drivers/Inc)

add_library(ef_drivers INTERFACE)
target_include_directories(ef_drivers INTERFACE Drivers/Inc)

add_library(ef_mcu_abstraction
    McuAbstraction/Src/ef_iic.c
    McuAbstraction/Src/ef_spi.c
    McuAbstraction/Src/ef_uart.c
)
target_include_directories(ef_mcu_abstraction PUBLIC McuAbstraction/Inc Platform/Inc build/generated)
target_link_libraries(ef_mcu_abstraction PRIVATE ef_drivers)

add_library(ef_chipdrivers
    ChipDrivers/Src/KCT8206.c
    ChipDrivers/Src/LT8920.c
    ChipDrivers/Src/QMC6309.c
    ChipDrivers/Src/QMI8658.c
    ChipDrivers/Src/gnss_nmea.c
)
target_include_directories(ef_chipdrivers PUBLIC ChipDrivers/Inc Platform/Inc McuAbstraction/Inc)
target_link_libraries(ef_chipdrivers PRIVATE ef_mcu_abstraction)

add_library(ef_boarddevices
    BoardDevices/Src/board_console.c
    BoardDevices/Src/board_gps.c
    BoardDevices/Src/board_imu.c
    BoardDevices/Src/board_lt8920.c
    BoardDevices/Src/board_mag.c
    BoardDevices/Src/board_motor.c
    BoardDevices/Src/board_power.c
    BoardDevices/Src/board_sensor_bus.c
    BoardDevices/Src/board_spi_ps.c
    BoardDevices/Src/board_storage.c
    BoardDevices/Src/board_wireless.c
)
target_include_directories(ef_boarddevices PUBLIC BoardDevices/Inc Platform/Inc build/generated)
target_link_libraries(ef_boarddevices PRIVATE ef_mcu_abstraction ef_chipdrivers ef_drivers)

add_library(ef_components
    Components/Src/AHRS.c
    Components/Src/Filter.c
    Components/Src/HeadingEstimator.c
    Components/Src/MagCompass.c
    Components/Src/PID.c
)
target_include_directories(ef_components PUBLIC Components/Inc Platform/Inc)

add_library(ef_services
    Services/Src/logger.c
    Services/Src/parameter_store.c
)
target_include_directories(ef_services PUBLIC Services/Inc Platform/Inc)
target_link_libraries(ef_services PRIVATE ef_boarddevices)

add_library(ef_app
    App/Src/app.c
    App/Src/app_ahrs_core.c
    App/Src/app_ahrs_poll.c
    App/Src/app_bringup.c
    App/Src/app_extension.c
    App/Src/app_mag_event.c
    App/Src/app_state.c
    App/Src/north_calib.c
    App/Src/autodrive.c
    App/Src/autodrive_cmd.c
    App/Src/autodrive_config.c
    App/Src/autodrive_control.c
    App/Src/autodrive_diag.c
    App/Src/autodrive_gps.c
    App/Src/autodrive_nav.c
    App/Src/autodrive_points.c
    App/Src/autodrive_poll.c
    App/Src/ship_control.c
    App/Src/ship_control_core.c
    App/Src/ship_control_manual.c
    App/Src/ship_control_yaw.c
    App/Src/ship_protocol.c
    App/Src/ship_protocol_cmd.c
    App/Src/ship_protocol_event.c
    App/Src/ship_protocol_link.c
    App/Src/ship_protocol_parse.c
    App/Src/ship_protocol_points.c
    App/Src/ship_protocol_power.c
    App/Src/ship_protocol_state.c
    App/Src/ship_protocol_tx.c
)
target_include_directories(ef_app PUBLIC App/Inc Platform/Inc)
target_link_libraries(ef_app PRIVATE ef_boarddevices ef_components ef_services)
