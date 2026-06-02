from __future__ import annotations

import json
import socket
import threading
from collections.abc import Callable
from typing import Any

import pytest

from rp2350_relay_6ch.daemon_client import RelayDaemonClient
from rp2350_relay_6ch.exceptions import (
    RelayDeviceError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)


def run_socket_server(
    tmp_path: Any,
    handler: Callable[[dict[str, Any]], dict[str, Any]],
) -> tuple[str, threading.Thread]:
    socket_path = str(tmp_path / "relay.sock")
    ready = threading.Event()

    def server() -> None:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.bind(socket_path)
        sock.listen()
        ready.set()
        conn, _addr = sock.accept()
        with conn, sock, conn.makefile("rb") as reader:
            while line := reader.readline():
                request = json.loads(line.decode())
                response = handler(request)
                conn.sendall((json.dumps(response) + "\n").encode())

    thread = threading.Thread(target=server)
    thread.start()
    assert ready.wait(timeout=1.0)
    return socket_path, thread


def test_daemon_client_methods_send_expected_commands(tmp_path: Any) -> None:
    seen: list[dict[str, Any]] = []

    def handler(request: dict[str, Any]) -> dict[str, Any]:
        seen.append(request)
        return {"id": request["id"], "ok": True, "result": {"state": 1}}

    socket_path, thread = run_socket_server(tmp_path, handler)

    with RelayDaemonClient.connect(socket_path) as client:
        assert client.identity() == {"state": 1}
        assert client.capabilities() == {"state": 1}
        assert client.build_info() == {"state": 1}
        assert client.status() == {"state": 1}
        assert client.health() == {"state": 1}
        assert client.transport_status() == {"state": 1}
        assert client.safety() == {"state": 1}
        assert client.watchdog() == {"state": 1}
        assert client.set_relay(0, True) == {"state": 1}
        assert client.get_relay(0) == {"state": 1}
        assert client.get_all_relays() == {"state": 1}
        assert client.off_all_relays() == {"state": 1}
        assert client.daemon_status() == {"state": 1}

    thread.join(timeout=1.0)

    assert seen == [
        {"id": 1, "command": "identity"},
        {"id": 2, "command": "capabilities"},
        {"id": 3, "command": "build-info"},
        {"id": 4, "command": "status"},
        {"id": 5, "command": "health"},
        {"id": 6, "command": "transport"},
        {"id": 7, "command": "safety"},
        {"id": 8, "command": "watchdog"},
        {"id": 9, "command": "set", "args": {"channel": 0, "on": True}},
        {"id": 10, "command": "get", "args": {"channel": 0}},
        {"id": 11, "command": "get-all"},
        {"id": 12, "command": "off-all"},
        {"id": 13, "command": "daemon-status"},
    ]


@pytest.mark.parametrize(
    ("kind", "error_type"),
    [
        ("validation", RelayValidationError),
        ("transport", RelayTransportError),
        ("timeout", RelayTimeoutError),
        ("protocol", RelayProtocolError),
        ("daemon", RelayTransportError),
    ],
)
def test_daemon_client_maps_error_kinds(
    tmp_path: Any,
    kind: str,
    error_type: type[Exception],
) -> None:
    def handler(request: dict[str, Any]) -> dict[str, Any]:
        return {"id": request["id"], "ok": False, "error": {"kind": kind, "message": "failed"}}

    socket_path, thread = run_socket_server(tmp_path, handler)

    with RelayDaemonClient.connect(socket_path) as client:
        with pytest.raises(error_type, match="failed"):
            client.identity()

    thread.join(timeout=1.0)


def test_daemon_client_maps_device_error(tmp_path: Any) -> None:
    def handler(request: dict[str, Any]) -> dict[str, Any]:
        return {
            "id": request["id"],
            "ok": False,
            "error": {"kind": "device", "message": "busy", "group": 64, "rc": 3},
        }

    socket_path, thread = run_socket_server(tmp_path, handler)

    with RelayDaemonClient.connect(socket_path) as client:
        with pytest.raises(RelayDeviceError) as exc:
            client.pulse_relay(0, 100)

    thread.join(timeout=1.0)

    assert exc.value.group == 64
    assert exc.value.rc == 3


def test_daemon_client_validates_host_arguments() -> None:
    client, peer = socket.socketpair()
    try:
        daemon_client = RelayDaemonClient(client)
        with pytest.raises(RelayValidationError):
            daemon_client.get_relay(6)
        with pytest.raises(RelayValidationError):
            daemon_client.set_relay(6, True)
        with pytest.raises(RelayValidationError):
            daemon_client.set_relay(0, "on")  # type: ignore[arg-type]
        with pytest.raises(RelayValidationError):
            daemon_client.set_all_relays(0x40)
        with pytest.raises(RelayValidationError):
            daemon_client.pulse_relay(0, 1)
        with pytest.raises(RelayValidationError):
            daemon_client.set_all_relays(True)  # type: ignore[arg-type]
        with pytest.raises(RelayValidationError):
            daemon_client.pulse_relay(0, True)  # type: ignore[arg-type]
    finally:
        client.close()
        peer.close()
