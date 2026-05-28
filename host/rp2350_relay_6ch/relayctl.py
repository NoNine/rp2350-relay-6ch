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
from .config import resolve_socket_for_instance
from .daemon_client import RelayDaemonClient
from .exceptions import (
    RelayDeviceError,
    RelayError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)
from .smoke import run_smoke_sequence
from .systemd import doctor as systemd_doctor
from .systemd import install_user_unit


def _client(args: argparse.Namespace) -> RelayDaemonClient:
    if not args.socket and not args.instance:
        raise RelayValidationError("--socket or --instance is required")
    socket_path = args.socket
    if args.instance:
        socket_path = resolve_socket_for_instance(
            instance=args.instance,
            config_path=args.config,
            socket_override=args.socket,
        )
    return RelayDaemonClient.connect(socket_path, timeout_s=args.timeout)


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


def cmd_smoke(args: argparse.Namespace) -> dict[str, Any]:
    with _client(args) as client:
        return run_smoke_sequence(client, pulse_ms=args.pulse_ms)


def cmd_systemd_install(args: argparse.Namespace) -> dict[str, Any]:
    output = install_user_unit(
        force=args.force,
        python_path=args.python,
        print_unit=args.print_unit,
    )
    return {"message": output}


def cmd_systemd_doctor(args: argparse.Namespace) -> dict[str, Any]:
    result = systemd_doctor(instance=args.instance)
    return {"ok": result.ok, "messages": result.messages}


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
    "smoke": cmd_smoke,
    "systemd-install": cmd_systemd_install,
    "systemd-doctor": cmd_systemd_doctor,
}


class RelayCtlArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.print_usage(sys.stderr)
        self.exit(EXIT_ARGUMENT, f"{self.prog}: error: {message}\n")


def build_parser() -> argparse.ArgumentParser:
    parser = RelayCtlArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group()
    target.add_argument("--socket", help="relay daemon Unix socket path")
    target.add_argument("--instance", help="named relay instance from TOML config")
    parser.add_argument("--config", help="TOML config path for --instance")
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

    smoke_parser = subparsers.add_parser("smoke", help="pulse each relay and turn all off")
    smoke_parser.add_argument("--pulse-ms", type=int, default=1000)

    systemd_parser = subparsers.add_parser(
        "systemd",
        help="install or check daemon systemd user service files",
    )
    systemd_subparsers = systemd_parser.add_subparsers(dest="systemd_command", required=True)

    systemd_install = systemd_subparsers.add_parser("install", help="install systemd user templates")
    systemd_install.add_argument("--force", action="store_true")
    systemd_install.add_argument("--python", help="Python interpreter for the generated unit")
    systemd_install.add_argument("--print-unit", action="store_true")

    systemd_doctor_parser = systemd_subparsers.add_parser(
        "doctor",
        help="check systemd user service configuration",
    )
    systemd_doctor_parser.add_argument("--instance", help="named instance to validate")
    return parser


def run(args: argparse.Namespace) -> int:
    if args.command == "systemd":
        args.command = f"systemd-{args.systemd_command}"

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

    if args.command == "systemd-install":
        print(payload["message"])
    elif args.command == "systemd-doctor":
        print("\n".join(str(message) for message in payload["messages"]))
    else:
        _emit(args, payload)
    return EXIT_OK


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
