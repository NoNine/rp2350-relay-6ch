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

    def identity(self) -> dict[str, Any]:
        self.calls.append(("identity", ()))
        return {"relay_count": 6}

    def capabilities(self) -> dict[str, Any]:
        self.calls.append(("capabilities", ()))
        return {
            "capabilities": 63,
            "pulse_max_ms": 60000,
            "pulse_min_ms": 10,
        }

    def build_info(self) -> dict[str, Any]:
        self.calls.append(("build_info", ()))
        return {"app_version": "0.8.0"}

    def get_relay(self, channel: int) -> dict[str, Any]:
        self.calls.append(("get_relay", (channel,)))
        return {"channel": channel, "on": True, "pulsing": False}

    def get_all_relays(self) -> dict[str, Any]:
        self.calls.append(("get_all_relays", ()))
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

    def off_all_relays(self) -> dict[str, Any]:
        self.calls.append(("off_all_relays", ()))
        return {"state": 0, "pulsing": 0}

    def status(self) -> dict[str, Any]:
        self.calls.append(("status", ()))
        return {"state": 0, "pulsing": 0, "request_count": 2}

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

    def reboot(self) -> dict[str, Any]:
        self.calls.append(("reboot", ()))
        return {"reboot": True}

    def bootsel(self) -> dict[str, Any]:
        self.calls.append(("bootsel", ()))
        return {"ok": True}

    def daemon_status(self) -> dict[str, Any]:
        self.calls.append(("daemon_status", ()))
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


def test_relayctl_bootsel_outputs_requested_message(
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = relayctl.main(["--socket", "/tmp/relay.sock", "bootsel"])

    captured = capsys.readouterr()

    assert rc == relayctl.EXIT_OK
    assert captured.out == "bootsel requested\n"
    assert FakeDaemonClient.instances[0].calls == [("bootsel", ())]


def test_relayctl_uses_named_instance_socket(
    tmp_path: Any,
    capsys: pytest.CaptureFixture[str],
) -> None:
    config_path = tmp_path / "config.toml"
    config_path.write_text(
        """
[instances.bench-a]
serial = "abc"
socket = "/tmp/bench-a.sock"
""",
        encoding="utf-8",
    )

    rc = relayctl.main(
        [
            "--instance",
            "bench-a",
            "--config",
            str(config_path),
            "status",
        ]
    )

    capsys.readouterr()

    assert rc == relayctl.EXIT_OK
    assert FakeDaemonClient.instances[0].socket_path == "/tmp/bench-a.sock"
    assert FakeDaemonClient.instances[0].calls == [("status", ())]


def test_relayctl_outputs_json(capsys: pytest.CaptureFixture[str]) -> None:
    rc = relayctl.main(["--socket", "/tmp/relay.sock", "--output", "json", "daemon-status"])

    captured = capsys.readouterr()

    assert rc == relayctl.EXIT_OK
    assert '"connected": false' in captured.out
    assert FakeDaemonClient.instances[0].calls == [("daemon_status", ())]


def test_relayctl_capabilities_human_output_decodes_mask(
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = relayctl.main(["--socket", "/tmp/relay.sock", "capabilities"])

    captured = capsys.readouterr()

    assert rc == relayctl.EXIT_OK
    assert captured.out == (
        "capabilities:  0x3f\n"
        "  get:     true\n"
        "  get_all: true\n"
        "  set:     true\n"
        "  set_all: true\n"
        "  pulse:   true\n"
        "  off_all: true\n"
        "pulse_max_ms:  60000\n"
        "pulse_min_ms:  10\n"
    )
    assert FakeDaemonClient.instances[0].calls == [("capabilities", ())]


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
    assert FakeDaemonClient.instances[0].calls == [("daemon_status", ())]


def test_relayctl_smoke_pulses_each_relay_and_forces_teardown(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Any,
) -> None:
    config_path = tmp_path / "config.toml"
    config_path.write_text(
        """
[instances.bench-a]
serial = "abc"
socket = "/tmp/bench-a.sock"
""",
        encoding="utf-8",
    )
    sleeps: list[float] = []

    monkeypatch.setattr("rp2350_relay_6ch.smoke.time.sleep", sleeps.append)

    rc = relayctl.main(
        [
            "--instance",
            "bench-a",
            "--config",
            str(config_path),
            "smoke",
            "--pulse-ms",
            "25",
        ]
    )

    captured = capsys.readouterr()
    calls = FakeDaemonClient.instances[0].calls

    assert rc == relayctl.EXIT_OK
    assert "smoke test passed" in captured.out
    assert FakeDaemonClient.instances[0].socket_path == "/tmp/bench-a.sock"
    assert calls[:3] == [("identity", ()), ("capabilities", ()), ("status", ())]
    assert [call for call in calls if call[0] == "pulse_relay"] == [
        ("pulse_relay", (0, 25)),
        ("pulse_relay", (1, 25)),
        ("pulse_relay", (2, 25)),
        ("pulse_relay", (3, 25)),
        ("pulse_relay", (4, 25)),
        ("pulse_relay", (5, 25)),
    ]
    assert sleeps == [0.025] * 6
    assert calls[-1:] == [("off_all_relays", ())]


def test_relayctl_rejects_direct_serial_options() -> None:
    with pytest.raises(SystemExit) as exc:
        relayctl.main(["--socket", "/tmp/relay.sock", "--port", "COM7", "identity"])

    assert exc.value.code == relayctl.EXIT_ARGUMENT


def test_relayctl_requires_socket() -> None:
    rc = relayctl.main(["identity"])

    assert rc == relayctl.EXIT_ARGUMENT


def test_relayctl_rejects_socket_and_instance() -> None:
    with pytest.raises(SystemExit) as exc:
        relayctl.main(["--socket", "/tmp/relay.sock", "--instance", "bench-a", "identity"])

    assert exc.value.code == relayctl.EXIT_ARGUMENT


def test_relayctl_maps_transport_exit(capsys: pytest.CaptureFixture[str]) -> None:
    FakeDaemonClient.failure = RelayTransportError("unavailable")

    rc = relayctl.main(["--socket", "/tmp/relay.sock", "identity"])

    captured = capsys.readouterr()

    assert rc == relayctl.EXIT_TRANSPORT
    assert "transport error: unavailable" in captured.err


def test_relayctl_systemd_install_prints_generated_unit(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    monkeypatch.setattr(
        relayctl,
        "install_user_unit",
        lambda **kwargs: "ExecStart=/tmp/python -m rp2350_relay_6ch.daemon --instance %i",
    )

    rc = relayctl.main(["systemd", "install", "--print-unit"])

    captured = capsys.readouterr()

    assert rc == relayctl.EXIT_OK
    assert "ExecStart=/tmp/python" in captured.out


def test_relayctl_systemd_doctor_outputs_messages(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    class Result:
        ok = True
        messages = ["unit: ok", "instance bench-a: ok"]

    monkeypatch.setattr(relayctl, "systemd_doctor", lambda instance=None: Result())

    rc = relayctl.main(["systemd", "doctor", "--instance", "bench-a"])

    captured = capsys.readouterr()

    assert rc == relayctl.EXIT_OK
    assert "unit: ok" in captured.out
    assert "instance bench-a: ok" in captured.out
