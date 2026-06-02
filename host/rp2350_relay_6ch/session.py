"""Interactive session mode for the RP2350 relay controller."""

from __future__ import annotations

import argparse
import shlex
import sys
import threading
import time
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from enum import Enum
from typing import Any, TextIO

from prompt_toolkit import prompt as prompt_toolkit_prompt
from prompt_toolkit.completion import Completer, Completion
from prompt_toolkit.formatted_text import ANSI
from prompt_toolkit.history import InMemoryHistory
from prompt_toolkit.patch_stdout import patch_stdout

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

from .constants import COMMAND_MODEL_VERSION, PROTOCOL_VERSION, RELAY_COUNT
from .smoke import run_smoke_sequence

HEARTBEAT_INTERVAL_S = 2.5
REBOOT_RECONNECT_SETTLE_S = 1.0
REBOOT_RECONNECT_ATTEMPTS = 6
ANSI_GREEN = "\033[32m"
ANSI_BLUE = "\033[34m"
ANSI_RESET = "\033[0m"
STARTUP_BOX_MIN_WIDTH = 60
CONNECTED_COMMANDS = (
    "identity",
    "capabilities",
    "build-info",
    "get",
    "set",
    "set-all",
    "pulse",
    "off-all",
    "status",
    "health",
    "transport",
    "safety",
    "watchdog",
    "smoke",
    "reboot",
    "disconnect",
    "connect",
    "help",
    "exit",
    "quit",
)
DISCONNECTED_COMMANDS = ("connect", "help", "exit", "quit")


@dataclass
class SessionOptions:
    port: str | None
    serial: str | None
    baud: int
    timeout: float
    retries: int


@dataclass
class _ConnectionProbe:
    client: RelayClient
    identity: dict[str, Any]
    status: dict[str, Any]


class _ConnectResult(Enum):
    CONNECTED = "connected"
    FAILED = "failed"
    CANCELLED = "cancelled"


_SELECTION_CANCELLED = object()


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
        self._failed = False

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
                print(_format_heartbeat_error(exc), file=self._output)
                self._failed = True
            else:
                if self._failed:
                    print("heartbeat: restored", file=self._output)
                    self._failed = False


class RelaySessionCompleter(Completer):
    def __init__(self, connected: Callable[[], bool]) -> None:
        self._connected = connected

    def complete(self, text: str) -> list[str]:
        _prefix, suggestions = self._suggestions(text)
        return suggestions

    def get_completions(self, document: Any, complete_event: Any) -> Iterable[Any]:
        del complete_event
        prefix, suggestions = self._suggestions(document.text_before_cursor)
        start_position = -len(prefix) if prefix else 0
        for suggestion in suggestions:
            yield Completion(suggestion, start_position=start_position)

    def _suggestions(self, text: str) -> tuple[str, list[str]]:
        parsed = _completion_context(text)
        if parsed is None:
            return "", []
        tokens, prefix = parsed
        if not tokens:
            return prefix, _matching(self._commands(), prefix)

        command = tokens[0]
        if command not in self._commands():
            return prefix, []

        args = tokens[1:]
        if command == "connect":
            return prefix, _matching(("--port", "--serial"), prefix)
        if command in {"disconnect", "exit", "quit"}:
            return prefix, _matching(("--force",), prefix)
        if command in {"get", "set", "pulse"} and len(args) == 0:
            return prefix, _matching(_channel_labels(), prefix)
        if command == "set" and len(args) == 1:
            return prefix, _matching(("on", "off"), prefix)
        return prefix, []

    def _commands(self) -> tuple[str, ...]:
        return CONNECTED_COMMANDS if self._connected() else DISCONNECTED_COMMANDS


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
        self.completer = RelaySessionCompleter(lambda: self.connected)
        self.history = InMemoryHistory()

    def run(self) -> int:
        startup_result = self._connect_from_options(self.options, initial=True)
        if startup_result is _ConnectResult.CANCELLED:
            return 0
        if startup_result is _ConnectResult.FAILED:
            print("disconnected: run 'connect' to select a relay controller", file=self.output)

        while not self.should_exit:
            try:
                line = self._read_input(self._prompt(color=self._prompt_color_enabled()))
            except KeyboardInterrupt:
                print("^C", file=self.output)
                continue
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
    ) -> _ConnectResult:
        if options.port:
            self.preferred_port = options.port
            self.preferred_serial = None
            device = self._metadata_for_port(options.port)
            if self._open(
                options.port,
                usb_serial=device.serial_number if device else None,
                product=device.product if device else None,
            ):
                return _ConnectResult.CONNECTED
            if initial:
                self._print_available_devices()
            return _ConnectResult.FAILED
        if options.serial:
            self.preferred_serial = options.serial
            self.preferred_port = None
            try:
                device = self.serial_selector(options.serial)
            except RelayError as exc:
                print(f"{_error_label(exc)}: {exc}", file=self.output)
                if initial:
                    self._print_available_devices()
                return _ConnectResult.FAILED
            if self._open(
                device.port,
                usb_serial=device.serial_number,
                product=device.product,
            ):
                return _ConnectResult.CONNECTED
            return _ConnectResult.FAILED
        if not initial and not skip_preferred and self.preferred_port:
            device = self._metadata_for_port(self.preferred_port)
            if self._open(
                self.preferred_port,
                usb_serial=device.serial_number if device else None,
                product=device.product if device else None,
            ):
                return _ConnectResult.CONNECTED
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
            if self._open(
                device.port,
                usb_serial=device.serial_number,
                product=device.product,
            ):
                return _ConnectResult.CONNECTED
            return _ConnectResult.FAILED
        try:
            device = self._select_interactive_device()
        except RelayError as exc:
            print(f"{_error_label(exc)}: {exc}", file=self.output)
            return _ConnectResult.FAILED
        if device is _SELECTION_CANCELLED:
            return _ConnectResult.CANCELLED if initial else _ConnectResult.FAILED
        if device is None:
            return _ConnectResult.FAILED
        if self._open(device.port, usb_serial=device.serial_number, product=device.product):
            return _ConnectResult.CONNECTED
        return _ConnectResult.FAILED

    def _metadata_for_port(self, port: str) -> RelayUsbDevice | None:
        try:
            devices = self.discover()
        except RelayError:
            return None
        for device in devices:
            if device.port == port:
                return device
        return None

    def _open(self, port: str, *, usb_serial: str | None, product: str | None) -> bool:
        if self.connected:
            print("already connected: run 'disconnect' first", file=self.output)
            return True
        probe = self._probe_connection(port, quiet=False)
        if probe is None:
            self._enter_disconnected()
            return False

        self._adopt_connection(
            probe,
            port=port,
            usb_serial=usb_serial,
            product=product,
        )
        return True

    def _probe_connection(self, port: str, *, quiet: bool) -> _ConnectionProbe | None:
        try:
            client = self.client_factory(
                port,
                baudrate=self.options.baud,
                timeout_s=self.options.timeout,
                retries=self.options.retries,
            )
            with self._lock:
                identity = client.identity()
                _validate_readiness_identity(identity)
                status = client.status()
        except RelayError as exc:
            if not quiet:
                print(f"{_error_label(exc)}: {exc}", file=self.output)
            try:
                client.close()  # type: ignore[name-defined]
            except Exception:
                pass
            return None

        return _ConnectionProbe(client=client, identity=identity, status=status)

    def _adopt_connection(
        self,
        probe: _ConnectionProbe,
        *,
        port: str,
        usb_serial: str | None,
        product: str | None,
    ) -> None:
        self.client = probe.client
        self.connected = True
        self.port = port
        self.usb_serial = usb_serial
        self.product = product
        self._start_heartbeat()
        self._print_banner(probe.identity, probe.status)

    def _close_probe(self, probe: _ConnectionProbe) -> None:
        try:
            probe.client.close()
        except Exception:
            pass

    def _is_reboot_reconnect_fresh(
        self,
        pre_reboot_status: dict[str, Any],
        post_reboot_status: dict[str, Any],
    ) -> bool:
        pre_uptime = _status_uptime_ms(pre_reboot_status)
        post_uptime = _status_uptime_ms(post_reboot_status)
        if pre_uptime is None or post_uptime is None:
            return True
        return post_uptime < pre_uptime

    def _select_interactive_device(self) -> RelayUsbDevice | None | object:
        devices = self.discover()
        if not devices:
            print("transport error: no relay USB serial devices found", file=self.output)
            return None

        self._print_devices(devices)
        while True:
            try:
                choice = self._read_input("Select device number, or q to cancel: ").strip()
            except EOFError:
                return _SELECTION_CANCELLED
            if choice.lower() in {"q", "quit", "cancel"}:
                return _SELECTION_CANCELLED
            try:
                index = int(choice, 10)
            except ValueError:
                print("argument error: selection must be a number", file=self.output)
                continue
            if 1 <= index <= len(devices):
                return devices[index - 1]
            print(f"argument error: selection must be 1..{len(devices)}", file=self.output)

    def _print_devices(self, devices: Iterable[RelayUsbDevice]) -> None:
        print("Relay USB candidates:", file=self.output)
        for index, device in enumerate(devices, start=1):
            serial = device.serial_number or "unknown"
            if device.product:
                product = f"product={device.product}"
            else:
                product = "product=unknown status=unverified"
            print(
                f"  {index}. port={device.port} serial={serial} {product}",
                file=self.output,
            )

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

    def _print_banner(self, identity: dict[str, Any], status: dict[str, Any]) -> None:
        state = int(status.get("state", 0))
        pulsing = int(status.get("pulsing", 0))
        serial = self.usb_serial or "unknown"
        print(
            _format_startup_box(
                [
                    ("Connection", "connected"),
                    ("Port", self.port),
                    ("Serial", serial),
                    ("Hardware", identity.get("hardware", "unknown")),
                    ("Protocol", identity.get("protocol_version", "unknown")),
                    ("Relay count", identity.get("relay_count", "unknown")),
                    ("State", f"0x{state:02x}"),
                    ("On", _format_channels(state)),
                    ("Pulsing", _format_channels(pulsing)),
                ]
            ),
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
            pre_reboot_status = client.status()
            payload = client.reboot()
        print("reboot requested", file=self.output)
        reboot_serial = self.usb_serial
        self._close_current(force=True, warn=False)
        if reboot_serial:
            self.sleep(REBOOT_RECONNECT_SETTLE_S)
            for _ in range(REBOOT_RECONNECT_ATTEMPTS):
                try:
                    device = self.serial_selector(reboot_serial)
                except RelayError:
                    self.sleep(1.0)
                    continue
                probe = self._probe_connection(device.port, quiet=True)
                if probe is None:
                    self.sleep(1.0)
                    continue
                if self._is_reboot_reconnect_fresh(pre_reboot_status, probe.status):
                    self._adopt_connection(
                        probe,
                        port=device.port,
                        usb_serial=device.serial_number,
                        product=device.product,
                    )
                    return
                self._close_probe(probe)
                self.sleep(1.0)
            print("reboot reconnect failed; run 'connect' to reconnect", file=self.output)
        else:
            print("reboot closed the connection; run 'connect' to reconnect", file=self.output)
        self._enter_disconnected()
        del payload

    def _handle_relay_command(self, command: str, args: list[str]) -> None:
        client = self._require_client()
        with self._lock:
            if command == "identity":
                _expect_arg_count(command, args, 0)
                payload = client.identity()
            elif command == "capabilities":
                _expect_arg_count(command, args, 0)
                payload = client.capabilities()
            elif command == "build-info":
                _expect_arg_count(command, args, 0)
                payload = client.build_info()
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
                payload = client.status()
            elif command == "health":
                _expect_arg_count(command, args, 0)
                payload = client.health()
            elif command == "transport":
                _expect_arg_count(command, args, 0)
                payload = client.transport_status()
            elif command == "safety":
                _expect_arg_count(command, args, 0)
                payload = client.safety()
            elif command == "watchdog":
                _expect_arg_count(command, args, 0)
                payload = client.watchdog()
            elif command == "smoke":
                parser = _PromptArgParser(prog=command, add_help=False)
                parser.add_argument("--pulse-ms", type=int, default=1000)
                parsed = _parse_prompt_args(parser, args)
                payload = run_smoke_sequence(client, pulse_ms=parsed.pulse_ms, sleep=self.sleep)
            else:
                raise RelayValidationError(f"unknown command {command}")
        self._print_result(command, payload)

    def _close_allowed(self, force: bool) -> bool:
        if force:
            return True
        try:
            with self._lock:
                status = self._require_client().status()
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
            "Inspect:\n"
            "  status                       show relay state, transport counters, and last error\n"
            "  get                          show all relay states\n"
            "  get <channel>                show one relay state\n"
            "  identity                     show controller hardware and protocol information\n"
            "  capabilities                 show controller capabilities\n"
            "  build-info                   show firmware build details\n"
            "  health                       show health details\n"
            "  transport                    show transport details\n"
            "  safety                       show safety policy details\n"
            "  watchdog                     show watchdog details\n"
            "\n"
            "Control:\n"
            "  set <channel> <on|off>       set one relay\n"
            "  set-all <mask>               set all relays from a six-bit mask\n"
            "  pulse <channel> <duration>   pulse one relay for duration in ms\n"
            "  off-all                      turn every relay off and cancel pulses\n"
            "  smoke [--pulse-ms <ms>]      pulse each relay and turn all off\n"
            "\n"
            "Connection:\n"
            "  connect                      connect using saved selector or discovery\n"
            "  connect --port <port>        connect to a serial port\n"
            "  connect --serial <serial>    connect by USB serial number\n"
            "  disconnect                   close only when relays are confirmed off\n"
            "  disconnect --force           close without confirmed all-off state\n"
            "\n"
            "Exit:\n"
            "  exit                         exit only when relays are confirmed off\n"
            "  quit                         same as exit\n"
            "  exit --force                 exit without confirmed all-off state\n"
            "  quit --force                 same as exit --force\n"
            "\n"
            "Notes:\n"
            "  Channels are board labels: 1 is CH1, 6 is CH6.\n"
            "  Run off-all before disconnecting or exiting.\n"
            "  Use --force only when you intentionally accept unknown or active relay state.",
            file=self.output,
        )

    def _prompt(self, *, color: bool = False) -> str:
        state = str(self.port) if self.connected else "disconnected"
        return _format_prompt(state, color=color)

    def _prompt_color_enabled(self) -> bool:
        return (
            self.input_stream is sys.stdin
            and self.output is sys.stdout
            and self.output.isatty()
        )

    def _read_input(self, prompt: str) -> str:
        if self.input_stream is sys.stdin and self.output is sys.stdout:
            if self._prompt_toolkit_enabled():
                with patch_stdout():
                    return prompt_toolkit_prompt(
                        ANSI(prompt),
                        completer=self.completer,
                        history=self.history,
                    )
            return input(prompt)
        print(prompt, end="", file=self.output, flush=True)
        line = self.input_stream.readline()
        if line == "":
            raise EOFError
        return line.rstrip("\n")

    def _prompt_toolkit_enabled(self) -> bool:
        return (
            self.input_stream is sys.stdin
            and self.output is sys.stdout
            and self.input_stream.isatty()
            and self.output.isatty()
        )


def _format_prompt(state: str, *, color: bool = False) -> str:
    if color:
        return f"{ANSI_GREEN}rp2350-relay{ANSI_RESET}[{ANSI_BLUE}{state}{ANSI_RESET}]$ "
    return f"rp2350-relay[{state}]$ "


def _completion_context(text: str) -> tuple[list[str], str] | None:
    try:
        if text and text[-1].isspace():
            return shlex.split(text), ""
        tokens = shlex.split(text)
    except ValueError:
        return None
    if not tokens:
        return [], ""
    return tokens[:-1], tokens[-1]


def _matching(values: Iterable[str], prefix: str) -> list[str]:
    return [value for value in values if value.startswith(prefix)]


def _channel_labels() -> tuple[str, ...]:
    return tuple(str(channel) for channel in range(1, RELAY_COUNT + 1))


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


def _status_uptime_ms(status: dict[str, Any]) -> int | None:
    uptime = status.get("uptime_ms")
    if isinstance(uptime, bool):
        return None
    if isinstance(uptime, int):
        return uptime
    return None


def _validate_readiness_identity(identity: dict[str, Any]) -> None:
    if identity.get("protocol_version") != PROTOCOL_VERSION:
        raise RelayProtocolError(
            f"unexpected relay protocol version {identity.get('protocol_version')}"
        )
    if identity.get("command_model_version") != COMMAND_MODEL_VERSION:
        raise RelayProtocolError(
            f"unexpected relay command model version {identity.get('command_model_version')}"
        )


def _format_channels(mask: int) -> str:
    channels = [f"CH{channel + 1}" for channel in range(RELAY_COUNT) if mask & (1 << channel)]
    return ", ".join(channels) if channels else "none"


def _format_startup_box(rows: list[tuple[str, object]]) -> str:
    label_width = max(len(label) for label, _value in rows)
    content_rows = ["  RP2350 Relay Session"]
    content_rows.append("")
    content_rows.extend(
        f"  {label + ':':<{label_width + 1}}  {value}" for label, value in rows
    )
    inner_width = max(STARTUP_BOX_MIN_WIDTH, max(len(row) for row in content_rows))
    lines = [f"╭{'─' * (inner_width + 2)}╮"]
    lines.extend(f"│ {row:<{inner_width}} │" for row in content_rows)
    lines.append(f"╰{'─' * (inner_width + 2)}╯")
    return "\n".join(lines)


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


def _format_heartbeat_error(exc: RelayError) -> str:
    if isinstance(exc, RelayTimeoutError):
        return "heartbeat: no response"
    if isinstance(exc, RelayTransportError):
        return "heartbeat: serial link unavailable"
    return f"heartbeat: {_error_label(exc)}"
