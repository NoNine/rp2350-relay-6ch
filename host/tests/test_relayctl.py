from __future__ import annotations

from typing import Any

import pytest

import rp2350_relay_6ch.relayctl as relayctl
from rp2350_relay_6ch.exceptions import RelayTransportError


class FakeDaemonClient:
    instances: list["FakeDaemonClient"] = []
    failure: Exception | None = None

    def __init__(self, socket_path: str, *, timeout_s: float = 2.0) -> None:
        self.socket_path = socket_path
        self.timeout_s = timeout_s
        self.calls: list[tuple[str, tuple[Any, ...]]] = []
        FakeDaemonClient.instances.append(self)

    @classmethod
    def connect(cls, socket_path: str, timeout_s: float = 2.0) -> "FakeDaemonClient":
        if cls.failure is not None:
            raise cls.failure
        return cls(socket_path, timeout_s=timeout_s)

    def __enter__(self) -> "FakeDaemonClient":
        return self

    def __exit__(self, exc_type: object, exc: object, tb: object) -> None:
        pass

    def get_info(self) -> dict[str, Any]:
        self.calls.append(("get_info", ()))
        return {"relay_count": 6}

    def get_build_info(self) -> dict[str, Any]:
        self.calls.append(("get_build_info", ()))
        return {"app_version": "0.8.0"}

    def get_relays(self, channel: int | None = None) -> dict[str, Any]:
        self.calls.append(("get_relays", (channel,)))
        return {"state": 0x21, "pulsing": 0}

    def set_relay(self, channel: int, on: bool) -> dict[str, Any]:
        self.calls.append(("set_relay", (channel, on)))
        return {"state": 1 << channel}

    def set_all_relays(self, state: int) -> dict[str, Any]:
        self.calls.append(("set_all_relays", (state,)))
        return {"state": state}

    def pulse_relay(self, channel: int, duration_ms: int) -> dict[str, Any]:
        self.calls.append(("pulse_relay", (channel, duration_ms)))
        return {"state": 1 << channel, "pulsing": 1 << channel}

    def off_all(self) -> dict[str, Any]:
        self.calls.append(("off_all", ()))
        return {"state": 0, "pulsing": 0}

    def get_status(self) -> dict[str, Any]:
        self.calls.append(("get_status", ()))
        return {"state": 0, "pulsing": 0, "request_count": 2}

    def reboot(self) -> dict[str, Any]:
        self.calls.append(("reboot", ()))
        return {"reboot": True}

    def get_daemon_status(self) -> dict[str, Any]:
        self.calls.append(("get_daemon_status", ()))
        return {
            "connected": False,
            "selector_type": "serial",
            "selector_value": "abc",
            "current_port": None,
            "socket_path": self.socket_path,
            "reconnect_attempts": 1,
            "last_error": "missing",
            "daemon_version": "0.8.0",
        }


@pytest.fixture(autouse=True)
def fake_client(monkeypatch: pytest.MonkeyPatch) -> None:
    FakeDaemonClient.instances = []
    FakeDaemonClient.failure = None
    monkeypatch.setattr(relayctl, "RelayDaemonClient", FakeDaemonClient)


def test_relayctl_uses_socket_timeout_and_one_based_channels(
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = relayctl.main(["--socket", "/tmp/relay.sock", "--timeout", "3.5", "set", "6", "on"])

    captured = capsys.readouterr()

    assert rc == relayctl.EXIT_OK
    assert "CH6" in captured.out
    assert FakeDaemonClient.instances[0].socket_path == "/tmp/relay.sock"
    assert FakeDaemonClient.instances[0].timeout_s == 3.5
    assert FakeDaemonClient.instances[0].calls == [("set_relay", (5, True))]


def test_relayctl_outputs_json(capsys: pytest.CaptureFixture[str]) -> None:
    rc = relayctl.main(["--socket", "/tmp/relay.sock", "--output", "json", "daemon-status"])

    captured = capsys.readouterr()

    assert rc == relayctl.EXIT_OK
    assert '"connected": false' in captured.out
    assert FakeDaemonClient.instances[0].calls == [("get_daemon_status", ())]


def test_relayctl_daemon_status_defaults_to_human_output(
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = relayctl.main(["--socket", "/tmp/relay.sock", "daemon-status"])

    captured = capsys.readouterr()

    assert rc == relayctl.EXIT_OK
    assert captured.out == (
        "connected:           false\n"
        "selector_type:       serial\n"
        "selector_value:      abc\n"
        "current_port:        none\n"
        "socket_path:         /tmp/relay.sock\n"
        "reconnect_attempts:  1\n"
        "last_error:          missing\n"
        "daemon_version:      0.8.0\n"
    )
    assert "{" not in captured.out
    assert FakeDaemonClient.instances[0].calls == [("get_daemon_status", ())]


def test_relayctl_rejects_direct_serial_options() -> None:
    with pytest.raises(SystemExit) as exc:
        relayctl.main(["--socket", "/tmp/relay.sock", "--port", "COM7", "info"])

    assert exc.value.code == relayctl.EXIT_ARGUMENT


def test_relayctl_requires_socket() -> None:
    with pytest.raises(SystemExit) as exc:
        relayctl.main(["info"])

    assert exc.value.code == relayctl.EXIT_ARGUMENT


def test_relayctl_maps_transport_exit(capsys: pytest.CaptureFixture[str]) -> None:
    FakeDaemonClient.failure = RelayTransportError("unavailable")

    rc = relayctl.main(["--socket", "/tmp/relay.sock", "info"])

    captured = capsys.readouterr()

    assert rc == relayctl.EXIT_TRANSPORT
    assert "transport error: unavailable" in captured.err
