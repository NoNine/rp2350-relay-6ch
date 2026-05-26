"""Command-line client for the RP2350 relay daemon."""

from __future__ import annotations

import argparse
import sys
from typing import Any

from .cli import (
    EXIT_ARGUMENT,
    EXIT_DEVICE,
    EXIT_OK,
    EXIT_PROTOCOL,
    EXIT_TIMEOUT,
    EXIT_TRANSPORT,
    _emit,
    _parse_channel,
    _parse_on_off,
    _parse_state,
)
from .daemon_client import RelayDaemonClient
from .exceptions import (
    RelayDeviceError,
    RelayError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)


def _client(args: argparse.Namespace) -> RelayDaemonClient:
    return RelayDaemonClient.connect(args.socket, timeout_s=args.timeout)


def cmd_info(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return client.get_info()


def cmd_build_info(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return client.get_build_info()


def cmd_get(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return client.get_relays(args.channel)


def cmd_set(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return client.set_relay(args.channel, args.state)


def cmd_set_all(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return client.set_all_relays(args.state)


def cmd_pulse(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return client.pulse_relay(args.channel, args.duration_ms)


def cmd_off_all(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return client.off_all()


def cmd_status(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return client.get_status()


def cmd_reboot(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return client.reboot()


def cmd_daemon_status(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return client.get_daemon_status()


COMMANDS = {
    "info": cmd_info,
    "build-info": cmd_build_info,
    "get": cmd_get,
    "set": cmd_set,
    "set-all": cmd_set_all,
    "pulse": cmd_pulse,
    "off-all": cmd_off_all,
    "status": cmd_status,
    "reboot": cmd_reboot,
    "daemon-status": cmd_daemon_status,
}


class RelayCtlArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.print_usage(sys.stderr)
        self.exit(EXIT_ARGUMENT, f"{self.prog}: error: {message}\n")


def build_parser() -> argparse.ArgumentParser:
    parser = RelayCtlArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True, help="relay daemon Unix socket path")
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--output", choices=("human", "json"), default="human")

    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("info", help="print relay controller information")
    subparsers.add_parser("build-info", help="print firmware build information")

    get_parser = subparsers.add_parser("get", help="get relay state")
    get_parser.add_argument("channel", nargs="?", type=_parse_channel)

    set_parser = subparsers.add_parser("set", help="set one relay on or off")
    set_parser.add_argument("channel", type=_parse_channel)
    set_parser.add_argument("state", type=_parse_on_off)

    set_all_parser = subparsers.add_parser("set-all", help="set all relay states by mask")
    set_all_parser.add_argument("state", type=_parse_state)

    pulse_parser = subparsers.add_parser("pulse", help="pulse one relay")
    pulse_parser.add_argument("channel", type=_parse_channel)
    pulse_parser.add_argument("duration_ms", type=int)

    subparsers.add_parser("off-all", help="turn every relay off")
    subparsers.add_parser("status", help="print relay controller status")
    subparsers.add_parser("reboot", help="request a firmware reboot")
    subparsers.add_parser("daemon-status", help="print daemon process status")
    return parser


def run(args: argparse.Namespace) -> int:
    try:
        payload = COMMANDS[args.command](args)
    except RelayValidationError as exc:
        print(f"argument error: {exc}", file=sys.stderr)
        return EXIT_ARGUMENT
    except RelayTimeoutError as exc:
        print(f"timeout error: {exc}", file=sys.stderr)
        return EXIT_TIMEOUT
    except RelayTransportError as exc:
        print(f"transport error: {exc}", file=sys.stderr)
        return EXIT_TRANSPORT
    except RelayProtocolError as exc:
        print(f"protocol error: {exc}", file=sys.stderr)
        return EXIT_PROTOCOL
    except RelayDeviceError as exc:
        print(f"device error: group={exc.group} rc={exc.rc}: {exc}", file=sys.stderr)
        return EXIT_DEVICE
    except RelayError as exc:
        print(f"relay error: {exc}", file=sys.stderr)
        return EXIT_PROTOCOL

    _emit(args, payload)
    return EXIT_OK


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
