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
    build_parser,
    config_from_args,
    parse_request_line,
)
from rp2350_relay_6ch.discovery import RelayUsbDevice
from rp2350_relay_6ch.exceptions import (
    RelayDeviceError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)


class FakeRelayClient:
    instances: list["FakeRelayClient"] = []
    failing_ports: set[str] = set()
    fail_info: Exception | None = None
    heartbeat_failure: Exception | None = None
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

    def identity(self) -> dict[str, Any]:
        self.calls.append(("identity", ()))
        if self.fail_info is not None:
            raise self.fail_info
        return {"protocol_version": 7, "command_model_version": 2, "relay_count": 6}

    def capabilities(self) -> dict[str, Any]:
        self.calls.append(("capabilities", ()))
        return {"capabilities": 31}

    def build_info(self) -> dict[str, Any]:
        self.calls.append(("build_info", ()))
        return {"app_version": "0.8.0"}

    def status(self) -> dict[str, Any]:
        self.calls.append(("status", ()))
        return {
            "state": self.relay_state,
            "pulsing": 0,
            "health": "degraded",
            "health_reasons": 8,
            "health_primary_reason": "comm_owner_timeout",
            "health_transitions": 3,
        }

    def health(self) -> dict[str, Any]:
        self.calls.append(("health", ()))
        return {"health": "normal"}

    def transport_status(self) -> dict[str, Any]:
        self.calls.append(("transport_status", ()))
        return {"transport": "usb_cdc_acm_smp"}

    def safety(self) -> dict[str, Any]:
        self.calls.append(("safety", ()))
        return {"comm_loss_policy": "energized_only"}

    def watchdog(self) -> dict[str, Any]:
        self.calls.append(("watchdog", ()))
        return {"watchdog_enabled": False}

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

    def heartbeat(self) -> dict[str, Any]:
        self.calls.append(("heartbeat", ()))
        if self.heartbeat_failure is not None:
            raise self.heartbeat_failure
        return {"ok": True}


@pytest.fixture(autouse=True)
def reset_fakes() -> None:
    FakeRelayClient.instances = []
    FakeRelayClient.failing_ports = set()
    FakeRelayClient.fail_info = None
    FakeRelayClient.heartbeat_failure = None
    FakeRelayClient.command_delay = 0.0


def make_daemon(
    tmp_path: Any,
    *,
    selector_type: str = "port",
    selector_value: str = "COM7",
    wait_device: bool = False,
    serial_selector: Any | None = None,
    heartbeat_interval: float = 2.5,
) -> RelayDaemon:
    config = DaemonConfig(
        selector_type=selector_type,
        selector_value=selector_value,
        socket_path=str(tmp_path / "relay.sock"),
        reconnect_interval=0.05,
        heartbeat_interval=heartbeat_interval,
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
        parse_request_line(b'{"command":"identity"}', 1)
    with pytest.raises(RelayValidationError, match="id"):
        parse_request_line(b'{"id":null,"command":"identity"}', 1)
    with pytest.raises(RelayValidationError, match="unknown command"):
        parse_request_line(b'{"id":"1","command":"bogus"}', 1)
    with pytest.raises(RelayValidationError, match="channel"):
        parse_request_line(b'{"id":"1","command":"set","args":{"channel":6,"on":true}}', 1)


def test_parse_request_accepts_string_and_integer_ids() -> None:
    assert parse_request_line(b'{"id":"abc","command":"identity"}', 1).request_id == "abc"
    assert parse_request_line(b'{"id":7,"command":"status"}', 2).request_id == 7


def test_config_from_args_resolves_named_instance(tmp_path: Any) -> None:
    config_path = tmp_path / "config.toml"
    config_path.write_text(
        """
[instances.bench-a]
serial = "abc"
socket = "/tmp/bench-a.sock"
wait_device = true
timeout = 3.5
""",
        encoding="utf-8",
    )
    args = build_parser().parse_args(["--instance", "bench-a", "--config", str(config_path)])

    config = config_from_args(args)

    assert config.selector_type == "serial"
    assert config.selector_value == "abc"
    assert config.socket_path == "/tmp/bench-a.sock"
    assert config.wait_device is True
    assert config.timeout == 3.5


def test_config_from_args_keeps_raw_selector_flow() -> None:
    args = build_parser().parse_args(["--port", "/dev/ttyACM0", "--socket", "/tmp/relay.sock"])

    config = config_from_args(args)

    assert config.selector_type == "port"
    assert config.selector_value == "/dev/ttyACM0"
    assert config.socket_path == "/tmp/relay.sock"


def test_initial_connect_runs_identity_then_status_without_off_all(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)

    daemon._initial_connect()

    assert daemon.connected is True
    assert FakeRelayClient.instances[0].calls == [("identity", ()), ("status", ())]


def test_default_heartbeat_interval_is_fixed_2_5_seconds(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)

    assert daemon.config.heartbeat_interval == 2.5


def test_heartbeat_succeeds_silently_while_connected(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)
    daemon._initial_connect()

    daemon._heartbeat_once()

    assert daemon.connected is True
    assert daemon.last_error is None
    assert FakeRelayClient.instances[0].calls == [
        ("identity", ()),
        ("status", ()),
        ("heartbeat", ()),
    ]


def test_heartbeat_does_not_run_while_disconnected(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path, wait_device=True)
    daemon._mark_disconnected("lost")

    daemon._heartbeat_once()

    assert FakeRelayClient.instances == []
    assert daemon.get_daemon_status()["last_error"] == "lost"


def test_heartbeat_failure_closes_client_and_marks_disconnected(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)
    daemon._initial_connect()
    FakeRelayClient.heartbeat_failure = RelayTimeoutError("late")

    daemon._heartbeat_once()

    assert daemon.connected is False
    assert daemon.current_port is None
    assert daemon.last_error == "late"
    assert FakeRelayClient.instances[0].closed is True
    assert FakeRelayClient.instances[0].calls[-1] == ("heartbeat", ())


def test_heartbeat_waits_for_active_command(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)
    daemon._initial_connect()
    order: list[str] = []
    release_command = threading.Event()
    command_started = threading.Event()
    client = FakeRelayClient.instances[0]

    def set_relay(channel: int, on: bool) -> dict[str, Any]:
        client.calls.append(("set_relay", (channel, on)))
        order.append("command-start")
        command_started.set()
        assert release_command.wait(timeout=1.0)
        order.append("command-end")
        return {"state": 1 if on else 0, "pulsing": 0}

    def heartbeat() -> dict[str, Any]:
        client.calls.append(("heartbeat", ()))
        order.append("heartbeat")
        return {"ok": True}

    client.set_relay = set_relay  # type: ignore[method-assign]
    client.heartbeat = heartbeat  # type: ignore[method-assign]
    request = parse_request_line(
        b'{"id":"1","command":"set","args":{"channel":0,"on":true}}',
        1,
    )

    command_thread = threading.Thread(target=lambda: daemon._handle_request(request))
    command_thread.start()
    assert command_started.wait(timeout=1.0)
    heartbeat_thread = threading.Thread(target=daemon._heartbeat_once)
    heartbeat_thread.start()
    time.sleep(0.05)

    assert order == ["command-start"]

    release_command.set()
    command_thread.join(timeout=1.0)
    heartbeat_thread.join(timeout=1.0)

    assert order == ["command-start", "command-end", "heartbeat"]


def test_heartbeat_loop_runs_and_stops_on_shutdown(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path, heartbeat_interval=0.01)
    daemon._initial_connect()
    daemon._bind_socket()
    daemon._start_threads()
    deadline = time.monotonic() + 1.0
    client = FakeRelayClient.instances[0]

    while ("heartbeat", ()) not in client.calls and time.monotonic() < deadline:
        time.sleep(0.01)

    assert ("heartbeat", ()) in client.calls

    daemon.shutdown()
    heartbeat_count = client.calls.count(("heartbeat", ()))
    time.sleep(0.05)

    assert client.calls.count(("heartbeat", ())) == heartbeat_count
    assert client.calls[-1] == ("off_all", ())


def test_readiness_protocol_mismatch_is_configuration_error(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)
    FakeRelayClient.fail_info = None

    class ProtocolMismatchClient(FakeRelayClient):
        def identity(self) -> dict[str, Any]:
            self.calls.append(("identity", ()))
            return {"protocol_version": 99, "command_model_version": 2, "relay_count": 6}

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


def test_status_preserves_additive_health_fields(tmp_path: Any) -> None:
    daemon = make_daemon(tmp_path)
    daemon._initial_connect()

    request = parse_request_line(b'{"id":"1","command":"status"}', 1)
    response = daemon._handle_request(request)

    assert response == {
        "state": 0,
        "pulsing": 0,
        "health": "degraded",
        "health_reasons": 8,
        "health_primary_reason": "comm_owner_timeout",
        "health_transitions": 3,
    }


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
