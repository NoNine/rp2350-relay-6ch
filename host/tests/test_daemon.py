from __future__ import annotations

import json
import os
import socket
import threading
import time
from typing import Any

import pytest

from rp2350_relay_6ch.daemon import (
    MAX_FRAME_BYTES,
    DaemonConfig,
    RelayDaemon,
    _prepare_socket_path,
    parse_request_line,
)
from rp2350_relay_6ch.discovery import RelayUsbDevice
from rp2350_relay_6ch.exceptions import (
    RelayDeviceError,
    RelayProtocolError,
    RelayTransportError,
    RelayValidationError,
)


class FakeRelayClient:
    instances: list["FakeRelayClient"] = []
    failing_ports: set[str] = set()
    fail_info: Exception | None = None
    command_delay = 0.0

    def __init__(self, port: str, **kwargs: Any) -> None:
        if port in self.failing_ports:
            raise RelayTransportError(f"cannot open {port}")
        self.port = port
        self.kwargs = kwargs
        self.calls: list[tuple[str, tuple[Any, ...]]] = []
        self.closed = False
        self.relay_state = 0
        FakeRelayClient.instances.append(self)

    @classmethod
    def connect(cls, port: str, **kwargs: Any) -> "FakeRelayClient":
        return cls(port, **kwargs)

    def close(self) -> None:
        self.closed = True

    def get_info(self) -> dict[str, Any]:
        self.calls.append(("get_info", ()))
        if self.fail_info is not None:
            raise self.fail_info
        return {"protocol_version": 3, "relay_count": 6}

    def get_build_info(self) -> dict[str, Any]:
        self.calls.append(("get_build_info", ()))
        return {"app_version": "0.8.0"}

    def get_status(self) -> dict[str, Any]:
        self.calls.append(("get_status", ()))
        return {"state": self.relay_state, "pulsing": 0}

    def get_relays(self, channel: int | None = None) -> dict[str, Any]:
        self.calls.append(("get_relays", (channel,)))
        return {"state": self.relay_state, "pulsing": 0}

    def set_relay(self, channel: int, on: bool) -> dict[str, Any]:
        self.calls.append(("set_relay", (channel, on)))
        if self.command_delay:
            time.sleep(self.command_delay)
        if on:
            self.relay_state |= 1 << channel
        else:
            self.relay_state &= ~(1 << channel)
        return {"state": self.relay_state, "pulsing": 0}

    def set_all_relays(self, state: int) -> dict[str, Any]:
        self.calls.append(("set_all_relays", (state,)))
        self.relay_state = state
        return {"state": state, "pulsing": 0}

    def pulse_relay(self, channel: int, duration_ms: int) -> dict[str, Any]:
        self.calls.append(("pulse_relay", (channel, duration_ms)))
        return {"state": 1 << channel, "pulsing": 1 << channel}

    def off_all(self) -> dict[str, Any]:
        self.calls.append(("off_all", ()))
        self.relay_state = 0
        return {"state": 0, "pulsing": 0}

    def reboot(self) -> dict[str, Any]:
        self.calls.append(("reboot", ()))
        return {"reboot": True}


@pytest.fixture(autouse=True)
def reset_fakes() -> None:
    FakeRelayClient.instances = []
    FakeRelayClient.failing_ports = set()
    FakeRelayClient.fail_info = None
    FakeRelayClient.command_delay = 0.0


def make_daemon(
    tmp_path: Any,
    *,
    selector_type: str = "port",
    selector_value: str = "COM7",
    wait_device: bool = False,
    serial_selector: Any | None = None,
) -> RelayDaemon:
    config = DaemonConfig(
        selector_type=selector_type,
        selector_value=selector_value,
        socket_path=str(tmp_path / "relay.sock"),
        reconnect_interval=0.05,
        wait_device=wait_device,
    )
    return RelayDaemon(
        config,
        client_factory=FakeRelayClient.connect,
        serial_selector=serial_selector or (lambda serial: RelayUsbDevice("COM9", serial)),
        sleep=lambda seconds: None,
    )


def test_parse_request_validation_errors() -> None:
    with pytest.raises(RelayProtocolError):
        parse_request_line(b"{", 1)
    with pytest.raises(RelayValidationError, match="id"):
        parse_request_line(b'{"command":"info"}', 1)
    with pytest.raises(RelayValidationError, match="id"):
        parse_request_line(b'{"id":null,"command":"info"}', 1)
    with pytest.raises(RelayValidationError, match="unknown command"):
        parse_request_line(b'{"id":"1","command":"bogus"}', 1)
    with pytest.raises(RelayValidationError, match="channel"):
        parse_request_line(b'{"id":"1","command":"set","args":{"channel":6,"on":true}}', 1)


def test_parse_request_accepts_string_and_integer_ids() -> None:
    assert parse_request_line(b'{"id":"abc","command":"info"}', 1).request_id == "abc"
    assert parse_request_line(b'{"id":7,"command":"status"}', 2).request_id == 7


def test_initial_connect_runs_info_then_status_without_off_all(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)

    daemon._initial_connect()

    assert daemon.connected is True
    assert FakeRelayClient.instances[0].calls == [("get_info", ()), ("get_status", ())]


def test_readiness_protocol_mismatch_is_configuration_error(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)
    FakeRelayClient.fail_info = None

    class ProtocolMismatchClient(FakeRelayClient):
        def get_info(self) -> dict[str, Any]:
            self.calls.append(("get_info", ()))
            return {"protocol_version": 99, "relay_count": 6}

    daemon.client_factory = ProtocolMismatchClient.connect

    with pytest.raises(RelayProtocolError, match="protocol version"):
        daemon._initial_connect()


def test_wait_device_starts_disconnected_for_missing_serial(tmp_path: Any) -> None:
    def selector(serial: str) -> RelayUsbDevice:
        raise RelayValidationError(f"no relay device found with USB serial {serial}")

    daemon = make_daemon(
        tmp_path,
        selector_type="serial",
        selector_value="abc",
        wait_device=True,
        serial_selector=selector,
    )

    daemon._initial_connect()

    assert daemon.connected is False
    assert daemon.get_daemon_status()["reconnect_attempts"] == 1
    assert daemon.get_daemon_status()["last_error"] == "no relay device found with USB serial abc"


def test_duplicate_serial_is_configuration_error_even_with_wait_device(tmp_path: Any) -> None:
    def selector(serial: str) -> RelayUsbDevice:
        raise RelayValidationError(f"multiple relay devices found with USB serial {serial}")

    daemon = make_daemon(
        tmp_path,
        selector_type="serial",
        selector_value="abc",
        wait_device=True,
        serial_selector=selector,
    )

    with pytest.raises(RelayValidationError, match="multiple"):
        daemon._initial_connect()


def test_disconnected_device_command_fails_but_daemon_status_succeeds(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path, wait_device=True)
    daemon._mark_disconnected("lost")

    status_request = parse_request_line(b'{"id":"1","command":"daemon-status"}', 1)
    set_request = parse_request_line(
        b'{"id":"2","command":"set","args":{"channel":0,"on":true}}',
        2,
    )

    assert daemon._handle_request(status_request)["connected"] is False
    with pytest.raises(RelayTransportError, match="disconnected"):
        daemon._handle_request(set_request)


def test_reboot_returns_success_then_enters_disconnected(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)
    daemon._initial_connect()
    request = parse_request_line(b'{"id":"1","command":"reboot"}', 1)

    assert daemon._handle_request(request) == {"reboot": True}

    assert daemon.connected is False
    assert daemon.last_error == "firmware reboot requested"


def test_serial_rediscovery_after_renumbering(tmp_path: Any) -> None:
    ports = iter(["COM7", "COM8"])

    daemon = make_daemon(
        tmp_path,
        selector_type="serial",
        selector_value="abc",
        serial_selector=lambda serial: RelayUsbDevice(next(ports), serial),
    )

    daemon._connect_ready()
    daemon._mark_disconnected("lost")
    daemon._connect_ready()

    assert [client.port for client in FakeRelayClient.instances] == ["COM7", "COM8"]
    assert daemon.current_port == "COM8"


def test_shutdown_runs_best_effort_off_all_and_removes_socket(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)
    daemon._initial_connect()
    daemon._bind_socket()
    socket_path = daemon.config.socket_path

    daemon.shutdown()

    assert FakeRelayClient.instances[0].calls[-1] == ("off_all", ())
    assert FakeRelayClient.instances[0].closed is True
    assert not os.path.exists(socket_path)


def test_socket_parent_permissions_and_stale_unlink(tmp_path: Any) -> None:
    socket_path = tmp_path / "runtime" / "relay.sock"
    socket_path.parent.mkdir()
    stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    stale.bind(str(socket_path))
    stale.close()

    _prepare_socket_path(str(socket_path))

    assert oct(socket_path.parent.stat().st_mode & 0o777) == "0o700"
    assert not socket_path.exists()


def test_socket_path_in_use_by_non_daemon_listener(tmp_path: Any) -> None:
    socket_path = str(tmp_path / "relay.sock")
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(socket_path)
    listener.listen()
    try:
        with pytest.raises(RelayTransportError, match="non-daemon"):
            _prepare_socket_path(socket_path)
    finally:
        listener.close()


def test_persistent_socket_requests_are_serialized(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)
    daemon._initial_connect()
    thread = threading.Thread(target=daemon.run)
    thread.start()
    deadline = time.monotonic() + 2.0
    while not os.path.exists(daemon.config.socket_path) and time.monotonic() < deadline:
        time.sleep(0.01)

    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.connect(daemon.config.socket_path)
    with client, client.makefile("rb") as reader:
        client.sendall(b'{"id":"1","command":"set","args":{"channel":0,"on":true}}\n')
        client.sendall(b'{"id":"2","command":"get"}\n')
        first = json.loads(reader.readline().decode())
        second = json.loads(reader.readline().decode())

    daemon.shutdown()
    thread.join(timeout=2.0)

    assert first == {"id": "1", "ok": True, "result": {"state": 1, "pulsing": 0}}
    assert second == {"id": "2", "ok": True, "result": {"state": 1, "pulsing": 0}}


def test_oversized_frame_returns_protocol_error(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)
    daemon._initial_connect()
    thread = threading.Thread(target=daemon.run)
    thread.start()
    deadline = time.monotonic() + 2.0
    while not os.path.exists(daemon.config.socket_path) and time.monotonic() < deadline:
        time.sleep(0.01)

    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.connect(daemon.config.socket_path)
    with client, client.makefile("rb") as reader:
        client.sendall(b"x" * (MAX_FRAME_BYTES + 1) + b"\n")
        response = json.loads(reader.readline().decode())

    daemon.shutdown()
    thread.join(timeout=2.0)

    assert response["ok"] is False
    assert response["error"]["kind"] == "protocol"


def test_device_errors_preserve_group_and_rc(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)

    class DeviceErrorClient(FakeRelayClient):
        def pulse_relay(self, channel: int, duration_ms: int) -> dict[str, Any]:
            raise RelayDeviceError(64, 3, "busy")

    daemon.client_factory = DeviceErrorClient.connect
    daemon._initial_connect()
    request = parse_request_line(
        b'{"id":"1","command":"pulse","args":{"channel":0,"duration_ms":100}}',
        1,
    )

    with pytest.raises(RelayDeviceError) as exc:
        daemon._handle_request(request)

    assert exc.value.group == 64
    assert exc.value.rc == 3
