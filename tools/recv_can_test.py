#!/usr/bin/env python3
"""USB2CAN receive-side host test utility."""

from __future__ import annotations

import argparse
import sys
from datetime import datetime
from dataclasses import dataclass, field
from typing import Iterable

import serial


PROTOCOL_HEAD = 0xA5
CMD_CAN_TX = 0x01
CMD_CAN_RX_REPORT = 0x02
CMD_ERROR_REPORT = 0x7F
HEADER_SIZE = 5
CRC16_SIZE = 2
DEFAULT_PORT = "/dev/ttyACM0"

ERROR_CODE_NAMES = {
    0x00: "None",
    0x01: "InvalidArgument",
    0x02: "BufferTooSmall",
    0x03: "ChecksumError",
    0x04: "LengthError",
    0x05: "NeedMoreData",
    0x06: "Unsupported",
    0x07: "IoError",
}


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


def format_hex(data: Iterable[int]) -> str:
    return " ".join(f"{value:02X}" for value in data)


def format_timestamp() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")


def parse_protocol_frame(raw_frame: bytes) -> dict:
    if len(raw_frame) < HEADER_SIZE + CRC16_SIZE:
        raise ValueError("frame too short")
    if raw_frame[0] != PROTOCOL_HEAD:
        raise ValueError(f"unexpected head byte: 0x{raw_frame[0]:02X}")

    payload_length = int.from_bytes(raw_frame[2:4], byteorder="little")
    expected_length = HEADER_SIZE + CRC16_SIZE + payload_length
    if len(raw_frame) != expected_length:
        raise ValueError(
            f"frame length mismatch: expected {expected_length}, got {len(raw_frame)}"
        )
    if raw_frame[4] != crc8(raw_frame[:4]):
        raise ValueError("crc8 mismatch")

    payload = raw_frame[7:]
    payload_crc = int.from_bytes(raw_frame[5:7], byteorder="little")
    if payload_crc != crc16(payload):
        raise ValueError("crc16 mismatch")

    return {
        "head": raw_frame[0],
        "cmd": raw_frame[1],
        "len": payload_length,
        "crc8": raw_frame[4],
        "crc16": payload_crc,
        "data": payload,
        "raw_frame": raw_frame,
    }


def decode_packet(packet: dict) -> dict:
    data = packet["data"]

    if packet["cmd"] == CMD_CAN_RX_REPORT:
        if len(data) < 3:
            raise ValueError("CAN report payload too short")
        can_id = int.from_bytes(data[0:2], byteorder="little")
        dlc = data[2]
        payload = data[3:]
        if dlc > 8:
            raise ValueError("CAN report DLC out of range")
        if len(payload) != dlc:
            raise ValueError("CAN report DLC length mismatch")
        return {
            "kind": "can_rx",
            "can_id": can_id,
            "dlc": dlc,
            "payload": payload,
            "raw_frame": packet["raw_frame"],
        }

    if packet["cmd"] == CMD_ERROR_REPORT:
        if len(data) != 1:
            raise ValueError("error report payload length mismatch")
        error_code = data[0]
        return {
            "kind": "error",
            "error_code": error_code,
            "error_name": ERROR_CODE_NAMES.get(error_code, "Unknown"),
            "raw_frame": packet["raw_frame"],
        }

    return {
        "kind": "unknown",
        "cmd": packet["cmd"],
        "data": data,
        "raw_frame": packet["raw_frame"],
    }


def format_decoded_message(message: dict) -> str:
    if message["kind"] == "can_rx":
        return (
            f"CAN_RX can_id=0x{message['can_id']:03X} "
            f"dlc={message['dlc']} "
            f"payload={format_hex(message['payload'])} "
            f"raw={format_hex(message['raw_frame'])}"
        )

    if message["kind"] == "error":
        return (
            f"ERROR_REPORT code=0x{message['error_code']:02X} "
            f"name={message['error_name']} "
            f"raw={format_hex(message['raw_frame'])}"
        )

    return (
        f"UNKNOWN_REPORT cmd=0x{message['cmd']:02X} "
        f"data={format_hex(message['data'])} "
        f"raw={format_hex(message['raw_frame'])}"
    )


@dataclass
class ProtocolStreamParser:
    buffer: bytearray = field(default_factory=bytearray)

    def push(self, chunk: bytes) -> list[dict]:
        self.buffer.extend(chunk)
        packets: list[dict] = []

        while True:
            while self.buffer and self.buffer[0] != PROTOCOL_HEAD:
                del self.buffer[0]

            if len(self.buffer) < HEADER_SIZE:
                break

            payload_length = int.from_bytes(self.buffer[2:4], byteorder="little")
            expected_length = HEADER_SIZE + CRC16_SIZE + payload_length

            if len(self.buffer) < expected_length:
                break

            raw_frame = bytes(self.buffer[:expected_length])
            del self.buffer[:expected_length]
            packets.append(parse_protocol_frame(raw_frame))

        return packets


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Receive and print USB2CAN reports from a USB CDC ACM device."
    )
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial device path.")
    parser.add_argument("--baudrate", type=int, default=115200, help="Host serial baudrate setting.")
    parser.add_argument(
        "--timeout",
        type=float,
        default=0,
        help="Serial read timeout in seconds.",
    )
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=1024,
        help="Max bytes to read per serial read call.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    parser = ProtocolStreamParser()

    print(f"listening on: {args.port}")
    print("press Ctrl+C to stop")

    try:
        with serial.Serial(args.port, baudrate=args.baudrate, timeout=args.timeout) as ser:
            while True:
                chunk = ser.read(args.chunk_size)
                if not chunk:
                    continue

                try:
                    packets = parser.push(chunk)
                except ValueError as exc:
                    print(f"parse error: {exc}", file=sys.stderr)
                    parser = ProtocolStreamParser()
                    continue

                for packet in packets:
                    try:
                        decoded = decode_packet(packet)
                    except ValueError as exc:
                        print(
                            f"{format_timestamp()} decode error: {exc} "
                            f"raw={format_hex(packet['raw_frame'])}",
                            file=sys.stderr,
                        )
                        continue
                    print(
                        f"{format_timestamp()} {format_decoded_message(decoded)}",
                        flush=True,
                    )
    except KeyboardInterrupt:
        print("\nstopped")
        return 0
    except serial.SerialException as exc:
        print(f"{format_timestamp()} serial open/read failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
