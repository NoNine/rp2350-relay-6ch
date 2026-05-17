from __future__ import annotations

import importlib.util
from pathlib import Path
from typing import Any

import pytest

from rp2350_relay_6ch.exceptions import (
    RelayDeviceError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
)

ROOT_DIR = Path(__file__).resolve().parents[2]
CLI_PATH = ROOT_DIR / "tools" / "rp2350_relay_cli.py"
SPEC = importlib.util.spec_from_file_location("rp2350_relay_cli", CLI_PATH)
assert SPEC is not None
assert SPEC.loader is not None
cli = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cli)


class FakeClient:
    instances: list["FakeClient"] = []
    failure: Exception | None = None

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

    def get_info(self) -> dict[str, Any]:
        self.calls.append(("get_info", ()))
        return {
            "hardware": "Waveshare RP2350-Relay-6CH",
            "protocol_version": 1,
            "relay_count": 6,
        }

    def get_relays(self, channel: int | None = None) -> dict[str, Any]:
        self.calls.append(("get_relays", (channel,)))
        return {"state": 0x21, "pulsing": 0}

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
        return {"state": 0, "pulsing": 0, "request_count": 12, "last_error": 0}

    def reboot(self) -> dict[str, Any]:
        self.calls.append(("reboot", ()))
        return {"reboot": True}


@pytest.fixture(autouse=True)
def fake_client(monkeypatch: pytest.MonkeyPatch) -> None:
    FakeClient.instances = []
    FakeClient.failure = None
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


def test_set_converts_one_based_channel_to_zero_based() -> None:
    rc = cli.main(["--port", "COM7", "set", "6", "on"])

    assert rc == cli.EXIT_OK
    assert FakeClient.instances[0].calls == [("set_relay", (5, True))]


def test_set_all_accepts_hex_mask() -> None:
    rc = cli.main(["--port", "COM7", "set-all", "0x21"])

    assert rc == cli.EXIT_OK
    assert FakeClient.instances[0].calls == [("set_all_relays", (0x21,))]


def test_get_channel_human_output_names_enabled_relays(
    capsys: pytest.CaptureFixture[str],
) -> None:
    rc = cli.main(["--port", "COM7", "get", "1"])

    captured = capsys.readouterr()

    assert rc == cli.EXIT_OK
    assert "on: CH1, CH6" in captured.out
    assert FakeClient.instances[0].calls == [("get_relays", (0,))]


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


def test_smoke_pulses_each_relay_and_forces_teardown() -> None:
    rc = cli.main(["--port", "COM7", "smoke", "--pulse-ms", "25"])

    calls = FakeClient.instances[0].calls

    assert rc == cli.EXIT_OK
    assert calls[:2] == [("get_info", ()), ("get_status", ())]
    assert [call for call in calls if call[0] == "pulse_relay"] == [
        ("pulse_relay", (0, 25)),
        ("pulse_relay", (1, 25)),
        ("pulse_relay", (2, 25)),
        ("pulse_relay", (3, 25)),
        ("pulse_relay", (4, 25)),
        ("pulse_relay", (5, 25)),
    ]
    assert calls[-1:] == [("off_all", ())]
