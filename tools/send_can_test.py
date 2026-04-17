#!/usr/bin/env python3
"""USB2CAN send-only host test utility."""

from __future__ import annotations

import argparse
import sys
import time
from typing import Iterable

PROTOCOL_HEAD = 0xA5
CMD_CAN_TX = 0x01
CMD_CANFD_TX = 0x03
CMD_GET_MODE = 0x10
CMD_GET_CAPABILITY = 0x14
CMD_SET_MODE = 0x12
DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_CAN_ID = 0x123
DEFAULT_DATA = bytes([0x11, 0x22, 0x33, 0x44])
MODE_CAN2_STD = 0x00
MODE_CANFD_STD = 0x01
MODE_CANFD_STD_BRS = 0x02
MODE_NAME_TO_ID = {
    "can2": MODE_CAN2_STD,
    "canfd": MODE_CANFD_STD,
    "canfd-brs": MODE_CANFD_STD_BRS,
}
CANFD_VALID_LENGTHS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64}


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

    return bytes(values)


def format_hex(data: Iterable[int]) -> str:
    return " ".join(f"{value:02X}" for value in data)


def build_payload(can_id: int, payload: bytes) -> bytes:
    return can_id.to_bytes(2, byteorder="little") + bytes([len(payload)]) + payload


def build_raw_protocol_frame(cmd: int, data: bytes) -> bytes:
    header = bytes([PROTOCOL_HEAD, cmd]) + len(data).to_bytes(2, byteorder="little")
    header_crc = bytes([crc8(header)])
    data_crc = crc16(data).to_bytes(2, byteorder="little")
    return header + header_crc + data_crc + data


def build_protocol_frame(can_id: int, payload: bytes) -> bytes:
    if len(payload) > 8:
        raise ValueError("CAN2 payload supports at most 8 bytes")
    data = build_payload(can_id, payload)
    return build_raw_protocol_frame(CMD_CAN_TX, data)


def build_set_mode_frame(mode_id: int) -> bytes:
    return build_raw_protocol_frame(CMD_SET_MODE, bytes([mode_id]))


def build_get_mode_frame() -> bytes:
    return build_raw_protocol_frame(CMD_GET_MODE, b"")


def build_get_capability_frame() -> bytes:
    return build_raw_protocol_frame(CMD_GET_CAPABILITY, b"")


def should_send_mode_select(args: argparse.Namespace) -> bool:
    return args.query is None


def build_canfd_protocol_frame(can_id: int, payload: bytes) -> bytes:
    if len(payload) not in CANFD_VALID_LENGTHS:
        raise ValueError("CAN FD payload length must be one of 0..8,12,16,20,24,32,48,64")
    data = can_id.to_bytes(2, byteorder="little") + bytes([len(payload)]) + payload
    return build_raw_protocol_frame(CMD_CANFD_TX, data)


def build_batched_frames(frame: bytes, count: int) -> bytes:
    if count < 0:
        raise ValueError("count must be >= 0")
    return frame * count


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send USB2CAN test frames over USB CDC ACM.")
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial device path.")
    parser.add_argument("--baudrate", type=int, default=115200, help="Host serial baudrate setting.")
    parser.add_argument(
        "--mode",
        choices=sorted(MODE_NAME_TO_ID.keys()),
        default="can2",
        help="Active communication mode to request before data transmission.",
    )
    parser.add_argument("--can-id", default=hex(DEFAULT_CAN_ID), help="Standard CAN ID, e.g. 0x123.")
    parser.add_argument(
        "--data",
        default=format_hex(DEFAULT_DATA),
        help="CAN payload bytes in hex, e.g. '11 22 33 44'.",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=0,
        help="Number of frames to send. Use 0 for continuous sending.",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=0.1,
        help="Seconds between frames when count > 1.",
    )
    parser.add_argument(
        "--pack-count",
        action="store_true",
        help="When count > 0, pack count protocol frames into one CDC write.",
    )
    parser.add_argument(
        "--query",
        choices=["get-mode", "get-capability"],
        help="Send a control-plane query instead of CAN data.",
    )
    parser.add_argument(
        "--read-response",
        action="store_true",
        help="Read and print device responses after sending control/data frames.",
    )
    parser.add_argument(
        "--set-mode-only",
        action="store_true",
        help="Send SET_MODE and exit without transmitting CAN data.",
    )
    args = parser.parse_args(argv)
    if args.count < 0:
        parser.error("--count must be >= 0")
    if args.interval < 0:
        parser.error("--interval must be >= 0")
    if args.pack_count and args.count == 0:
        parser.error("--pack-count requires --count > 0")
    if args.query is not None and args.pack_count:
        parser.error("--query cannot be used with --pack-count")
    if args.query is not None and args.set_mode_only:
        parser.error("--query cannot be used with --set-mode-only")
    if args.mode == "can2" and len(parse_data_bytes(args.data)) > 8:
        parser.error("CAN2 mode payload supports at most 8 bytes")
    if args.mode != "can2" and len(parse_data_bytes(args.data)) not in CANFD_VALID_LENGTHS:
        parser.error("CAN FD mode payload length must be one of 0..8,12,16,20,24,32,48,64")
    return args


def main(argv: list[str] | None = None) -> int:
    try:
        import serial
    except ModuleNotFoundError:
        print("pyserial is required, install it with: pip install pyserial", file=sys.stderr)
        return 1

    args = parse_args(argv)
    can_id = parse_can_id(args.can_id)
    payload = parse_data_bytes(args.data)
    mode_id = MODE_NAME_TO_ID[args.mode]
    mode_frame = build_set_mode_frame(mode_id)
    frame = build_protocol_frame(can_id, payload) if args.mode == "can2" else build_canfd_protocol_frame(can_id, payload)
    query_frame = None
    if args.query == "get-mode":
        query_frame = build_get_mode_frame()
    elif args.query == "get-capability":
        query_frame = build_get_capability_frame()

    print(f"port: {args.port}")
    print(f"can_id: 0x{can_id:03X}")
    print(f"mode_select[{len(mode_frame)}]: {format_hex(mode_frame)}")
    if args.query is not None:
        print(f"query_frame[{len(query_frame)}]: {format_hex(query_frame)}")
        print(f"query: {args.query}")
    else:
        print(f"payload[{len(payload)}]: {format_hex(payload)}")
        print(f"usb_frame[{len(frame)}]: {format_hex(frame)}")
        print(f"active_mode: {args.mode}")
        if args.count == 0:
            print("mode: continuous")
        elif args.pack_count:
            print(f"mode: finite batched ({args.count} frames in one write)")
        else:
            print(f"mode: finite ({args.count} frames)")

    try:
        with serial.Serial(args.port, baudrate=args.baudrate, timeout=1) as ser:
            parser = None
            if args.read_response:
                import recv_can_test

                parser = recv_can_test.ProtocolStreamParser()

            def read_responses(expected_label: str) -> None:
                if parser is None:
                    return
                deadline = time.time() + 1.0
                while time.time() < deadline:
                    chunk = ser.read(1024)
                    if not chunk:
                        continue
                    packets = parser.push(chunk)
                    for packet in packets:
                        decoded = recv_can_test.decode_packet(packet)
                        print(f"{expected_label}: {recv_can_test.format_decoded_message(decoded)}")
                    if packets:
                        return

            index = 0
            if should_send_mode_select(args):
                ser.write(mode_frame)
                ser.flush()
                read_responses("mode_rsp")
            if args.set_mode_only:
                return 0
            if args.query is not None:
                ser.write(query_frame)
                ser.flush()
                read_responses("query_rsp")
                return 0
            if args.pack_count:
                batched_frames = build_batched_frames(frame, args.count)
                written = ser.write(batched_frames)
                ser.flush()
                read_responses("data_rsp")
                index = args.count
                print(f"sent one batch with {args.count} frames, wrote {written} bytes")
                return 0

            while args.count == 0 or index < args.count:
                written = ser.write(frame)
                ser.flush()
                read_responses("data_rsp")
                index += 1
                if args.count == 0:
                    print(f"sent {index}, wrote {written} bytes")
                else:
                    print(f"sent {index}/{args.count}, wrote {written} bytes")
                if (args.count == 0 or index < args.count) and args.interval > 0:
                    time.sleep(args.interval)
    except serial.SerialException as exc:
        print(f"serial open/send failed: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print(f"\nstopped by user after sending {index} frame(s)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
