"""Command-line utility for the RP2350 relay controller."""

from __future__ import annotations

import argparse
import json
import sys
from typing import Any

from rp2350_relay_6ch import (
    RelayClient,
    RelayDeviceError,
    RelayError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)
from rp2350_relay_6ch.constants import RELAY_COUNT, RELAY_MASK
from rp2350_relay_6ch.session import run_session

EXIT_OK = 0
EXIT_ARGUMENT = 2
EXIT_TRANSPORT = 3
EXIT_TIMEOUT = 4
EXIT_PROTOCOL = 5
EXIT_DEVICE = 6


def _client(args: argparse.Namespace) -> RelayClient:
    return RelayClient.connect(
        args.port,
        baudrate=args.baud,
        timeout_s=args.timeout,
        retries=args.retries,
    )


def _format_json(payload: dict[str, Any]) -> str:
    return json.dumps(payload, sort_keys=True)


def _state_channels(state: int) -> list[str]:
    return [f"CH{channel + 1}" for channel in range(RELAY_COUNT) if state & (1 << channel)]


def _format_single_channel(payload: dict[str, Any]) -> str:
    channel = int(payload["channel"])
    fields = [
        f"channel: CH{channel + 1}",
        f"on: {str(bool(payload.get('on', False))).lower()}",
    ]
    if "pulsing" in payload:
        fields.append(f"pulsing: {str(bool(payload['pulsing'])).lower()}")
    return "\n".join(fields)


def _format_relay_masks(payload: dict[str, Any]) -> str:
    state = int(payload.get("state", 0))
    pulsing = int(payload.get("pulsing", 0))
    on_channels = _state_channels(state)
    pulse_channels = _state_channels(pulsing)
    fields = [
        f"state: 0x{state:02x}",
        f"on: {', '.join(on_channels) if on_channels else 'none'}",
    ]
    if "pulsing" in payload:
        fields.append(f"pulsing: {', '.join(pulse_channels) if pulse_channels else 'none'}")
    return "\n".join(fields)


def _format_status(payload: dict[str, Any]) -> str:
    relay_keys = {"state", "pulsing"}
    transport = {key: payload[key] for key in sorted(payload) if key not in relay_keys}
    lines = ["relays:"]
    lines.extend(f"  {line}" for line in _format_relay_masks(payload).splitlines())
    if transport:
        lines.append("")
        lines.append("transport:")
        lines.extend(f"  {key}: {value}" for key, value in transport.items())
    return "\n".join(lines)


def _format_human(command: str, payload: dict[str, Any]) -> str:
    if command == "info":
        return _format_key_values(payload)

    if command == "get" and "channel" in payload:
        return _format_single_channel(payload)

    if command in {"get", "set", "set-all", "pulse", "off-all"}:
        return _format_relay_masks(payload)

    if command == "status":
        return _format_status(payload)

    if command == "build-info":
        return _format_key_values(payload)

    if command == "reboot":
        return "reboot requested"

    if command == "smoke":
        return "smoke test passed"

    return _format_json(payload)


def _format_key_values(payload: dict[str, Any]) -> str:
    lines = []
    for key in sorted(payload):
        lines.append(f"{key}: {payload[key]}")
    return "\n".join(lines)


def _emit(args: argparse.Namespace, payload: dict[str, Any]) -> None:
    if args.output == "json":
        print(_format_json(payload))
    else:
        print(_format_human(args.command, payload))


def _parse_state(value: str) -> int:
    try:
        state = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("state must be an integer mask") from exc

    if state < 0 or state & ~RELAY_MASK:
        raise argparse.ArgumentTypeError(f"state must be 0..0x{RELAY_MASK:x}")
    return state


def _parse_channel(value: str) -> int:
    try:
        channel = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("channel must be an integer") from exc

    if channel < 1 or channel > RELAY_COUNT:
        raise argparse.ArgumentTypeError(f"channel must be 1..{RELAY_COUNT}")
    return channel - 1


def _parse_on_off(value: str) -> bool:
    normalized = value.lower()
    if normalized in {"on", "1", "true", "yes"}:
        return True
    if normalized in {"off", "0", "false", "no"}:
        return False
    raise argparse.ArgumentTypeError("state must be on or off")


def _require_port(args: argparse.Namespace) -> None:
    if not args.port:
        raise RelayValidationError("--port is required")


def cmd_info(args: argparse.Namespace) -> dict[str, Any]:
    _require_port(args)
    return _client(args).get_info()


def cmd_build_info(args: argparse.Namespace) -> dict[str, Any]:
    _require_port(args)
    return _client(args).get_build_info()


def cmd_get(args: argparse.Namespace) -> dict[str, Any]:
    _require_port(args)
    return _client(args).get_relays(args.channel)


def cmd_set(args: argparse.Namespace) -> dict[str, Any]:
    _require_port(args)
    return _client(args).set_relay(args.channel, args.state)


def cmd_set_all(args: argparse.Namespace) -> dict[str, Any]:
    _require_port(args)
    return _client(args).set_all_relays(args.state)


def cmd_pulse(args: argparse.Namespace) -> dict[str, Any]:
    _require_port(args)
    return _client(args).pulse_relay(args.channel, args.duration_ms)


def cmd_off_all(args: argparse.Namespace) -> dict[str, Any]:
    _require_port(args)
    return _client(args).off_all()


def cmd_status(args: argparse.Namespace) -> dict[str, Any]:
    _require_port(args)
    return _client(args).get_status()


def cmd_reboot(args: argparse.Namespace) -> dict[str, Any]:
    _require_port(args)
    return _client(args).reboot()


def cmd_smoke(args: argparse.Namespace) -> dict[str, Any]:
    _require_port(args)
    results: list[dict[str, Any]] = []
    final: dict[str, Any] | None = None
    with _client(args) as client:
        try:
            results.append({"command": "info", "response": client.get_info()})
            results.append({"command": "status", "response": client.get_status()})
            for channel in range(RELAY_COUNT):
                label = f"CH{channel + 1}"
                results.append(
                    {
                        "command": "pulse",
                        "channel": label,
                        "response": client.pulse_relay(channel, args.pulse_ms),
                    }
                )
            final = client.off_all()
            return {"ok": True, "results": results, "final": final}
        finally:
            if final is None:
                client.off_all()


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
    "smoke": cmd_smoke,
}


class RelayArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.print_usage(sys.stderr)
        self.exit(EXIT_ARGUMENT, f"{self.prog}: error: {message}\n")


def build_parser() -> argparse.ArgumentParser:
    parser = RelayArgumentParser(description=__doc__)
    selector = parser.add_mutually_exclusive_group()
    selector.add_argument("--port", help="serial port, for example COM7 or /dev/ttyACM0")
    selector.add_argument("--serial", help="USB serial number for session device selection")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--retries", type=int, default=1)
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

    smoke_parser = subparsers.add_parser("smoke", help="pulse each relay and turn all off")
    smoke_parser.add_argument("--pulse-ms", type=int, default=100)

    session_parser = subparsers.add_parser(
        "session",
        help="open an interactive relay session",
        description="Open a long-lived relay control session.",
        epilog=(
            "examples:\n"
            "  rp2350-relay session\n"
            "  rp2350-relay --port COM7 session\n"
            "  rp2350-relay session --port COM7\n\n"
            "Normal exit and disconnect verify all relays are off. "
            "Run off-all first or use --force inside the session when you "
            "intentionally accept unknown or active relay state."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    session_selector = session_parser.add_mutually_exclusive_group()
    session_selector.add_argument("--port", dest="session_port", metavar="PORT")
    session_selector.add_argument("--serial", dest="session_serial", metavar="SERIAL")
    session_parser.add_argument("--baud", dest="session_baud", metavar="BAUD", type=int)
    session_parser.add_argument("--timeout", dest="session_timeout", metavar="TIMEOUT", type=float)
    session_parser.add_argument("--retries", dest="session_retries", metavar="RETRIES", type=int)

    return parser


def _apply_session_overrides(args: argparse.Namespace) -> None:
    if getattr(args, "session_port", None) is not None:
        args.port = args.session_port
        args.serial = None
    if getattr(args, "session_serial", None) is not None:
        args.serial = args.session_serial
        args.port = None
    for name in ("baud", "timeout", "retries"):
        value = getattr(args, f"session_{name}", None)
        if value is not None:
            setattr(args, name, value)


def run(args: argparse.Namespace) -> int:
    if args.command == "session":
        _apply_session_overrides(args)
        return run_session(args)

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
