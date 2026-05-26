#!/usr/bin/env python3
"""
Build legacy ship protocol frames for GPS point storage and auto-drive testing.

Supported commands:
- 0x13 return-home
- 0x14 goto-point
- 0x15 return-switch (+ saved return point)

The wire format matches the legacy ship firmware:
AA | len | cmd | payload... | xor | BB

For GPS point payloads, each 16-bit field is encoded as raw legacy bytes:
low-byte first, high-byte second.
"""

from __future__ import annotations

import argparse
import sys
from typing import Iterable, List, Sequence


SHIP_HEAD = 0xAA
SHIP_TAIL = 0xBB
CMD_RETURN_HOME = 0x13
CMD_GOTO_POINT = 0x14
CMD_RETURN_SWITCH = 0x15
CMD_GPS_REPORT = 0x12
GPS_REPORT_PAYLOAD_LEN = 15
LEGACY_POINT_LEN = 10


def xor_bytes(values: Iterable[int]) -> int:
    result = 0
    for value in values:
        result ^= value & 0xFF
    return result & 0xFF


def format_hex(data: Sequence[int]) -> str:
    return " ".join(f"{value:02X}" for value in data)


def format_c_array(data: Sequence[int]) -> str:
    return "{ " + ", ".join(f"0x{value:02X}" for value in data) + " }"


def parse_hex_bytes(text: str) -> List[int]:
    clean = (
        text.replace(",", " ")
        .replace("\r", " ")
        .replace("\n", " ")
        .replace("\t", " ")
    )
    parts = [part for part in clean.split(" ") if part]
    if not parts:
        raise ValueError("no hex bytes found")

    values: List[int] = []
    for part in parts:
        token = part[2:] if part.lower().startswith("0x") else part
        if len(token) == 0 or len(token) > 2:
            raise ValueError(f"bad byte token: {part}")
        values.append(int(token, 16))
    return values


def parse_u16_text(text: str) -> int:
    value = int(text, 0)
    if value < 0 or value > 0xFFFF:
        raise ValueError(f"u16 out of range: {text}")
    return value


def parse_cmd(text: str) -> int:
    mapping = {
        "0x13": CMD_RETURN_HOME,
        "return-home": CMD_RETURN_HOME,
        "home": CMD_RETURN_HOME,
        "0x14": CMD_GOTO_POINT,
        "goto-point": CMD_GOTO_POINT,
        "goto": CMD_GOTO_POINT,
        "fish": CMD_GOTO_POINT,
        "0x15": CMD_RETURN_SWITCH,
        "return-switch": CMD_RETURN_SWITCH,
        "switch": CMD_RETURN_SWITCH,
    }
    key = text.strip().lower()
    if key not in mapping:
        raise ValueError(f"unsupported cmd: {text}")
    return mapping[key]


def require_ascii_dir(label: str, value: str, allowed: str) -> str:
    if len(value) != 1:
        raise ValueError(f"{label} must be one character")
    upper = value.upper()
    if upper not in allowed:
        raise ValueError(f"{label} must be one of: {', '.join(allowed)}")
    return upper


def u16le_bytes(value: int) -> List[int]:
    return [value & 0xFF, (value >> 8) & 0xFF]


def build_point_payload(
    lon_dir: str,
    lon_whole: int,
    lon_frac: int,
    lat_dir: str,
    lat_whole: int,
    lat_frac: int,
) -> List[int]:
    return [
        ord(require_ascii_dir("lon_dir", lon_dir, "EW")),
        *u16le_bytes(lon_whole),
        *u16le_bytes(lon_frac),
        ord(require_ascii_dir("lat_dir", lat_dir, "NS")),
        *u16le_bytes(lat_whole),
        *u16le_bytes(lat_frac),
    ]


def build_frame(cmd: int, payload: Sequence[int]) -> List[int]:
    body_len = 2 + len(payload)
    body = [body_len & 0xFF, cmd & 0xFF, *payload]
    checksum = xor_bytes(body)
    return [SHIP_HEAD, *body, checksum, SHIP_TAIL]


def parse_gps_report_payload(raw: Sequence[int]) -> List[int]:
    data = list(raw)
    if len(data) == GPS_REPORT_PAYLOAD_LEN:
        return data

    if len(data) < 5:
        raise ValueError("gps report frame too short")
    if data[0] != SHIP_HEAD or data[-1] != SHIP_TAIL:
        raise ValueError("frame must start with AA and end with BB")

    body_len = data[1]
    if body_len + 3 != len(data):
        raise ValueError(
            f"frame len mismatch: len field={body_len}, actual frame len={len(data)}"
        )

    checksum = xor_bytes(data[1:-2])
    if checksum != data[-2]:
        raise ValueError(
            f"xor mismatch: calc=0x{checksum:02X}, recv=0x{data[-2]:02X}"
        )

    if data[2] != CMD_GPS_REPORT:
        raise ValueError(f"frame cmd is 0x{data[2]:02X}, expected 0x12")

    payload = data[3:-2]
    if len(payload) != GPS_REPORT_PAYLOAD_LEN:
        raise ValueError(
            f"gps report payload len is {len(payload)}, expected {GPS_REPORT_PAYLOAD_LEN}"
        )
    return payload


def decode_point_from_gps_report(payload: Sequence[int]) -> dict:
    if len(payload) != GPS_REPORT_PAYLOAD_LEN:
        raise ValueError("gps report payload must be 15 bytes")
    return {
        "sat": payload[0],
        "angle_deg": payload[1] | (payload[2] << 8),
        "lon_dir": chr(payload[3]),
        "lon_whole": payload[4] | (payload[5] << 8),
        "lon_frac": payload[6] | (payload[7] << 8),
        "lat_dir": chr(payload[8]),
        "lat_whole": payload[9] | (payload[10] << 8),
        "lat_frac": payload[11] | (payload[12] << 8),
        "power": payload[13],
        "auto": payload[14],
    }


def print_result(title: str, payload: Sequence[int], frame: Sequence[int]) -> None:
    print(title)
    print(f"payload ({len(payload)} bytes): {format_hex(payload)}")
    print(f"frame   ({len(frame)} bytes): {format_hex(frame)}")
    print(f"c array payload: {format_c_array(payload)}")
    print(f"c array frame:   {format_c_array(frame)}")


def run_from_point(args: argparse.Namespace) -> int:
    cmd = parse_cmd(args.cmd)
    point_payload = build_point_payload(
        lon_dir=args.lon_dir,
        lon_whole=parse_u16_text(args.lon_whole),
        lon_frac=parse_u16_text(args.lon_frac),
        lat_dir=args.lat_dir,
        lat_whole=parse_u16_text(args.lat_whole),
        lat_frac=parse_u16_text(args.lat_frac),
    )

    if cmd == CMD_RETURN_SWITCH:
        switch_value = int(args.switch, 0)
        if switch_value < 0 or switch_value > 0xFF:
            raise ValueError("--switch must be 0..255")
        payload = [switch_value, *point_payload]
    else:
        payload = point_payload

    frame = build_frame(cmd, payload)
    print_result("build from manual point", payload, frame)
    return 0


def run_from_gps_report(args: argparse.Namespace) -> int:
    cmd = parse_cmd(args.cmd)
    report_bytes = parse_hex_bytes(args.report)
    payload_0x12 = parse_gps_report_payload(report_bytes)
    point = decode_point_from_gps_report(payload_0x12)

    point_payload = build_point_payload(
        lon_dir=point["lon_dir"],
        lon_whole=point["lon_whole"],
        lon_frac=point["lon_frac"],
        lat_dir=point["lat_dir"],
        lat_whole=point["lat_whole"],
        lat_frac=point["lat_frac"],
    )

    if cmd == CMD_RETURN_SWITCH:
        switch_value = int(args.switch, 0)
        if switch_value < 0 or switch_value > 0xFF:
            raise ValueError("--switch must be 0..255")
        payload = [switch_value, *point_payload]
    else:
        payload = point_payload

    frame = build_frame(cmd, payload)
    print("parsed 0x12 gps report")
    print(
        "point:"
        f" sat={point['sat']}"
        f" angle={point['angle_deg']}"
        f" lon={point['lon_dir']}{point['lon_whole']}.{point['lon_frac']:04d}"
        f" lat={point['lat_dir']}{point['lat_whole']}.{point['lat_frac']:04d}"
        f" power=0x{point['power']:02X}"
        f" auto=0x{point['auto']:02X}"
    )
    print_result("build from 0x12 report", payload, frame)
    return 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build legacy ship protocol packets for return-home / goto-point tests."
    )
    subparsers = parser.add_subparsers(dest="mode", required=True)

    p_point = subparsers.add_parser(
        "from-point",
        help="build 0x13 / 0x14 / 0x15 from manual point fields",
    )
    p_point.add_argument("--cmd", required=True, help="return-home | goto-point | return-switch")
    p_point.add_argument("--switch", default="0x31", help="only used for 0x15")
    p_point.add_argument("--lon-dir", required=True, help="E or W")
    p_point.add_argument("--lon-whole", required=True, help="example: 12205")
    p_point.add_argument("--lon-frac", required=True, help="example: 47538")
    p_point.add_argument("--lat-dir", required=True, help="N or S")
    p_point.add_argument("--lat-whole", required=True, help="example: 3731")
    p_point.add_argument("--lat-frac", required=True, help="example: 52206")
    p_point.set_defaults(func=run_from_point)

    p_report = subparsers.add_parser(
        "from-gps-report",
        help="build 0x13 / 0x14 / 0x15 directly from a 0x12 gps payload or full frame",
    )
    p_report.add_argument("--cmd", required=True, help="return-home | goto-point | return-switch")
    p_report.add_argument("--switch", default="0x31", help="only used for 0x15")
    p_report.add_argument(
        "--report",
        required=True,
        help=(
            "0x12 payload hex (15 bytes) or full frame hex."
            " Example payload: 08 5A 00 45 AD 2F A2 8A 4E 8C CB 58 01 00 00"
        ),
    )
    p_report.set_defaults(func=run_from_gps_report)

    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()
    try:
        return args.func(args)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
