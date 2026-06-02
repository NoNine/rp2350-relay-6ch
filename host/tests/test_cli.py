from __future__ import annotations

from typing import Any

import pytest

import rp2350_relay_6ch.cli as cli
import rp2350_relay_6ch.smoke as smoke
from rp2350_relay_6ch.exceptions import (
    RelayDeviceError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
)


class FakeClient:
    instances: list["FakeClient"] = []
    failure: Exception | None = None
    relay_state = 0x21
    relay_pulsing = 0
    protocol_version = 6

    def __init__(
        self,
        port: str,
        *,
        baudrate: int = 115200,
        timeout_s: float = 2.0,
        retries: int = 1,
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.timeout_s = timeout_s
        self.retries = retries
        self.calls: list[tuple[str, tuple[Any, ...]]] = []
        self.closed = False
        FakeClient.instances.append(self)

    @classmethod
    def connect(
        cls,
        port: str,
        *,
        baudrate: int = 115200,
        timeout_s: float = 2.0,
        retries: int = 1,
    ) -> "FakeClient":
        if cls.failure is not None:
            raise cls.failure
        return cls(port, baudrate=baudrate, timeout_s=timeout_s, retries=retries)

    def __enter__(self) -> "FakeClient":
        return self

    def __exit__(self, exc_type: object, exc: object, tb: object) -> None:
        self.close()

    def close(self) -> None:
        self.closed = True

    def get_info(self) -> dict[str, Any]:
        self.calls.append(("get_info", ()))
        return {
            "capabilities": 31,
            "hardware": "Waveshare RP2350-Relay-6CH",
            "pulse_max_ms": 60000,
            "pulse_min_ms": 10,
            "protocol_version": self.protocol_version,
            "relay_count": 6,
        }

    def get_build_info(self) -> dict[str, Any]:
        self.calls.append(("get_build_info", ()))
        return {
            "app_version": "0.6.0",
            "zephyr_version": "4.2.0",
            "board": "native_sim",
            "git_commit": "abcdef123456",
            "git_dirty": False,
            "build_timestamp": "2026-05-18T08:00:00+08:00",
            "compiler": "GNU 13.3.0",
        }

    def get_relays(self, channel: int | None = None) -> dict[str, Any]:
        self.calls.append(("get_relays", (channel,)))
        if channel is None:
            return {"state": self.relay_state, "pulsing": self.relay_pulsing}
        return {
            "channel": channel,
            "on": (self.relay_state & (1 << channel)) != 0,
            "pulsing": (self.relay_pulsing & (1 << channel)) != 0,
        }

    def set_relay(self, channel: int, on: bool) -> dict[str, Any]:
        self.calls.append(("set_relay", (channel, on)))
        return {"state": 1 if on else 0}

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
        return {
            "state": 0,
            "pulsing": 0,
            "health": "degraded",
            "health_reasons": 8,
            "health_primary_reason": "comm_owner_timeout",
            "health_transitions": 3,
            "busy": 0,
            "comm_loss_reboot_on_timeout": False,
            "request_count": 12,
            "last_error": 0,
            "transport": "usb_cdc_acm_smp",
            "usb_cdc_acm": True,
            "smp_uart": True,
        }

    def reboot(self) -> dict[str, Any]:
        self.calls.append(("reboot", ()))
        return {"reboot": True}

    def heartbeat(self) -> dict[str, Any]:
        self.calls.append(("heartbeat", ()))
        return {"ok": True}


@pytest.fixture(autouse=True)
def fake_client(monkeypatch: pytest.MonkeyPatch) -> None:
    FakeClient.instances = []
    FakeClient.failure = None
    FakeClient.relay_state = 0x21
    FakeClient.relay_pulsing = 0
    FakeClient.protocol_version = 6
    monkeypatch.setattr(cli, "RelayClient", FakeClient)


def test_info_outputs_json_and_uses_connection_options(capsys: pytest.CaptureFixture[str]) -> None:
    rc = cli.main(
        [
            "--port",
            "COM7",
            "--baud",
            "9600",
            "--timeout",
            "3.5",
            "--retries",
            "2",
            "--output",
            "json",
            "info",
        ]
    )

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert '"relay_count": 6' in captured.out
    assert FakeClient.instances[0].port == "COM7"
    assert FakeClient.instances[0].baudrate == 9600
    assert FakeClient.instances[0].timeout_s == 3.5
    assert FakeClient.instances[0].retries == 2


def test_info_human_output_lists_all_fields(capsys: pytest.CaptureFixture[str]) -> None:
    rc = cli.main(["--port", "COM7", "info"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert "capabilities:      31" in captured.out
    assert "hardware:          Waveshare RP2350-Relay-6CH" in captured.out
    assert "pulse_max_ms:      60000" in captured.out
    assert "pulse_min_ms:      10" in captured.out
    assert "protocol_version:  6" in captured.out
    assert "relay_count:       6" in captured.out


def test_info_allows_old_protocol_for_diagnostics(
    capsys: pytest.CaptureFixture[str],
) -> None:
    FakeClient.protocol_version = 3

    rc = cli.main(["--port", "COM7", "info"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert "protocol_version:  3" in captured.out


def test_non_info_rejects_old_protocol(capsys: pytest.CaptureFixture[str]) -> None:
    FakeClient.protocol_version = 3

    rc = cli.main(["--port", "COM7", "status"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_PROTOCOL
    assert "unexpected relay protocol version 3" in captured.err


def test_session_parser_accepts_port_and_delegates(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    seen: list[Any] = []

    def fake_run_session(args: Any) -> int:
        seen.append(args)
        return 0

    monkeypatch.setattr(cli, "run_session", fake_run_session)

    rc = cli.main(["--port", "COM7", "session"])

    assert rc == cli.EXIT_OK
    assert seen[0].port == "COM7"
    assert seen[0].command == "session"


def test_session_parser_accepts_session_local_port_and_delegates(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    seen: list[Any] = []

    def fake_run_session(args: Any) -> int:
        seen.append(args)
        return 0

    monkeypatch.setattr(cli, "run_session", fake_run_session)

    rc = cli.main(["session", "--port", "COM7"])

    assert rc == cli.EXIT_OK
    assert seen[0].port == "COM7"
    assert seen[0].command == "session"


def test_session_local_options_override_global_options(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    seen: list[Any] = []

    def fake_run_session(args: Any) -> int:
        seen.append(args)
        return 0

    monkeypatch.setattr(cli, "run_session", fake_run_session)

    rc = cli.main(
        [
            "--port",
            "COM7",
            "--baud",
            "9600",
            "--timeout",
            "1.0",
            "--retries",
            "1",
            "session",
            "--port",
            "COM8",
            "--baud",
            "115200",
            "--timeout",
            "3.0",
            "--retries",
            "2",
        ]
    )

    assert rc == cli.EXIT_OK
    assert seen[0].port == "COM8"
    assert seen[0].baud == 115200
    assert seen[0].timeout == 3.0
    assert seen[0].retries == 2


def test_session_parser_accepts_session_local_serial(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    seen: list[Any] = []

    def fake_run_session(args: Any) -> int:
        seen.append(args)
        return 0

    monkeypatch.setattr(cli, "run_session", fake_run_session)

    rc = cli.main(["session", "--serial", "abc"])

    assert rc == cli.EXIT_OK
    assert seen[0].serial == "abc"


def test_session_local_serial_clears_global_port(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    seen: list[Any] = []

    def fake_run_session(args: Any) -> int:
        seen.append(args)
        return 0

    monkeypatch.setattr(cli, "run_session", fake_run_session)

    rc = cli.main(["--port", "COM7", "session", "--serial", "abc"])

    assert rc == cli.EXIT_OK
    assert seen[0].serial == "abc"
    assert seen[0].port is None


def test_session_parser_rejects_port_and_serial() -> None:
    with pytest.raises(SystemExit) as exc:
        cli.main(["--port", "COM7", "--serial", "abc", "session"])

    assert exc.value.code == cli.EXIT_ARGUMENT


def test_session_help_includes_examples_and_safe_exit_language(
    capsys: pytest.CaptureFixture[str],
) -> None:
    with pytest.raises(SystemExit) as exc:
        cli.main(["session", "--help"])

    captured = capsys.readouterr()

    assert exc.value.code == cli.EXIT_OK
    assert "rp2350-relay session --port COM7" in captured.out
    assert "Normal exit and disconnect verify all relays are off" in captured.out


def test_build_info_outputs_json_and_uses_client(
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = cli.main(["--port", "COM7", "--output", "json", "build-info"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert '"git_commit": "abcdef123456"' in captured.out
    assert FakeClient.instances[0].calls == [("get_info", ()), ("get_build_info", ())]


def test_build_info_human_output_lists_fields(capsys: pytest.CaptureFixture[str]) -> None:
    rc = cli.main(["--port", "COM7", "build-info"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert "app_version:      0.6.0" in captured.out
    assert "git_dirty:        False" in captured.out


def test_set_converts_one_based_channel_to_zero_based() -> None:
    rc = cli.main(["--port", "COM7", "set", "6", "on"])

    assert rc == cli.EXIT_OK
    assert FakeClient.instances[0].calls == [("get_info", ()), ("set_relay", (5, True))]


def test_set_all_accepts_hex_mask() -> None:
    rc = cli.main(["--port", "COM7", "set-all", "0x21"])

    assert rc == cli.EXIT_OK
    assert FakeClient.instances[0].calls == [
        ("get_info", ()),
        ("set_all_relays", (0x21,)),
    ]


def test_get_all_human_output_names_enabled_relays(
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = cli.main(["--port", "COM7", "get"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert "state:    0x21" in captured.out
    assert "on:       CH1, CH6" in captured.out
    assert "pulsing:  none" in captured.out
    assert FakeClient.instances[0].calls == [("get_info", ()), ("get_relays", (None,))]


def test_get_channel_human_output_reports_on_channel(
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = cli.main(["--port", "COM7", "get", "1"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert "channel:  CH1" in captured.out
    assert "on:       true" in captured.out
    assert "pulsing:  false" in captured.out
    assert FakeClient.instances[0].calls == [("get_info", ()), ("get_relays", (0,))]


def test_get_channel_human_output_reports_off_channel(
    capsys: pytest.CaptureFixture[str],
) -> None:
    FakeClient.relay_state = 0

    rc = cli.main(["--port", "COM7", "get", "1"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert "channel:  CH1" in captured.out
    assert "on:       false" in captured.out
    assert "pulsing:  false" in captured.out


def test_get_channel_human_output_reports_pulsing_channel(
    capsys: pytest.CaptureFixture[str],
) -> None:
    FakeClient.relay_state = 1
    FakeClient.relay_pulsing = 1

    rc = cli.main(["--port", "COM7", "get", "1"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert "channel:  CH1" in captured.out
    assert "on:       true" in captured.out
    assert "pulsing:  true" in captured.out


@pytest.mark.parametrize(
    ("argv", "expected"),
    [
        (["--port", "COM7", "set", "1", "on"], "state:  0x01\non:     CH1\n"),
        (["--port", "COM7", "set-all", "0x21"], "state:  0x21\non:     CH1, CH6\n"),
        (
            ["--port", "COM7", "pulse", "1", "100"],
            "state:    0x01\non:       CH1\npulsing:  CH1\n",
        ),
        (
            ["--port", "COM7", "off-all"],
            "state:    0x00\non:       none\npulsing:  none\n",
        ),
    ],
)
def test_relay_commands_human_output_aligns_key_values(
    argv: list[str],
    expected: str,
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = cli.main(argv)

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert captured.out == expected


def test_status_human_output_groups_relay_and_transport_fields(
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = cli.main(["--port", "COM7", "status"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert captured.out == (
        "[relays]\n"
        "state:                        0x00\n"
        "on:                           none\n"
        "pulsing:                      none\n"
        "\n"
        "[health]\n"
        "state:                        degraded\n"
        "primary_reason:               comm_owner_timeout\n"
        "reasons:                      8\n"
        "transitions:                  3\n"
        "\n"
        "[transport]\n"
        "busy:                         0\n"
        "comm_loss_reboot_on_timeout:  False\n"
        "last_error:                   0\n"
        "request_count:                12\n"
        "transport:                    usb_cdc_acm_smp\n"
    )


def test_status_json_output_is_unchanged(capsys: pytest.CaptureFixture[str]) -> None:
    rc = cli.main(["--port", "COM7", "--output", "json", "status"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert captured.out.strip() == (
        '{"busy": 0, "comm_loss_reboot_on_timeout": false, "health": "degraded", '
        '"health_primary_reason": "comm_owner_timeout", "health_reasons": 8, '
        '"health_transitions": 3, "last_error": 0, "pulsing": 0, '
        '"request_count": 12, "smp_uart": true, "state": 0, '
        '"transport": "usb_cdc_acm_smp", "usb_cdc_acm": true}'
    )


def test_missing_port_returns_argument_exit(capsys: pytest.CaptureFixture[str]) -> None:
    rc = cli.main(["status"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_ARGUMENT
    assert "--port is required" in captured.err


@pytest.mark.parametrize(
    ("failure", "exit_code", "message"),
    [
        (RelayTransportError("closed"), cli.EXIT_TRANSPORT, "transport error"),
        (RelayTimeoutError("late"), cli.EXIT_TIMEOUT, "timeout error"),
        (RelayProtocolError("bad seq"), cli.EXIT_PROTOCOL, "protocol error"),
        (RelayDeviceError(64, 3, "busy"), cli.EXIT_DEVICE, "device error"),
    ],
)
def test_failure_exit_codes(
    failure: Exception,
    exit_code: int,
    message: str,
    capsys: pytest.CaptureFixture[str],
) -> None:
    FakeClient.failure = failure

    rc = cli.main(["--port", "COM7", "info"])

    captured = capsys.readouterr()

    assert rc == exit_code
    assert message in captured.err


def test_invalid_channel_exits_with_argument_code() -> None:
    with pytest.raises(SystemExit) as exc:
        cli.main(["--port", "COM7", "get", "7"])

    assert exc.value.code == cli.EXIT_ARGUMENT


def test_smoke_pulses_each_relay_and_forces_teardown(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    sleeps: list[float] = []

    monkeypatch.setattr(smoke.time, "sleep", sleeps.append)

    rc = cli.main(["--port", "COM7", "smoke", "--pulse-ms", "25"])

    calls = FakeClient.instances[0].calls

    assert rc == cli.EXIT_OK
    assert calls[:3] == [("get_info", ()), ("get_info", ()), ("get_status", ())]
    assert [call for call in calls if call[0] == "pulse_relay"] == [
        ("pulse_relay", (0, 25)),
        ("pulse_relay", (1, 25)),
        ("pulse_relay", (2, 25)),
        ("pulse_relay", (3, 25)),
        ("pulse_relay", (4, 25)),
        ("pulse_relay", (5, 25)),
    ]
    assert sleeps == [0.025] * 6
    assert calls[-1:] == [("off_all", ())]
    assert FakeClient.instances[0].closed is True


def test_smoke_defaults_to_observable_pulse_duration(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    sleeps: list[float] = []

    monkeypatch.setattr(smoke.time, "sleep", sleeps.append)

    rc = cli.main(["--port", "COM7", "smoke"])

    calls = FakeClient.instances[0].calls

    assert rc == cli.EXIT_OK
    assert [call for call in calls if call[0] == "pulse_relay"] == [
        ("pulse_relay", (0, 1000)),
        ("pulse_relay", (1, 1000)),
        ("pulse_relay", (2, 1000)),
        ("pulse_relay", (3, 1000)),
        ("pulse_relay", (4, 1000)),
        ("pulse_relay", (5, 1000)),
    ]
    assert sleeps == [1.0] * 6
