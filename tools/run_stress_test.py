#!/usr/bin/env python3
"""Host-side USB2CAN stress test runner."""

from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
SEND_TOOL = TOOLS_DIR / "send_can_test.py"
CANFD_VALID_LENGTHS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64}


def generate_hex_payload(length: int) -> str:
    if length not in CANFD_VALID_LENGTHS:
        raise ValueError(f"invalid CAN FD payload length: {length}")
    return " ".join(f"{value:02X}" for value in range(length))


def build_mode_switch_commands(port: str, baudrate: int, loops: int) -> list[list[str]]:
    commands: list[list[str]] = []
    for _ in range(loops):
        for mode in ("canfd", "canfd-brs", "can2"):
            commands.append(build_set_mode_command(port, baudrate, mode))
            commands.append(
                [
                    sys.executable,
                    str(SEND_TOOL),
                    "--port",
                    port,
                    "--baudrate",
                    str(baudrate),
                    "--query",
                    "get-mode",
                    "--read-response",
                ]
            )
    return commands


def build_set_mode_command(port: str, baudrate: int, mode: str) -> list[str]:
    return [
        sys.executable,
        str(SEND_TOOL),
        "--port",
        port,
        "--baudrate",
        str(baudrate),
        "--mode",
        mode,
        "--set-mode-only",
        "--read-response",
    ]


def build_burst_command(
    port: str,
    baudrate: int,
    mode: str,
    payload_length: int,
    count: int,
    interval: float,
) -> list[str]:
    return [
        sys.executable,
        str(SEND_TOOL),
        "--port",
        port,
        "--baudrate",
        str(baudrate),
        "--mode",
        mode,
        "--skip-mode-select",
        "--can-id",
        "0x123",
        "--data",
        generate_hex_payload(payload_length),
        "--count",
        str(count),
        "--interval",
        f"{interval:g}",
    ]


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run host-side USB2CAN stress checks.")
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial device path.")
    parser.add_argument("--baudrate", type=int, default=115200, help="Host serial baudrate setting.")
    parser.add_argument(
        "--switch-loops",
        type=int,
        default=10,
        help="Number of mode-switch loops to run. Each loop performs canfd -> canfd-brs -> can2.",
    )
    parser.add_argument("--burst-count", type=int, default=50, help="Number of frames per burst test.")
    parser.add_argument(
        "--burst-interval",
        type=float,
        default=0.001,
        help=(
            "Seconds between frames during burst tests. Use 0 only for explicit "
            "USB/CAN TX ring overflow testing."
        ),
    )
    parser.add_argument(
        "--scenarios",
        nargs="+",
        choices=["mode-switch", "can2-burst", "canfd-burst", "canfd-brs-burst"],
        default=["mode-switch", "can2-burst", "canfd-burst", "canfd-brs-burst"],
        help="Stress scenarios to execute.",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print commands without executing them.")
    args = parser.parse_args(argv)
    if args.switch_loops < 0:
        parser.error("--switch-loops must be >= 0")
    if args.burst_count < 0:
        parser.error("--burst-count must be >= 0")
    if args.burst_interval < 0:
        parser.error("--burst-interval must be >= 0")
    return args


def iter_commands(args: argparse.Namespace) -> list[tuple[str, list[str]]]:
    plan: list[tuple[str, list[str]]] = []
    if "mode-switch" in args.scenarios:
        for index, command in enumerate(build_mode_switch_commands(args.port, args.baudrate, args.switch_loops), start=1):
            plan.append((f"mode-switch-{index:03d}", command))
    if "can2-burst" in args.scenarios:
        plan.append(("can2-burst-mode", build_set_mode_command(args.port, args.baudrate, "can2")))
        plan.append(
            (
                "can2-burst",
                build_burst_command(args.port, args.baudrate, "can2", 4, args.burst_count, args.burst_interval),
            )
        )
    if "canfd-burst" in args.scenarios:
        plan.append(("canfd-burst-mode", build_set_mode_command(args.port, args.baudrate, "canfd")))
        plan.append(
            (
                "canfd-burst",
                build_burst_command(args.port, args.baudrate, "canfd", 12, args.burst_count, args.burst_interval),
            )
        )
    if "canfd-brs-burst" in args.scenarios:
        plan.append(("canfd-brs-burst-mode", build_set_mode_command(args.port, args.baudrate, "canfd-brs")))
        plan.append(
            (
                "canfd-brs-burst",
                build_burst_command(
                    args.port,
                    args.baudrate,
                    "canfd-brs",
                    64,
                    args.burst_count,
                    args.burst_interval,
                ),
            )
        )
    return plan


def run_command(label: str, command: list[str], dry_run: bool) -> int:
    rendered = shlex.join(command)
    print(f"[{label}] {rendered}")
    if dry_run:
        return 0

    completed = subprocess.run(command, check=False, text=True, capture_output=True)
    if completed.stdout:
        print(completed.stdout, end="" if completed.stdout.endswith("\n") else "\n")
    if completed.stderr:
        print(completed.stderr, file=sys.stderr, end="" if completed.stderr.endswith("\n") else "\n")
    if completed.returncode != 0:
        print(f"[{label}] failed with exit code {completed.returncode}", file=sys.stderr)
    return completed.returncode


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    plan = iter_commands(args)
    if not plan:
        print("no scenarios selected")
        return 0

    print("usb2can host stress runner")
    print(f"port: {args.port}")
    print(f"switch_loops: {args.switch_loops}")
    print(f"burst_count: {args.burst_count}")
    print(f"burst_interval: {args.burst_interval:g}")
    print(f"scenarios: {', '.join(args.scenarios)}")

    for label, command in plan:
        if run_command(label, command, args.dry_run) != 0:
            return 1

    print("stress run completed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
