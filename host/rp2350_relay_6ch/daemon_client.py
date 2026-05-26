"""Unix-socket client for the RP2350 relay daemon."""

from __future__ import annotations

import json
import socket
from typing import Any

from .constants import PULSE_MAX_MS, PULSE_MIN_MS, RELAY_COUNT, RELAY_MASK
from .exceptions import (
    RelayDeviceError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)


class RelayDaemonClient:
    """High-level client for a local ``rp2350-relayd`` instance."""

    def __init__(self, sock: socket.socket, *, timeout_s: float = 2.0) -> None:
        if timeout_s <= 0:
            raise RelayValidationError("timeout_s must be positive")
        self._sock = sock
        self.timeout_s = timeout_s
        self._next_id = 1
        self._reader = sock.makefile("rb")

    @classmethod
    def connect(cls, socket_path: str, timeout_s: float = 2.0) -> "RelayDaemonClient":
        if not socket_path:
            raise RelayValidationError("socket_path is required")
        if timeout_s <= 0:
            raise RelayValidationError("timeout_s must be positive")

        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(timeout_s)
        try:
            sock.connect(socket_path)
        except OSError as exc:
            sock.close()
            raise RelayTransportError(str(exc)) from exc
        return cls(sock, timeout_s=timeout_s)

    def __enter__(self) -> "RelayDaemonClient":
        return self

    def __exit__(self, exc_type: object, exc: object, tb: object) -> None:
        self.close()

    def close(self) -> None:
        try:
            self._reader.close()
        finally:
            self._sock.close()

    def get_info(self) -> dict[str, Any]:
        return self._request("info")

    def get_build_info(self) -> dict[str, Any]:
        return self._request("build-info")

    def get_relays(self, channel: int | None = None) -> dict[str, Any]:
        args: dict[str, Any] = {}
        if channel is not None:
            self._validate_channel(channel)
            args["channel"] = channel
        return self._request("get", args)

    def set_relay(self, channel: int, on: bool) -> dict[str, Any]:
        self._validate_channel(channel)
        if not isinstance(on, bool):
            raise RelayValidationError("on must be a bool")
        return self._request("set", {"channel": channel, "on": on})

    def set_all_relays(self, state: int) -> dict[str, Any]:
        self._validate_state_mask(state)
        return self._request("set-all", {"state": state})

    def pulse_relay(self, channel: int, duration_ms: int) -> dict[str, Any]:
        self._validate_channel(channel)
        self._validate_duration(duration_ms)
        return self._request("pulse", {"channel": channel, "duration_ms": duration_ms})

    def off_all(self) -> dict[str, Any]:
        return self._request("off-all")

    def get_status(self) -> dict[str, Any]:
        return self._request("status")

    def reboot(self) -> dict[str, Any]:
        return self._request("reboot")

    def get_daemon_status(self) -> dict[str, Any]:
        return self._request("daemon-status")

    def _request(self, command: str, args: dict[str, Any] | None = None) -> dict[str, Any]:
        request_id = self._next_id
        self._next_id += 1
        frame: dict[str, Any] = {"id": request_id, "command": command}
        if args is not None:
            frame["args"] = args
        encoded = (json.dumps(frame, separators=(",", ":")) + "\n").encode()

        try:
            self._sock.sendall(encoded)
            raw = self._reader.readline()
        except socket.timeout as exc:
            raise RelayTimeoutError("timed out waiting for daemon response") from exc
        except OSError as exc:
            raise RelayTransportError(str(exc)) from exc

        if not raw:
            raise RelayTransportError("daemon closed the connection")

        try:
            response = json.loads(raw.decode())
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise RelayProtocolError("daemon returned malformed JSON") from exc

        if not isinstance(response, dict):
            raise RelayProtocolError("daemon response must be a JSON object")
        if response.get("id") != request_id:
            raise RelayProtocolError("daemon response id does not match the request")
        if response.get("ok") is True:
            result = response.get("result")
            if not isinstance(result, dict):
                raise RelayProtocolError("daemon success response result must be an object")
            return result
        if response.get("ok") is False:
            error = response.get("error")
            if not isinstance(error, dict):
                raise RelayProtocolError("daemon error response must include an error object")
            self._raise_error(error)
        raise RelayProtocolError("daemon response missing ok status")

    @staticmethod
    def _raise_error(error: dict[str, Any]) -> None:
        kind = error.get("kind")
        message = error.get("message")
        if not isinstance(kind, str) or not isinstance(message, str):
            raise RelayProtocolError("daemon error object missing kind or message")
        if kind == "validation":
            raise RelayValidationError(message)
        if kind == "transport":
            raise RelayTransportError(message)
        if kind == "timeout":
            raise RelayTimeoutError(message)
        if kind == "protocol":
            raise RelayProtocolError(message)
        if kind == "device":
            raise RelayDeviceError(int(error.get("group", -1)), int(error.get("rc", -1)), message)
        if kind == "daemon":
            raise RelayTransportError(message)
        raise RelayProtocolError(f"unknown daemon error kind {kind}")

    @staticmethod
    def _validate_channel(channel: int) -> None:
        if (
            not isinstance(channel, int)
            or isinstance(channel, bool)
            or channel < 0
            or channel >= RELAY_COUNT
        ):
            raise RelayValidationError(f"channel must be 0..{RELAY_COUNT - 1}")

    @staticmethod
    def _validate_state_mask(state: int) -> None:
        if not isinstance(state, int) or isinstance(state, bool) or state < 0 or state & ~RELAY_MASK:
            raise RelayValidationError(f"state must be a six-bit mask 0..0x{RELAY_MASK:x}")

    @staticmethod
    def _validate_duration(duration_ms: int) -> None:
        if (
            not isinstance(duration_ms, int)
            or isinstance(duration_ms, bool)
            or duration_ms < PULSE_MIN_MS
            or duration_ms > PULSE_MAX_MS
        ):
            raise RelayValidationError(f"duration_ms must be {PULSE_MIN_MS}..{PULSE_MAX_MS}")
