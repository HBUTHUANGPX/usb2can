#!/usr/bin/env python3
"""USB2CAN send-only host test utility."""

from __future__ import annotations

import argparse
import sys
import time
from typing import Iterable

import serial


PROTOCOL_HEAD = 0xA5
CMD_CAN_TX = 0x01
DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_CAN_ID = 0x123
DEFAULT_DATA = bytes([0x11, 0x22, 0x33, 0x44])


def crc8(data: bytes) -> int:
    crc = 0xFF
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x31) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc & 0xFFFF


def parse_can_id(text: str) -> int:
    can_id = int(text, 0)
    if not 0 <= can_id <= 0x7FF:
        raise argparse.ArgumentTypeError("CAN ID must be in range 0x000-0x7FF")
    return can_id


def parse_data_bytes(text: str) -> bytes:
    normalized = text.replace(",", " ").strip()
    if not normalized:
        return b""

    values = []
    for item in normalized.split():
        value = int(item, 16)
        if not 0 <= value <= 0xFF:
            raise argparse.ArgumentTypeError(f"byte out of range: {item}")
        values.append(value)

    if len(values) > 8:
        raise argparse.ArgumentTypeError("CAN payload supports at most 8 bytes")

    return bytes(values)


def format_hex(data: Iterable[int]) -> str:
    return " ".join(f"{value:02X}" for value in data)


def build_payload(can_id: int, payload: bytes) -> bytes:
    return can_id.to_bytes(2, byteorder="little") + bytes([len(payload)]) + payload


def build_protocol_frame(can_id: int, payload: bytes) -> bytes:
    data = build_payload(can_id, payload)
    header = bytes([PROTOCOL_HEAD, CMD_CAN_TX]) + len(data).to_bytes(2, byteorder="little")
    header_crc = bytes([crc8(header)])
    data_crc = crc16(data).to_bytes(2, byteorder="little")
    return header + header_crc + data_crc + data


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send one USB2CAN test frame over USB CDC ACM.")
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial device path.")
    parser.add_argument("--baudrate", type=int, default=115200, help="Host serial baudrate setting.")
    parser.add_argument("--can-id", default=hex(DEFAULT_CAN_ID), help="Standard CAN ID, e.g. 0x123.")
    parser.add_argument(
        "--data",
        default=format_hex(DEFAULT_DATA),
        help="CAN payload bytes in hex, e.g. '11 22 33 44'.",
    )
    parser.add_argument("--count", type=int, default=1, help="Number of frames to send.")
    parser.add_argument(
        "--interval",
        type=float,
        default=0.1,
        help="Seconds between frames when count > 1.",
    )
    args = parser.parse_args(argv)
    if args.count < 1:
        parser.error("--count must be >= 1")
    if args.interval < 0:
        parser.error("--interval must be >= 0")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    can_id = parse_can_id(args.can_id)
    payload = parse_data_bytes(args.data)
    frame = build_protocol_frame(can_id, payload)

    print(f"port: {args.port}")
    print(f"can_id: 0x{can_id:03X}")
    print(f"payload[{len(payload)}]: {format_hex(payload)}")
    print(f"usb_frame[{len(frame)}]: {format_hex(frame)}")

    try:
        with serial.Serial(args.port, baudrate=args.baudrate, timeout=1) as ser:
            for index in range(args.count):
                written = ser.write(frame)
                ser.flush()
                print(f"sent {index + 1}/{args.count}, wrote {written} bytes")
                if index + 1 < args.count and args.interval > 0:
                    time.sleep(args.interval)
    except serial.SerialException as exc:
        print(f"serial open/send failed: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
