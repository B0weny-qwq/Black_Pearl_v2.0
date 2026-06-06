#!/usr/bin/env python3
"""Generate board resource C macros from embedforge.yaml.

This project keeps the hardware allocation in embedforge.yaml.  Keil/C251
cannot consume YAML directly, so this script emits a small C header that the
BoardDevices layer includes.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
YAML_PATH = ROOT / "embedforge.yaml"
OUT_PATH = ROOT / "build" / "generated" / "ef_board_resources.h"


def read_yaml_text() -> str:
    if not YAML_PATH.exists():
        raise SystemExit(f"missing {YAML_PATH}")
    return YAML_PATH.read_text(encoding="utf-8")


def require(pattern: str, text: str, label: str) -> str:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        raise SystemExit(f"embedforge.yaml missing {label}")
    return match.group(1)


def pin_mask(pin: str) -> str:
    match = re.fullmatch(r"P([0-7])\.([0-7])", pin)
    if match is None:
        raise SystemExit(f"unsupported pin format: {pin}")
    return f"GPIO_Pin_{match.group(2)}"


def port_bit(pin: str) -> str:
    match = re.fullmatch(r"P([0-7])\.([0-7])", pin)
    if match is None:
        raise SystemExit(f"unsupported pin format: {pin}")
    return f"P{match.group(1)}{match.group(2)}"


def top_block(text: str, name: str) -> str:
    pattern = rf"^{re.escape(name)}:\n((?:  .*\n|    .*\n|      .*\n|        .*\n|          .*\n)*)"
    return require(pattern, text, name)


def block(text: str, name: str) -> str:
    pattern = rf"^  {re.escape(name)}:\n((?:    .*\n|      .*\n|        .*\n|          .*\n)*)"
    return require(pattern, text, name)


def field(section: str, name: str) -> str:
    return require(rf"^\s+{re.escape(name)}:\s+([^\n]+)$", section, name).strip()


def pin(section: str, name: str) -> str:
    pins = require(r"^\s+pins:\n((?:\s{6}.*\n)+)", section, "pins")
    return require(rf"^\s+{re.escape(name)}:\s+([P0-7.]+)$", pins, f"pin {name}").strip()


def gpio_pin(text: str, name: str) -> str:
    section = block(text, name)
    return field(section, "pin")


def i2c_speed_macro(speed_hz: str) -> str:
    mapping = {
        "100000": "EF_IIC_SPEED_100K",
        "200000": "EF_IIC_SPEED_200K",
        "400000": "EF_IIC_SPEED_400K",
    }
    try:
        return mapping[speed_hz.strip()]
    except KeyError:
        raise SystemExit(f"unsupported sensor_i2c speed_hz: {speed_hz}")


def i2c_route_id(mux: str) -> int:
    mapping = {
        "I2C_P14_P15": 0,
        "I2C_P24_P25": 1,
        "I2C_P76_P77": 2,
        "I2C_P33_P32": 3,
    }
    try:
        return mapping[mux.strip()]
    except KeyError:
        raise SystemExit(f"unsupported sensor I2C mux: {mux}")


def i2c_pin_group_macro(mux: str) -> str:
    mapping = {
        "I2C_P14_P15": "EF_IIC_PIN_P14_P15",
        "I2C_P24_P25": "EF_IIC_PIN_P24_P25",
        "I2C_P76_P77": "EF_IIC_PIN_P76_P77",
        "I2C_P33_P32": "EF_IIC_PIN_P33_P32",
    }
    try:
        return mapping[mux.strip()]
    except KeyError:
        raise SystemExit(f"unsupported sensor I2C mux: {mux}")


def spi_route_id(mux: str) -> int:
    mapping = {
        "SPI_P54_P13_P14_P15": 0,
        "SPI_P22_P23_P24_P25": 1,
        "SPI_P54_P40_P41_P43": 2,
        "SPI_P35_P34_P33_P32": 3,
    }
    try:
        return mapping[mux.strip()]
    except KeyError:
        raise SystemExit(f"unsupported LT8920 SPI mux: {mux}")


def sensor_i2c_lt8920_spi_conflict(i2c_route: int, spi_route: int) -> bool:
    return (
        (i2c_route == 0 and spi_route == 0) or
        (i2c_route == 1 and spi_route == 1) or
        (i2c_route == 3 and spi_route == 3)
    )


def emit_header(text: str) -> str:
    console = block(text, "uart1_console")
    gnss = block(text, "uart2_gnss")
    sensor_i2c = block(text, "i2c_sensor_bus")
    radio_spi = block(text, "spi_lt8920")
    motor_pwm3 = block(text, "motor_pwm3")
    motor_pwm4 = block(text, "motor_pwm4")

    console_bus = block(text, "console_uart")
    gnss_bus = block(text, "gnss_uart")
    sensor_bus = block(text, "sensor_i2c")
    radio_bus = block(text, "radio_spi")
    sensor_i2c_speed_hz = field(sensor_bus, "speed_hz")
    sensor_i2c_mux = field(sensor_i2c, "mux")
    radio_spi_mux = field(radio_spi, "mux")
    sensor_i2c_route = i2c_route_id(sensor_i2c_mux)
    radio_spi_route = spi_route_id(radio_spi_mux)

    if sensor_i2c_lt8920_spi_conflict(sensor_i2c_route, radio_spi_route):
        raise SystemExit(
            "resource conflict: sensor_i2c pins overlap LT8920 SPI pins "
            f"({sensor_i2c_mux} vs {radio_spi_mux})"
        )

    lt8920_reset = gpio_pin(text, "lt8920_reset")
    ant_sel = gpio_pin(text, "kct8206_ant_sel")
    rxen = gpio_pin(text, "kct8206_rxen")
    txen = gpio_pin(text, "kct8206_txen")

    radio_device = block(text, "radio")
    defaults = require(r"^\s+defaults:\n((?:\s{6}.*\n)+)", radio_device, "radio defaults")
    channel = field(defaults, "channel")
    sync_word = field(defaults, "sync_word")

    return f"""/* DO NOT EDIT. Generated from embedforge.yaml by tools/gen_board_resources.py. */
#ifndef __EF_BOARD_RESOURCES_H__
#define __EF_BOARD_RESOURCES_H__

#define EF_BOARD_MAIN_CLOCK_HZ             {field(top_block(text, "mcu"), "main_clock_hz")}UL

/* Console UART */
#define EF_BOARD_CONSOLE_UART_PORT         EF_UART_PORT_1
#define EF_BOARD_CONSOLE_UART_BAUDRATE     {field(console_bus, "baudrate")}UL
#define EF_BOARD_CONSOLE_UART_MUX          {field(console, "mux")}
#define EF_BOARD_CONSOLE_RX_PIN_MASK       {pin_mask(pin(console, "rx"))}
#define EF_BOARD_CONSOLE_TX_PIN_MASK       {pin_mask(pin(console, "tx"))}

/* GNSS UART */
#define EF_BOARD_GNSS_UART_PORT            EF_UART_PORT_2
#define EF_BOARD_GNSS_UART_BAUDRATE        {field(gnss_bus, "baudrate")}UL
#define EF_BOARD_GNSS_UART_MUX             {field(gnss, "mux")}
#define EF_BOARD_GNSS_RX_PIN_MASK          {pin_mask(pin(gnss, "rx"))}
#define EF_BOARD_GNSS_TX_PIN_MASK          {pin_mask(pin(gnss, "tx"))}

/* Shared sensor I2C */
#define EF_BOARD_SENSOR_I2C_ROUTE_ID       {sensor_i2c_route}U
#define EF_BOARD_SENSOR_I2C_PIN_GROUP      {i2c_pin_group_macro(sensor_i2c_mux)}
#define EF_BOARD_SENSOR_I2C_MUX            {sensor_i2c_mux}
#define EF_BOARD_SENSOR_I2C_SPEED          {i2c_speed_macro(sensor_i2c_speed_hz)}
#define EF_BOARD_SENSOR_I2C_SPEED_HZ       {sensor_i2c_speed_hz}UL
#define EF_BOARD_SENSOR_I2C_SCL_PIN_MASK   {pin_mask(pin(sensor_i2c, "scl"))}
#define EF_BOARD_SENSOR_I2C_SDA_PIN_MASK   {pin_mask(pin(sensor_i2c, "sda"))}

/* LT8920 SPI */
#define EF_BOARD_LT8920_SPI_ROUTE_ID       {radio_spi_route}U
#define EF_BOARD_LT8920_SPI_MUX            {radio_spi_mux}
#define EF_BOARD_LT8920_SPI_SPEED          EF_SPI_SPEED_FOSC_16
#define EF_BOARD_LT8920_SPI_CS_PIN_MASK    {pin_mask(pin(radio_spi, "cs"))}
#define EF_BOARD_LT8920_SPI_SCLK_PIN_MASK  {pin_mask(pin(radio_spi, "sclk"))}
#define EF_BOARD_LT8920_SPI_MOSI_PIN_MASK  {pin_mask(pin(radio_spi, "mosi"))}
#define EF_BOARD_LT8920_SPI_MISO_PIN_MASK  {pin_mask(pin(radio_spi, "miso"))}
#define EF_BOARD_LT8920_CS_BIT             {port_bit(pin(radio_spi, "cs"))}
#define EF_BOARD_LT8920_RESET_PIN_MASK     {pin_mask(lt8920_reset)}
#define EF_BOARD_LT8920_RESET_BIT          {port_bit(lt8920_reset)}
#define EF_BOARD_LT8920_DEFAULT_CHANNEL    {channel}U
#define EF_BOARD_LT8920_DEFAULT_SYNC_WORD  {sync_word}UL

/* KCT8206 frontend GPIO */
#define EF_BOARD_KCT8206_ANT_SEL_PIN_MASK  {pin_mask(ant_sel)}
#define EF_BOARD_KCT8206_ANT_SEL_BIT       {port_bit(ant_sel)}
#define EF_BOARD_KCT8206_RXEN_PIN_MASK     {pin_mask(rxen)}
#define EF_BOARD_KCT8206_RXEN_BIT          {port_bit(rxen)}
#define EF_BOARD_KCT8206_TXEN_PIN_MASK     {pin_mask(txen)}
#define EF_BOARD_KCT8206_TXEN_BIT          {port_bit(txen)}

/* Motor PWM3 reserved pins */
#define EF_BOARD_MOTOR_PWM3_MUX            {field(motor_pwm3, "mux")}
#define EF_BOARD_MOTOR_PWM3P_PIN_MASK      {pin_mask(pin(motor_pwm3, "pwm3p"))}
#define EF_BOARD_MOTOR_PWM3N_PIN_MASK      {pin_mask(pin(motor_pwm3, "pwm3n"))}

/* Motor PWM4 reserved pins */
#define EF_BOARD_MOTOR_PWM4_MUX            {field(motor_pwm4, "mux")}
#define EF_BOARD_MOTOR_PWM4P_PIN_MASK      {pin_mask(pin(motor_pwm4, "pwm4p"))}
#define EF_BOARD_MOTOR_PWM4N_PIN_MASK      {pin_mask(pin(motor_pwm4, "pwm4n"))}

/* SPI-PS peer link is not active in the current firmware. */
#define EF_BOARD_SPI_PS_ENABLED            0U
#define EF_BOARD_SPI_PS_SHARES_LT8920_SPI  1U

#if ((EF_BOARD_SENSOR_I2C_ROUTE_ID == 0U) && (EF_BOARD_LT8920_SPI_ROUTE_ID == 0U)) || \\
    ((EF_BOARD_SENSOR_I2C_ROUTE_ID == 1U) && (EF_BOARD_LT8920_SPI_ROUTE_ID == 1U)) || \\
    ((EF_BOARD_SENSOR_I2C_ROUTE_ID == 3U) && (EF_BOARD_LT8920_SPI_ROUTE_ID == 3U))
#error "resource conflict: LT8920 SPI route overlaps sensor I2C route"
#endif

#endif
"""


def main() -> int:
    text = read_yaml_text()
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(emit_header(text), encoding="ascii")
    print(f"generated {OUT_PATH.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
