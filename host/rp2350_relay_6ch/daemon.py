"""Foreground Unix-socket daemon for one RP2350 relay controller."""

from __future__ import annotations

import argparse
import json
import logging
import os
import queue
import selectors
import signal
import socket
import stat
import sys
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from . import __version__
from .client import RelayClient
from .config import resolve_instance_config
from .constants import (
    HARDWARE_NAME,
    PROTOCOL_VERSION,
    PULSE_MAX_MS,
    PULSE_MIN_MS,
    RELAY_COUNT,
    RELAY_MASK,
)
from .daemon_client import RelayDaemonClient
from .discovery import RelayUsbDevice, select_device_by_serial
from .exceptions import (
    RelayDeviceError,
    RelayError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)

MAX_FRAME_BYTES = 4096
HEARTBEAT_INTERVAL_S = 2.5
EXIT_OK = 0
EXIT_ARGUMENT = 2
EXIT_TRANSPORT = 3
EXIT_PROTOCOL = 5
EXIT_DAEMON = 7
DEVICE_COMMANDS = {
    "info",
    "build-info",
    "get",
    "set",
    "set-all",
    "pulse",
    "off-all",
    "status",
    "reboot",
}
COMMANDS = DEVICE_COMMANDS | {"daemon-status"}
LOG = logging.getLogger(__name__)


@dataclass(frozen=True)
class DaemonConfig:
    selector_type: str
    selector_value: str
    socket_path: str
    baud: int = 115200
    timeout: float = 2.0
    retries: int = 1
    reconnect_interval: float = 1.0
    heartbeat_interval: float = HEARTBEAT_INTERVAL_S
    wait_device: bool = False


@dataclass
class _Request:
    request_id: str | int | None
    command: str | None
    args: dict[str, Any]
    responder: Callable[[dict[str, Any]], None]
    accepted_order: int


@dataclass
class _PendingLine:
    data: bytes
    responder: Callable[[dict[str, Any]], None]
    accepted_order: int


@dataclass
class _ConnectionProbe:
    client: RelayClient
    port: str


def response_ok(request_id: str | int | None, result: dict[str, Any]) -> dict[str, Any]:
    return {"id": request_id, "ok": True, "result": result}


def response_error(
    request_id: str | int | None,
    kind: str,
    message: str,
    *,
    group: int | None = None,
    rc: int | None = None,
) -> dict[str, Any]:
    error: dict[str, Any] = {"kind": kind, "message": message}
    if group is not None:
        error["group"] = group
    if rc is not None:
        error["rc"] = rc
    return {"id": request_id, "ok": False, "error": error}


def parse_request_line(line: bytes, accepted_order: int) -> _Request:
    request_id: str | int | None = None
    try:
        decoded = line.decode()
        payload = json.loads(decoded)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RelayProtocolError(f"malformed JSON: {exc}") from exc

    if not isinstance(payload, dict):
        raise RelayValidationError("request must be a JSON object")
    request_id = payload.get("id")
    if not isinstance(request_id, (str, int)) or isinstance(request_id, bool):
        raise RelayValidationError("id is required and must be a string or integer")
    command = payload.get("command")
    if not isinstance(command, str):
        raise RelayValidationError("command is required")
    if command not in COMMANDS:
        raise RelayValidationError(f"unknown command {command}")
    args = payload.get("args", {})
    if args is None:
        args = {}
    if not isinstance(args, dict):
        raise RelayValidationError("args must be an object")
    _validate_args(command, args)
    return _Request(
        request_id=request_id,
        command=command,
        args=args,
        responder=lambda response: None,
        accepted_order=accepted_order,
    )


def _validate_args(command: str, args: dict[str, Any]) -> None:
    if command in {"info", "build-info", "off-all", "status", "reboot", "daemon-status"}:
        if args:
            raise RelayValidationError(f"{command} does not accept args")
        return

    if command == "get":
        if set(args) - {"channel"}:
            raise RelayValidationError("get accepts only channel")
        if "channel" in args:
            _validate_channel(args["channel"])
        return

    if command == "set":
        _require_keys(args, {"channel", "on"}, command)
        _validate_channel(args["channel"])
        if not isinstance(args["on"], bool):
            raise RelayValidationError("on must be a bool")
        return

    if command == "set-all":
        _require_keys(args, {"state"}, command)
        state = args["state"]
        if not isinstance(state, int) or isinstance(state, bool) or state < 0 or state & ~RELAY_MASK:
            raise RelayValidationError(f"state must be 0..0x{RELAY_MASK:x}")
        return

    if command == "pulse":
        _require_keys(args, {"channel", "duration_ms"}, command)
        _validate_channel(args["channel"])
        duration = args["duration_ms"]
        if (
            not isinstance(duration, int)
            or isinstance(duration, bool)
            or duration < PULSE_MIN_MS
            or duration > PULSE_MAX_MS
        ):
            raise RelayValidationError(f"duration_ms must be {PULSE_MIN_MS}..{PULSE_MAX_MS}")


def _require_keys(args: dict[str, Any], keys: set[str], command: str) -> None:
    actual = set(args)
    if actual != keys:
        missing = sorted(keys - actual)
        extra = sorted(actual - keys)
        parts = []
        if missing:
            parts.append(f"missing {', '.join(missing)}")
        if extra:
            parts.append(f"unexpected {', '.join(extra)}")
        raise RelayValidationError(f"{command} args invalid: {'; '.join(parts)}")


def _validate_channel(channel: Any) -> None:
    if not isinstance(channel, int) or isinstance(channel, bool) or channel < 0 or channel >= RELAY_COUNT:
        raise RelayValidationError(f"channel must be 0..{RELAY_COUNT - 1}")


def error_response_for_exception(
    request_id: str | int | None,
    exc: BaseException,
) -> dict[str, Any]:
    if isinstance(exc, RelayValidationError):
        return response_error(request_id, "validation", str(exc))
    if isinstance(exc, RelayTimeoutError):
        return response_error(request_id, "timeout", str(exc))
    if isinstance(exc, RelayTransportError):
        return response_error(request_id, "transport", str(exc))
    if isinstance(exc, RelayProtocolError):
        return response_error(request_id, "protocol", str(exc))
    if isinstance(exc, RelayDeviceError):
        return response_error(request_id, "device", str(exc), group=exc.group, rc=exc.rc)
    return response_error(request_id, "daemon", str(exc))


class RelayDaemon:
    def __init__(
        self,
        config: DaemonConfig,
        *,
        client_factory: Callable[..., RelayClient] = RelayClient.connect,
        serial_selector: Callable[[str], RelayUsbDevice] = select_device_by_serial,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self.config = config
        self.client_factory = client_factory
        self.serial_selector = serial_selector
        self.sleep = sleep
        self.client: RelayClient | None = None
        self.connected = False
        self.current_port: str | None = None
        self.reconnect_attempts = 0
        self.last_error: str | None = None
        self._server: socket.socket | None = None
        self._selector = selectors.DefaultSelector()
        self._requests: queue.Queue[_Request | None] = queue.Queue()
        self._pending_lines: queue.Queue[_PendingLine | None] = queue.Queue()
        self._stop = threading.Event()
        self._state_lock = threading.Lock()
        self._command_lock = threading.Lock()
        self._order = 0
        self._threads: list[threading.Thread] = []
        self._fatal_error: RelayError | None = None

    def run(self) -> int:
        try:
            self._initial_connect()
            self._bind_socket()
            self._start_threads()
            LOG.info("relay daemon listening on %s", self.config.socket_path)
            self._accept_loop()
        finally:
            self.shutdown()
        if self._fatal_error is not None:
            raise self._fatal_error
        return EXIT_OK

    def shutdown(self) -> None:
        self._stop.set()
        server = self._server
        if server is not None:
            try:
                server.close()
            except OSError:
                pass
        self._requests.put(None)
        self._pending_lines.put(None)
        for thread in self._threads:
            thread.join(timeout=max(self.config.timeout, 0.1) + 1.0)
        with self._command_lock:
            client = self.client
            if client is not None:
                try:
                    client.off_all()
                    LOG.info("shutdown off-all completed")
                except RelayError as exc:
                    LOG.warning("shutdown off-all failed: %s", exc)
                try:
                    client.close()
                except Exception as exc:  # pragma: no cover - defensive cleanup
                    LOG.warning("serial close failed: %s", exc)
            self.client = None
            self.connected = False
            self.current_port = None
        try:
            if os.path.exists(self.config.socket_path):
                os.unlink(self.config.socket_path)
        except OSError as exc:
            LOG.warning("failed to remove socket %s: %s", self.config.socket_path, exc)

    def install_signal_handlers(self) -> None:
        def handle_signal(signum: int, frame: object) -> None:
            del frame
            LOG.info("received signal %s, shutting down", signum)
            self._stop.set()
            if self._server is not None:
                try:
                    self._server.close()
                except OSError:
                    pass

        signal.signal(signal.SIGINT, handle_signal)
        signal.signal(signal.SIGTERM, handle_signal)

    def get_daemon_status(self) -> dict[str, Any]:
        with self._state_lock:
            return {
                "connected": self.connected,
                "selector_type": self.config.selector_type,
                "selector_value": self.config.selector_value,
                "current_port": self.current_port,
                "socket_path": self.config.socket_path,
                "reconnect_attempts": self.reconnect_attempts,
                "last_error": self.last_error,
                "daemon_version": __version__,
            }

    def _start_threads(self) -> None:
        for target, name in (
            (self._parser_loop, "relay-daemon-parser"),
            (self._worker_loop, "relay-daemon-worker"),
            (self._reconnect_loop, "relay-daemon-reconnect"),
            (self._heartbeat_loop, "relay-daemon-heartbeat"),
        ):
            thread = threading.Thread(target=target, name=name, daemon=True)
            thread.start()
            self._threads.append(thread)

    def _initial_connect(self) -> None:
        try:
            self._connect_ready()
        except RelayValidationError as exc:
            if self.config.wait_device and _is_missing_selected_device(exc):
                self._mark_disconnected(str(exc), count_attempt=True)
                LOG.info("starting disconnected: %s", exc)
                return
            raise
        except RelayProtocolError:
            raise
        except RelayTransportError as exc:
            if not self.config.wait_device:
                raise
            self._mark_disconnected(str(exc), count_attempt=True)
            LOG.info("starting disconnected: %s", exc)
        except RelayError as exc:
            if not self.config.wait_device:
                raise RelayTransportError(str(exc)) from exc
            self._mark_disconnected(str(exc), count_attempt=True)
            LOG.info("starting disconnected: %s", exc)

    def _connect_ready(self) -> None:
        port = self._selected_port()
        client = self.client_factory(
            port,
            baudrate=self.config.baud,
            timeout_s=self.config.timeout,
            retries=self.config.retries,
        )
        try:
            info = client.get_info()
            _validate_readiness_info(info)
            client.get_status()
        except Exception:
            client.close()
            raise

        with self._state_lock:
            old_client = self.client
            self.client = client
            self.connected = True
            self.current_port = port
            self.reconnect_attempts = 0
            self.last_error = None
        if old_client is not None:
            old_client.close()
        LOG.info("relay controller ready on %s", port)

    def _selected_port(self) -> str:
        if self.config.selector_type == "port":
            return self.config.selector_value
        device = self.serial_selector(self.config.selector_value)
        return device.port

    def _mark_disconnected(self, message: str, *, count_attempt: bool = False) -> None:
        with self._state_lock:
            client = self.client
            self.client = None
            self.connected = False
            self.current_port = None
            if count_attempt:
                self.reconnect_attempts += 1
            self.last_error = message
        if client is not None:
            try:
                client.close()
            except Exception:
                pass

    def _reconnect_loop(self) -> None:
        while not self._stop.wait(self.config.reconnect_interval):
            if self.connected:
                continue
            try:
                self._connect_ready()
            except RelayValidationError as exc:
                if _is_missing_selected_device(exc):
                    self._mark_disconnected(str(exc), count_attempt=True)
                    LOG.info("reconnect attempt failed: %s", exc)
                    continue
                LOG.error("configuration error during reconnect: %s", exc)
                self._fatal_error = exc
                self._stop.set()
                break
            except RelayProtocolError as exc:
                LOG.error("configuration error during reconnect: %s", exc)
                self._fatal_error = exc
                self._stop.set()
                break
            except RelayError as exc:
                self._mark_disconnected(str(exc), count_attempt=True)
                LOG.info("reconnect attempt failed: %s", exc)

    def _heartbeat_loop(self) -> None:
        while not self._stop.wait(self.config.heartbeat_interval):
            self._heartbeat_once()

    def _heartbeat_once(self) -> None:
        with self._command_lock:
            client = self.client
            if client is None or not self.connected:
                return
            try:
                client.heartbeat()
            except RelayError as exc:
                LOG.warning("heartbeat failed: %s", exc)
                self._mark_disconnected(str(exc))

    def _bind_socket(self) -> None:
        _prepare_socket_path(self.config.socket_path)
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            server.bind(self.config.socket_path)
            os.chmod(self.config.socket_path, 0o600)
            server.listen()
            server.setblocking(False)
        except OSError:
            server.close()
            raise
        self._server = server

    def _accept_loop(self) -> None:
        assert self._server is not None
        self._selector.register(self._server, selectors.EVENT_READ)
        try:
            while not self._stop.is_set():
                try:
                    events = self._selector.select(timeout=0.2)
                except OSError:
                    break
                for key, _mask in events:
                    if key.fileobj is self._server:
                        self._accept_client()
                    else:
                        self._read_client(key.fileobj)
        finally:
            for key in list(self._selector.get_map().values()):
                try:
                    self._selector.unregister(key.fileobj)
                except Exception:
                    pass
                try:
                    key.fileobj.close()
                except Exception:
                    pass

    def _accept_client(self) -> None:
        assert self._server is not None
        try:
            conn, _addr = self._server.accept()
        except OSError:
            return
        conn.setblocking(False)
        self._selector.register(conn, selectors.EVENT_READ, bytearray())

    def _read_client(self, conn: socket.socket) -> None:
        try:
            chunk = conn.recv(4096)
        except OSError:
            self._close_client(conn)
            return
        if not chunk:
            self._close_client(conn)
            return
        key = self._selector.get_key(conn)
        buffer: bytearray = key.data
        buffer.extend(chunk)
        while b"\n" in buffer:
            raw, _, remainder = buffer.partition(b"\n")
            buffer[:] = remainder
            self._order += 1
            if len(raw) > MAX_FRAME_BYTES:
                self._send_response(
                    conn,
                    response_error(None, "protocol", "request line exceeds 4096 bytes"),
                )
                continue
            self._pending_lines.put(
                _PendingLine(raw, lambda response, c=conn: self._send_response(c, response), self._order)
            )
        if len(buffer) > MAX_FRAME_BYTES:
            self._send_response(conn, response_error(None, "protocol", "request line exceeds 4096 bytes"))
            self._close_client(conn)

    def _close_client(self, conn: socket.socket) -> None:
        try:
            self._selector.unregister(conn)
        except Exception:
            pass
        try:
            conn.close()
        except OSError:
            pass

    def _send_response(self, conn: socket.socket, response: dict[str, Any]) -> None:
        try:
            conn.sendall((json.dumps(response, separators=(",", ":")) + "\n").encode())
        except OSError:
            LOG.info("client disconnected before response could be delivered")

    def _parser_loop(self) -> None:
        while not self._stop.is_set():
            pending = self._pending_lines.get()
            if pending is None:
                break
            request_id: str | int | None = None
            try:
                request = parse_request_line(pending.data, pending.accepted_order)
                request.responder = pending.responder
                self._requests.put(request)
            except RelayValidationError as exc:
                request_id = _request_id_from_line(pending.data)
                pending.responder(error_response_for_exception(request_id, exc))
            except RelayProtocolError as exc:
                pending.responder(error_response_for_exception(request_id, exc))

    def _worker_loop(self) -> None:
        while not self._stop.is_set():
            request = self._requests.get()
            if request is None:
                break
            try:
                result = self._handle_request(request)
                request.responder(response_ok(request.request_id, result))
            except RelayError as exc:
                request.responder(error_response_for_exception(request.request_id, exc))
            except Exception as exc:  # pragma: no cover - defensive daemon boundary
                LOG.exception("daemon command failed")
                request.responder(error_response_for_exception(request.request_id, exc))

    def _handle_request(self, request: _Request) -> dict[str, Any]:
        if request.command == "daemon-status":
            return self.get_daemon_status()
        with self._command_lock:
            client = self.client
            if client is None or not self.connected:
                raise RelayTransportError("relay controller is disconnected")
            try:
                result = _dispatch_device_command(client, request.command or "", request.args)
            except RelayTransportError as exc:
                self._mark_disconnected(str(exc))
                raise
            if request.command == "reboot":
                self._mark_disconnected("firmware reboot requested")
            return result


def _dispatch_device_command(
    client: RelayClient,
    command: str,
    args: dict[str, Any],
) -> dict[str, Any]:
    if command == "info":
        return client.get_info()
    if command == "build-info":
        return client.get_build_info()
    if command == "get":
        return client.get_relays(args.get("channel"))
    if command == "set":
        return client.set_relay(args["channel"], args["on"])
    if command == "set-all":
        return client.set_all_relays(args["state"])
    if command == "pulse":
        return client.pulse_relay(args["channel"], args["duration_ms"])
    if command == "off-all":
        return client.off_all()
    if command == "status":
        return client.get_status()
    if command == "reboot":
        return client.reboot()
    raise RelayValidationError(f"unknown command {command}")


def _request_id_from_line(line: bytes) -> str | int | None:
    try:
        payload = json.loads(line.decode())
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    if not isinstance(payload, dict):
        return None
    request_id = payload.get("id")
    if isinstance(request_id, (str, int)) and not isinstance(request_id, bool):
        return request_id
    return None


def _is_missing_selected_device(exc: RelayValidationError) -> bool:
    return str(exc).startswith("no relay device found with USB serial ")


def _validate_readiness_info(info: dict[str, Any]) -> None:
    if info.get("protocol_version") != PROTOCOL_VERSION:
        raise RelayProtocolError(
            f"unexpected relay protocol version {info.get('protocol_version')}"
        )
    relay_count = info.get("relay_count")
    if relay_count is not None and relay_count != RELAY_COUNT:
        raise RelayProtocolError(f"unexpected relay count {relay_count}")
    hardware = info.get("hardware")
    if isinstance(hardware, str) and hardware != HARDWARE_NAME:
        raise RelayProtocolError(f"unexpected relay hardware {hardware}")


def _prepare_socket_path(socket_path: str) -> None:
    parent = os.path.dirname(os.path.abspath(socket_path))
    if parent:
        os.makedirs(parent, mode=0o700, exist_ok=True)
        os.chmod(parent, 0o700)
    if not os.path.exists(socket_path):
        return
    if not stat.S_ISSOCK(os.stat(socket_path).st_mode):
        raise RelayTransportError(f"socket path exists and is not a socket: {socket_path}")
    probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    probe.settimeout(0.2)
    try:
        probe.connect(socket_path)
    except OSError:
        probe.close()
        os.unlink(socket_path)
        return
    finally:
        probe.close()
    try:
        with RelayDaemonClient.connect(socket_path, timeout_s=0.2) as client:
            client.get_daemon_status()
    except RelayError as exc:
        raise RelayTransportError(f"socket path is in use by a non-daemon listener: {socket_path}") from exc
    raise RelayTransportError(f"relay daemon is already running at {socket_path}")


class RelayDaemonArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.print_usage(sys.stderr)
        self.exit(EXIT_ARGUMENT, f"{self.prog}: error: {message}\n")


def build_parser() -> argparse.ArgumentParser:
    parser = RelayDaemonArgumentParser(description=__doc__)
    parser.add_argument("--instance", help="named relay instance from TOML config")
    parser.add_argument("--config", help="TOML config path for --instance")
    selector = parser.add_mutually_exclusive_group()
    selector.add_argument("--port", help="exact serial port, for example /dev/ttyACM0")
    selector.add_argument("--serial", help="USB serial number for relay device selection")
    parser.add_argument("--socket", help="Unix-domain socket path")
    parser.add_argument("--baud", type=int)
    parser.add_argument("--timeout", type=float)
    parser.add_argument("--retries", type=int)
    parser.add_argument("--reconnect-interval", type=float)
    parser.add_argument("--wait-device", action="store_true", default=None)
    parser.add_argument("--log-level")
    return parser


def config_from_args(args: argparse.Namespace) -> DaemonConfig:
    if args.instance:
        resolved = resolve_instance_config(
            instance=args.instance,
            config_path=args.config,
            overrides={
                "port": args.port,
                "serial": args.serial,
                "socket": args.socket,
                "baud": args.baud,
                "timeout": args.timeout,
                "retries": args.retries,
                "reconnect_interval": args.reconnect_interval,
                "wait_device": args.wait_device,
                "log_level": args.log_level,
            },
        )
        return DaemonConfig(
            selector_type=resolved.selector_type,
            selector_value=resolved.selector_value,
            socket_path=resolved.socket_path,
            baud=resolved.baud,
            timeout=resolved.timeout,
            retries=resolved.retries,
            reconnect_interval=resolved.reconnect_interval,
            wait_device=resolved.wait_device,
        )

    if not args.port and not args.serial:
        raise RelayValidationError("--instance or exactly one of --port/--serial is required")
    if not args.socket:
        raise RelayValidationError("--socket is required")

    baud = 115200 if args.baud is None else args.baud
    timeout = 2.0 if args.timeout is None else args.timeout
    retries = 1 if args.retries is None else args.retries
    reconnect_interval = 1.0 if args.reconnect_interval is None else args.reconnect_interval
    wait_device = False if args.wait_device is None else args.wait_device

    if timeout <= 0:
        raise RelayValidationError("--timeout must be positive")
    if retries < 0:
        raise RelayValidationError("--retries must be non-negative")
    if reconnect_interval <= 0:
        raise RelayValidationError("--reconnect-interval must be positive")
    if args.port:
        selector_type = "port"
        selector_value = args.port
    else:
        selector_type = "serial"
        selector_value = args.serial
    return DaemonConfig(
        selector_type=selector_type,
        selector_value=selector_value,
        socket_path=args.socket,
        baud=baud,
        timeout=timeout,
        retries=retries,
        reconnect_interval=reconnect_interval,
        wait_device=wait_device,
    )


def run(args: argparse.Namespace) -> int:
    log_level = args.log_level or "INFO"
    if args.instance:
        try:
            log_level = resolve_instance_config(
                instance=args.instance,
                config_path=args.config,
                overrides={
                    "port": args.port,
                    "serial": args.serial,
                    "socket": args.socket,
                    "baud": args.baud,
                    "timeout": args.timeout,
                    "retries": args.retries,
                    "reconnect_interval": args.reconnect_interval,
                    "wait_device": args.wait_device,
                    "log_level": args.log_level,
                },
            ).log_level
        except RelayValidationError:
            pass
    logging.basicConfig(
        level=getattr(logging, str(log_level).upper(), logging.INFO),
        format="%(levelname)s:%(name)s:%(message)s",
    )
    try:
        daemon = RelayDaemon(config_from_args(args))
        daemon.install_signal_handlers()
        return daemon.run()
    except RelayValidationError as exc:
        print(f"argument error: {exc}", file=sys.stderr)
        return EXIT_ARGUMENT
    except RelayTransportError as exc:
        print(f"transport error: {exc}", file=sys.stderr)
        return EXIT_TRANSPORT
    except RelayProtocolError as exc:
        print(f"protocol error: {exc}", file=sys.stderr)
        return EXIT_PROTOCOL
    except RelayError as exc:
        print(f"daemon error: {exc}", file=sys.stderr)
        return EXIT_DAEMON


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
