"""Interactive session mode for the RP2350 relay controller."""

from __future__ import annotations

import argparse
import shlex
import sys
import threading
import time
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from typing import Any, TextIO

from rp2350_relay_6ch import (
    RelayClient,
    RelayDeviceError,
    RelayError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)
from rp2350_relay_6ch.discovery import (
    RelayUsbDevice,
    list_relay_devices,
    select_device_by_serial,
)

from .constants import RELAY_COUNT

HEARTBEAT_INTERVAL_S = 5.0


@dataclass
class SessionOptions:
    port: str | None
    serial: str | None
    baud: int
    timeout: float
    retries: int


class HeartbeatPoller:
    def __init__(
        self,
        heartbeat: Callable[[], dict[str, Any]],
        *,
        interval_s: float = HEARTBEAT_INTERVAL_S,
        output: TextIO = sys.stdout,
        stop_event_factory: Callable[[], threading.Event] = threading.Event,
        thread_factory: Callable[..., threading.Thread] = threading.Thread,
        lock: threading.Lock | None = None,
    ) -> None:
        self._heartbeat = heartbeat
        self._interval_s = interval_s
        self._output = output
        self._stop = stop_event_factory()
        self._thread = thread_factory(target=self._run, daemon=True)
        self._lock = lock

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=self._interval_s + 1.0)

    def _run(self) -> None:
        while not self._stop.wait(self._interval_s):
            try:
                if self._lock is None:
                    self._heartbeat()
                else:
                    with self._lock:
                        self._heartbeat()
            except RelayError as exc:
                print(f"warning: heartbeat failed: {_error_label(exc)}: {exc}", file=self._output)


class RelaySession:
    def __init__(
        self,
        options: SessionOptions,
        *,
        input_stream: TextIO = sys.stdin,
        output: TextIO = sys.stdout,
        client_factory: Callable[..., RelayClient] = RelayClient.connect,
        discover: Callable[[], list[RelayUsbDevice]] = list_relay_devices,
        serial_selector: Callable[[str], RelayUsbDevice] = select_device_by_serial,
        heartbeat_factory: Callable[[RelayClient, TextIO], HeartbeatPoller] | None = None,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self.options = options
        self.input_stream = input_stream
        self.output = output
        self.client_factory = client_factory
        self.discover = discover
        self.serial_selector = serial_selector
        self._lock = threading.Lock()
        self.heartbeat_factory = heartbeat_factory or (
            lambda client, out: HeartbeatPoller(client.heartbeat, output=out, lock=self._lock)
        )
        self.sleep = sleep
        self.client: RelayClient | None = None
        self.heartbeat: HeartbeatPoller | None = None
        self.port = options.port
        self.usb_serial = options.serial
        self.preferred_port = options.port
        self.preferred_serial = options.serial
        self.product: str | None = None
        self.connected = False
        self.should_exit = False

    def run(self) -> int:
        if not self._connect_from_options(self.options, initial=True):
            print("disconnected: run 'connect' to select a relay controller", file=self.output)

        while not self.should_exit:
            try:
                line = self._read_input(
                    "rp2350-relay> " if self.connected else "rp2350-relay(disconnected)> "
                )
            except EOFError:
                self._handle_exit(force=False)
                break
            self.handle_line(line)
        return 0

    def handle_line(self, line: str) -> None:
        try:
            tokens = shlex.split(line)
        except ValueError as exc:
            print(f"argument error: {exc}", file=self.output)
            return

        if not tokens:
            return

        command = tokens[0]
        args = tokens[1:]

        if not self.connected and command not in {"connect", "help", "exit", "quit"}:
            print("not connected: run 'connect' first", file=self.output)
            return

        try:
            if command == "help":
                self._print_help()
            elif command == "connect":
                self._handle_connect(args)
            elif command in {"exit", "quit"}:
                self._handle_exit(force=self._parse_force(command, args))
            elif command == "disconnect":
                self._handle_disconnect(force=self._parse_force(command, args))
            elif command == "reboot":
                self._handle_reboot(args)
            else:
                self._handle_relay_command(command, args)
        except RelayValidationError as exc:
            print(f"argument error: {exc}", file=self.output)
        except RelayError as exc:
            print(_format_error(exc), file=self.output)
            if isinstance(exc, RelayTransportError) and not isinstance(exc, RelayTimeoutError):
                self._enter_disconnected()

    def _connect_from_options(
        self,
        options: SessionOptions,
        *,
        initial: bool = False,
        skip_preferred: bool = False,
    ) -> bool:
        if options.port:
            self.preferred_port = options.port
            self.preferred_serial = None
            if self._open(options.port, usb_serial=None, product=None):
                return True
            if initial:
                self._print_available_devices()
            return False
        if options.serial:
            self.preferred_serial = options.serial
            self.preferred_port = None
            try:
                device = self.serial_selector(options.serial)
            except RelayError as exc:
                print(f"{_error_label(exc)}: {exc}", file=self.output)
                if initial:
                    self._print_available_devices()
                return False
            return self._open(
                device.port,
                usb_serial=device.serial_number,
                product=device.product,
            )
        if not initial and not skip_preferred and self.preferred_port:
            if self._open(self.preferred_port, usb_serial=None, product=None):
                return True
            return self._connect_from_options(
                SessionOptions(
                    port=None,
                    serial=None,
                    baud=options.baud,
                    timeout=options.timeout,
                    retries=options.retries,
                ),
                skip_preferred=True,
            )
        if not initial and not skip_preferred and self.preferred_serial:
            try:
                device = self.serial_selector(self.preferred_serial)
            except RelayError as exc:
                print(f"{_error_label(exc)}: {exc}", file=self.output)
                return self._connect_from_options(
                    SessionOptions(
                        port=None,
                        serial=None,
                        baud=options.baud,
                        timeout=options.timeout,
                        retries=options.retries,
                    ),
                    skip_preferred=True,
                )
            return self._open(
                device.port,
                usb_serial=device.serial_number,
                product=device.product,
            )
        try:
            device = self._select_interactive_device()
        except RelayError as exc:
            print(f"{_error_label(exc)}: {exc}", file=self.output)
            return False
        if device is None:
            return False
        return self._open(device.port, usb_serial=device.serial_number, product=device.product)

    def _open(self, port: str, *, usb_serial: str | None, product: str | None) -> bool:
        if self.connected:
            print("already connected: run 'disconnect' first", file=self.output)
            return True
        try:
            client = self.client_factory(
                port,
                baudrate=self.options.baud,
                timeout_s=self.options.timeout,
                retries=self.options.retries,
            )
            with self._lock:
                info = client.get_info()
                status = client.get_status()
        except RelayError as exc:
            print(f"{_error_label(exc)}: {exc}", file=self.output)
            try:
                client.close()  # type: ignore[name-defined]
            except Exception:
                pass
            self._enter_disconnected()
            return False

        self.client = client
        self.connected = True
        self.port = port
        self.usb_serial = usb_serial
        self.product = product
        self._start_heartbeat()
        self._print_banner(info, status)
        return True

    def _select_interactive_device(self) -> RelayUsbDevice | None:
        devices = self.discover()
        if not devices:
            print("transport error: no relay USB serial devices found", file=self.output)
            return None

        self._print_devices(devices)
        while True:
            try:
                choice = self._read_input("Select device number, or q to cancel: ").strip()
            except EOFError:
                return None
            if choice.lower() in {"q", "quit", "cancel"}:
                return None
            try:
                index = int(choice, 10)
            except ValueError:
                print("argument error: selection must be a number", file=self.output)
                continue
            if 1 <= index <= len(devices):
                return devices[index - 1]
            print(f"argument error: selection must be 1..{len(devices)}", file=self.output)

    def _print_devices(self, devices: Iterable[RelayUsbDevice]) -> None:
        print("Relay controllers:", file=self.output)
        for index, device in enumerate(devices, start=1):
            serial = device.serial_number or "unknown"
            product = f" product={device.product}" if device.product else ""
            print(f"  {index}. port={device.port} serial={serial}{product}", file=self.output)

    def _print_available_devices(self) -> None:
        try:
            devices = self.discover()
        except RelayError as exc:
            print(f"{_error_label(exc)}: {exc}", file=self.output)
            return

        if devices:
            self._print_devices(devices)

    def _start_heartbeat(self) -> None:
        if self.client is None:
            return
        self.heartbeat = self.heartbeat_factory(self.client, self.output)
        self.heartbeat.start()

    def _stop_heartbeat(self) -> None:
        if self.heartbeat is not None:
            self.heartbeat.stop()
            self.heartbeat = None

    def _print_banner(self, info: dict[str, Any], status: dict[str, Any]) -> None:
        state = int(status.get("state", 0))
        pulsing = int(status.get("pulsing", 0))
        serial = self.usb_serial or "unknown"
        print(
            "connected: "
            f"port={self.port} serial={serial} "
            f"hardware={info.get('hardware', 'unknown')} "
            f"protocol={info.get('protocol_version', 'unknown')} "
            f"relays={info.get('relay_count', 'unknown')} "
            f"state=0x{state:02x} "
            f"on={_format_channels(state)} "
            f"pulsing={_format_channels(pulsing)}",
            file=self.output,
        )

    def _handle_connect(self, args: list[str]) -> None:
        if self.connected:
            print("already connected: run 'disconnect' first", file=self.output)
            return
        parser = _PromptArgParser(prog="connect", add_help=False)
        parser.add_argument("--port")
        parser.add_argument("--serial")
        parsed = _parse_prompt_args(parser, args)
        if parsed.port and parsed.serial:
            raise RelayValidationError("--port and --serial are mutually exclusive")
        options = SessionOptions(
            port=parsed.port,
            serial=parsed.serial,
            baud=self.options.baud,
            timeout=self.options.timeout,
            retries=self.options.retries,
        )
        self._connect_from_options(options)

    def _handle_exit(self, *, force: bool) -> None:
        if self.connected and not self._close_allowed(force):
            return
        if self.connected:
            self._close_current(force=force)
        self.should_exit = True

    def _handle_disconnect(self, *, force: bool) -> None:
        if not self.connected:
            print("not connected", file=self.output)
            return
        if not self._close_allowed(force):
            return
        self._close_current(force=force)
        print("disconnected", file=self.output)

    def _handle_reboot(self, args: list[str]) -> None:
        if args:
            raise RelayValidationError("reboot takes no arguments")
        client = self._require_client()
        with self._lock:
            payload = client.reboot()
        print("reboot requested", file=self.output)
        reboot_serial = self.usb_serial
        self._close_current(force=True, warn=False)
        if reboot_serial:
            for _ in range(6):
                try:
                    device = self.serial_selector(reboot_serial)
                except RelayError:
                    self.sleep(1.0)
                    continue
                if self._open(
                    device.port,
                    usb_serial=device.serial_number,
                    product=device.product,
                ):
                    return
            print("reboot reconnect failed; run 'connect' to reconnect", file=self.output)
        else:
            print("reboot closed the connection; run 'connect' to reconnect", file=self.output)
        self._enter_disconnected()
        del payload

    def _handle_relay_command(self, command: str, args: list[str]) -> None:
        client = self._require_client()
        with self._lock:
            if command == "info":
                _expect_arg_count(command, args, 0)
                payload = client.get_info()
            elif command == "build-info":
                _expect_arg_count(command, args, 0)
                payload = client.get_build_info()
            elif command == "get":
                if len(args) > 1:
                    raise RelayValidationError("get takes zero or one channel")
                channel = _parse_channel_arg(args[0]) if args else None
                payload = client.get_relays(channel)
            elif command == "set":
                _expect_arg_count(command, args, 2)
                payload = client.set_relay(
                    _parse_channel_arg(args[0]), _parse_on_off_arg(args[1])
                )
            elif command == "set-all":
                _expect_arg_count(command, args, 1)
                payload = client.set_all_relays(_parse_state_arg(args[0]))
            elif command == "pulse":
                _expect_arg_count(command, args, 2)
                payload = client.pulse_relay(_parse_channel_arg(args[0]), _parse_int_arg(args[1]))
            elif command == "off-all":
                _expect_arg_count(command, args, 0)
                payload = client.off_all()
            elif command == "status":
                _expect_arg_count(command, args, 0)
                payload = client.get_status()
            else:
                raise RelayValidationError(f"unknown command {command}")
        self._print_result(command, payload)

    def _close_allowed(self, force: bool) -> bool:
        if force:
            return True
        try:
            with self._lock:
                status = self._require_client().get_status()
            state = int(status["state"])
            pulsing = int(status.get("pulsing", 0))
        except Exception as exc:
            print(
                f"refusing to close: relay state is unknown ({_error_label(exc)}: {exc})",
                file=self.output,
            )
            print("run 'off-all' first or use --force", file=self.output)
            return False
        if state or pulsing:
            print(
                "refusing to close: "
                f"on={_format_channels(state)} pulsing={_format_channels(pulsing)}",
                file=self.output,
            )
            print("run 'off-all' first or use --force", file=self.output)
            return False
        return True

    def _close_current(self, *, force: bool, warn: bool = True) -> None:
        if force and warn:
            print("warning: closing without confirmed all-off state", file=self.output)
        self._stop_heartbeat()
        client = self.client
        self.client = None
        self.connected = False
        if client is not None:
            client.close()

    def _enter_disconnected(self) -> None:
        self._stop_heartbeat()
        client = self.client
        self.client = None
        self.connected = False
        if client is not None:
            client.close()

    def _require_client(self) -> RelayClient:
        if self.client is None:
            raise RelayTransportError("not connected")
        return self.client

    def _print_result(self, command: str, payload: dict[str, Any]) -> None:
        from rp2350_relay_6ch.cli import _format_human

        print(_format_human(command, payload), file=self.output)

    def _parse_force(self, command: str, args: list[str]) -> bool:
        parser = _PromptArgParser(prog=command, add_help=False)
        parser.add_argument("--force", action="store_true")
        parsed = _parse_prompt_args(parser, args)
        return bool(parsed.force)

    def _print_help(self) -> None:
        print(
            "commands: info, build-info, get [channel], set <channel> <on|off>, "
            "set-all <mask>, pulse <channel> <duration-ms>, off-all, status, "
            "reboot, connect, disconnect [--force], help, exit [--force], "
            "quit [--force]",
            file=self.output,
        )

    def _read_input(self, prompt: str) -> str:
        if self.input_stream is sys.stdin and self.output is sys.stdout:
            return input(prompt)
        print(prompt, end="", file=self.output, flush=True)
        line = self.input_stream.readline()
        if line == "":
            raise EOFError
        return line.rstrip("\n")


class _PromptArgParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise RelayValidationError(message)


def run_session(args: argparse.Namespace) -> int:
    session = RelaySession(
        SessionOptions(
            port=args.port,
            serial=args.serial,
            baud=args.baud,
            timeout=args.timeout,
            retries=args.retries,
        )
    )
    return session.run()


def _parse_prompt_args(parser: argparse.ArgumentParser, args: list[str]) -> argparse.Namespace:
    try:
        return parser.parse_args(args)
    except SystemExit as exc:
        raise RelayValidationError(str(exc)) from exc


def _expect_arg_count(command: str, args: list[str], count: int) -> None:
    if len(args) != count:
        raise RelayValidationError(f"{command} takes {count} argument(s)")


def _parse_int_arg(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:
        raise RelayValidationError("value must be an integer") from exc


def _parse_channel_arg(value: str) -> int:
    channel = _parse_int_arg(value)
    if channel < 1 or channel > RELAY_COUNT:
        raise RelayValidationError(f"channel must be 1..{RELAY_COUNT}")
    return channel - 1


def _parse_state_arg(value: str) -> int:
    from rp2350_relay_6ch.cli import _parse_state

    try:
        return _parse_state(value)
    except argparse.ArgumentTypeError as exc:
        raise RelayValidationError(str(exc)) from exc


def _parse_on_off_arg(value: str) -> bool:
    from rp2350_relay_6ch.cli import _parse_on_off

    try:
        return _parse_on_off(value)
    except argparse.ArgumentTypeError as exc:
        raise RelayValidationError(str(exc)) from exc


def _format_channels(mask: int) -> str:
    channels = [f"CH{channel + 1}" for channel in range(RELAY_COUNT) if mask & (1 << channel)]
    return ", ".join(channels) if channels else "none"


def _error_label(exc: BaseException) -> str:
    if isinstance(exc, RelayValidationError):
        return "argument error"
    if isinstance(exc, RelayTimeoutError):
        return "timeout error"
    if isinstance(exc, RelayTransportError):
        return "transport error"
    if isinstance(exc, RelayProtocolError):
        return "protocol error"
    if isinstance(exc, RelayDeviceError):
        return "device error"
    if isinstance(exc, RelayError):
        return "relay error"
    return "error"


def _format_error(exc: RelayError) -> str:
    if isinstance(exc, RelayDeviceError):
        return f"device error: group={exc.group} rc={exc.rc}: {exc}"
    return f"{_error_label(exc)}: {exc}"
