#!/usr/bin/env python3
"""Manual hardware test for the Python host library."""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Callable
from pathlib import Path
from typing import Any

ROOT_DIR = Path(__file__).resolve().parents[1]
HOST_DIR = ROOT_DIR / "host"
if str(HOST_DIR) not in sys.path:
    sys.path.insert(0, str(HOST_DIR))

from rp2350_relay_6ch import RelayClient, RelayDeviceError
from rp2350_relay_6ch.constants import CMD_PULSE, CMD_SET, OP_WRITE


def print_response(label: str, response: dict[str, Any]) -> None:
    print(f"\n## {label}")
    print(json.dumps(response, indent=2, sort_keys=True))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def expect_device_error(label: str, action: Callable[[], dict[str, Any]], rc: int) -> None:
    print(f"\n## {label}")
    try:
        response = action()
    except RelayDeviceError as exc:
        print(json.dumps({"group": exc.group, "rc": exc.rc, "error": str(exc)}, indent=2))
        require(exc.group == 64, f"{label}: expected error group 64, got {exc.group}")
        require(exc.rc == rc, f"{label}: expected rc {rc}, got {exc.rc}")
        return

    raise AssertionError(f"{label}: expected device error rc {rc}, got {response!r}")


def send_invalid_channel(client: RelayClient) -> dict[str, Any]:
    return client._request(CMD_SET, OP_WRITE, {"channel": 6, "on": True})


def send_invalid_pulse(client: RelayClient) -> dict[str, Any]:
    return client._request(CMD_PULSE, OP_WRITE, {"channel": 0, "duration_ms": 1})


def run(args: argparse.Namespace) -> None:
    client = RelayClient.connect(
        args.port,
        baudrate=args.baud,
        timeout_s=args.timeout,
        retries=args.retries,
    )
    final_off_attempted = False

    try:
        info = client.get_info()
        print_response("info", info)
        require(info.get("protocol_version") == 1, "protocol_version is not 1")
        require(info.get("relay_count") == 6, "relay_count is not 6")
        require(info.get("hardware") == "Waveshare RP2350-Relay-6CH", "unexpected hardware name")

        status = client.get_status()
        print_response("status", status)
        require(status.get("smp_uart") is True, "smp_uart is not true")
        require(status.get("state") == 0, "relays are not all off at start")

        print_response("get all", client.get_relays())
        print_response("set CH1 on", client.set_relay(0, True))
        print_response("get CH1", client.get_relays(0))
        print_response("set CH1 off", client.set_relay(0, False))
        print_response("set CH1 and CH6 on", client.set_all_relays(0x21))
        print_response("off all", client.off_all())
        print_response("pulse CH1", client.pulse_relay(0, args.pulse_ms))
        print_response("get CH1 after pulse request", client.get_relays(0))

        expect_device_error("invalid channel", lambda: send_invalid_channel(client), 2)
        expect_device_error("invalid pulse", lambda: send_invalid_pulse(client), 2)

        print_response("final status", client.get_status())
    finally:
        final_off_attempted = True
        print_response("final off all", client.off_all())

    require(final_off_attempted, "final off_all was not attempted")
    print("\nPython host library hardware test passed")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        required=True,
        help="UART1 SMP serial port, for example COM31",
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--retries", type=int, default=1)
    parser.add_argument("--pulse-ms", type=int, default=100)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
