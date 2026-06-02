from __future__ import annotations

import io
from dataclasses import dataclass
from typing import Any

import pytest
from prompt_toolkit.completion import Completer
from prompt_toolkit.history import InMemoryHistory

from rp2350_relay_6ch.discovery import RelayUsbDevice, list_relay_devices, select_device_by_serial
from rp2350_relay_6ch.exceptions import RelayTimeoutError, RelayTransportError
from rp2350_relay_6ch.session import (
    HEARTBEAT_INTERVAL_S,
    HeartbeatPoller,
    RelaySession,
    RelaySessionCompleter,
    SessionOptions,
    _ConnectResult,
    _format_heartbeat_error,
    _format_prompt,
)
import rp2350_relay_6ch.session as session_module


@dataclass
class FakePort:
    device: str
    vid: int | None
    pid: int | None
    product: str | None
    serial_number: str | None


class FakeHeartbeat:
    instances: list["FakeHeartbeat"] = []

    def __init__(self, client: "SessionFakeClient", output: io.StringIO) -> None:
        self.client = client
        self.output = output
        self.started = False
        self.stopped = False
        FakeHeartbeat.instances.append(self)

    def start(self) -> None:
        self.started = True

    def stop(self) -> None:
        self.stopped = True


class SessionFakeClient:
    instances: list["SessionFakeClient"] = []
    failing_ports: set[str] = set()
    fail_status = False
    relay_state = 0
    relay_pulsing = 0
    status_uptimes: list[int | None] = []

    def __init__(self, port: str, **kwargs: Any) -> None:
        self.port = port
        self.kwargs = kwargs
        self.calls: list[tuple[str, tuple[Any, ...]]] = []
        self.closed = False
        SessionFakeClient.instances.append(self)

    @classmethod
    def connect(cls, port: str, **kwargs: Any) -> "SessionFakeClient":
        if port in cls.failing_ports:
            raise RelayTransportError(f"cannot open {port}")
        return cls(port, **kwargs)

    def close(self) -> None:
        self.closed = True

    def identity(self) -> dict[str, Any]:
        self.calls.append(("identity", ()))
        return {
            "command_model_version": 2,
            "hardware": "Waveshare RP2350-Relay-6CH",
            "protocol_version": 7,
            "relay_count": 6,
        }

    def capabilities(self) -> dict[str, Any]:
        self.calls.append(("capabilities", ()))
        return {"capabilities": 31}

    def status(self) -> dict[str, Any]:
        self.calls.append(("status", ()))
        if self.fail_status:
            raise RelayTransportError("lost")
        status = {"state": self.relay_state, "pulsing": self.relay_pulsing}
        status.update(
            {
                "health": "degraded",
                "health_reasons": 8,
                "health_primary_reason": "comm_owner_timeout",
                "health_transitions": 3,
            }
        )
        if self.status_uptimes:
            uptime = self.status_uptimes.pop(0)
            if uptime is not None:
                status["uptime_ms"] = uptime
        return status

    def build_info(self) -> dict[str, Any]:
        self.calls.append(("build_info", ()))
        return {"app_version": "0.8.0"}

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
        return {"state": self.relay_state, "pulsing": self.relay_pulsing}

    def set_relay(self, channel: int, on: bool) -> dict[str, Any]:
        self.calls.append(("set_relay", (channel, on)))
        self.relay_state = 1 << channel if on else 0
        return {"state": self.relay_state, "pulsing": self.relay_pulsing}

    def set_all_relays(self, state: int) -> dict[str, Any]:
        self.calls.append(("set_all_relays", (state,)))
        self.relay_state = state
        return {"state": state, "pulsing": self.relay_pulsing}

    def pulse_relay(self, channel: int, duration_ms: int) -> dict[str, Any]:
        self.calls.append(("pulse_relay", (channel, duration_ms)))
        self.relay_state = 1 << channel
        self.relay_pulsing = 1 << channel
        return {"state": self.relay_state, "pulsing": self.relay_pulsing}

    def off_all(self) -> dict[str, Any]:
        self.calls.append(("off_all", ()))
        self.relay_state = 0
        self.relay_pulsing = 0
        return {"state": 0, "pulsing": 0}

    def reboot(self) -> dict[str, Any]:
        self.calls.append(("reboot", ()))
        return {"ok": True}

    def heartbeat(self) -> dict[str, Any]:
        self.calls.append(("heartbeat", ()))
        return {"ok": True}


@pytest.fixture(autouse=True)
def reset_fakes() -> None:
    SessionFakeClient.instances = []
    SessionFakeClient.failing_ports = set()
    SessionFakeClient.fail_status = False
    SessionFakeClient.relay_state = 0
    SessionFakeClient.relay_pulsing = 0
    SessionFakeClient.status_uptimes = []
    FakeHeartbeat.instances = []


def make_session(
    *,
    port: str | None = "COM7",
    serial: str | None = None,
    discover: Any | None = None,
    selector: Any | None = None,
) -> tuple[RelaySession, io.StringIO]:
    output = io.StringIO()
    session = RelaySession(
        SessionOptions(port=port, serial=serial, baud=115200, timeout=2.0, retries=1),
        input_stream=io.StringIO(),
        output=output,
        client_factory=SessionFakeClient.connect,
        discover=discover or (lambda: []),
        serial_selector=selector
        or (lambda value: RelayUsbDevice("COM9", value, "RP2350-Relay-6CH")),
        heartbeat_factory=lambda client, out: FakeHeartbeat(client, out),
        sleep=lambda seconds: None,
    )
    return session, output


def test_discovery_filters_relay_usb_metadata() -> None:
    devices = list_relay_devices(
        [
            FakePort("COM7", 0x2E8A, 0x0009, "RP2350-Relay-6CH", "abc"),
            FakePort("COM8", 0x2E8A, 0x0009, "Other", "def"),
            FakePort("COM9", 0x1234, 0x0009, "RP2350-Relay-6CH", "ghi"),
        ]
    )

    assert devices == [RelayUsbDevice("COM7", "abc", "RP2350-Relay-6CH", True)]


def test_discovery_accepts_vid_pid_serial_candidate_without_product() -> None:
    devices = list_relay_devices(
        [
            FakePort("COM7", 0x2E8A, 0x0009, None, "B905D541EF8C32DB"),
            FakePort("COM8", 0x2E8A, 0x0009, None, None),
        ]
    )

    assert devices == [RelayUsbDevice("COM7", "B905D541EF8C32DB", None, False)]


def test_discovery_rejects_wrong_vid_pid_even_with_serial() -> None:
    devices = list_relay_devices(
        [
            FakePort("COM7", 0x1234, 0x0009, None, "abc"),
            FakePort("COM8", 0x2E8A, 0x1234, None, "def"),
        ]
    )

    assert devices == []


def test_discovery_exact_serial_ignores_serialless_devices() -> None:
    device = select_device_by_serial(
        "abc",
        [
            FakePort("COM7", 0x2E8A, 0x0009, "RP2350-Relay-6CH", None),
            FakePort("COM8", 0x2E8A, 0x0009, "RP2350-Relay-6CH", "abc"),
        ],
    )

    assert device.port == "COM8"


def test_discovery_exact_serial_selects_unverified_candidate() -> None:
    device = select_device_by_serial(
        "B905D541EF8C32DB",
        [FakePort("COM7", 0x2E8A, 0x0009, None, "B905D541EF8C32DB")],
    )

    assert device == RelayUsbDevice("COM7", "B905D541EF8C32DB", None, False)


def test_startup_opens_one_client_runs_identity_status_and_starts_heartbeat() -> None:
    session, output = make_session(port="COM7")

    assert session._connect_from_options(session.options) is _ConnectResult.CONNECTED

    assert len(SessionFakeClient.instances) == 1
    assert SessionFakeClient.instances[0].calls == [("identity", ()), ("status", ())]
    assert FakeHeartbeat.instances[0].started is True
    text = output.getvalue()
    assert "╭" in text
    assert "RP2350 Relay Session" in text
    assert "Connection:   connected" in text
    assert "Port:         COM7" in text
    assert "Protocol:     7" in text
    assert "State:        0x00" in text
    assert "Pulsing:      none" in text


def test_session_heartbeat_interval_is_fixed_2_5_seconds() -> None:
    assert HEARTBEAT_INTERVAL_S == 2.5


def test_startup_with_explicit_port_attaches_matching_usb_metadata() -> None:
    devices = [RelayUsbDevice("COM7", "abc", "RP2350-Relay-6CH")]
    session, output = make_session(port="COM7", discover=lambda: devices)

    assert session._connect_from_options(session.options) is _ConnectResult.CONNECTED

    assert session.usb_serial == "abc"
    assert session.product == "RP2350-Relay-6CH"
    assert "Serial:       abc" in output.getvalue()
    assert SessionFakeClient.instances[0].port == "COM7"


def test_startup_with_explicit_port_attaches_unverified_usb_metadata() -> None:
    devices = [RelayUsbDevice("COM7", "B905D541EF8C32DB", None, False)]
    session, output = make_session(port="COM7", discover=lambda: devices)

    assert session._connect_from_options(session.options) is _ConnectResult.CONNECTED

    assert session.usb_serial == "B905D541EF8C32DB"
    assert session.product is None
    assert "Serial:       B905D541EF8C32DB" in output.getvalue()
    assert SessionFakeClient.instances[0].port == "COM7"


def test_explicit_port_metadata_lookup_does_not_substitute_other_port() -> None:
    devices = [RelayUsbDevice("COM8", "abc", "RP2350-Relay-6CH")]
    session, output = make_session(port="COM7", discover=lambda: devices)

    assert session._connect_from_options(session.options) is _ConnectResult.CONNECTED

    assert session.usb_serial is None
    assert "Serial:       unknown" in output.getvalue()
    assert SessionFakeClient.instances[0].port == "COM7"


def test_explicit_port_opens_when_metadata_lookup_fails() -> None:
    def discover() -> list[RelayUsbDevice]:
        raise RelayTransportError("metadata unavailable")

    session, output = make_session(port="COM7", discover=discover)

    assert session._connect_from_options(session.options) is _ConnectResult.CONNECTED

    assert session.usb_serial is None
    assert "Serial:       unknown" in output.getvalue()
    assert SessionFakeClient.instances[0].port == "COM7"


def test_interactive_selection_is_shown_even_for_one_device() -> None:
    output = io.StringIO()
    session = RelaySession(
        SessionOptions(port=None, serial=None, baud=115200, timeout=2.0, retries=1),
        input_stream=io.StringIO("1\n"),
        output=output,
        client_factory=SessionFakeClient.connect,
        discover=lambda: [RelayUsbDevice("COM7", None, "RP2350-Relay-6CH")],
        heartbeat_factory=lambda client, out: FakeHeartbeat(client, out),
    )

    assert session._connect_from_options(session.options) is _ConnectResult.CONNECTED

    assert "Relay USB candidates:" in output.getvalue()
    assert "Serial:       unknown" in output.getvalue()
    assert SessionFakeClient.instances[0].port == "COM7"


def test_interactive_selection_marks_productless_candidate_unverified() -> None:
    output = io.StringIO()
    session = RelaySession(
        SessionOptions(port=None, serial=None, baud=115200, timeout=2.0, retries=1),
        input_stream=io.StringIO("1\n"),
        output=output,
        client_factory=SessionFakeClient.connect,
        discover=lambda: [RelayUsbDevice("COM7", "B905D541EF8C32DB", None, False)],
        heartbeat_factory=lambda client, out: FakeHeartbeat(client, out),
    )

    assert session._connect_from_options(session.options) is _ConnectResult.CONNECTED

    text = output.getvalue()
    assert "Relay USB candidates:" in text
    assert "product=unknown status=unverified" in text
    assert "Port:         COM7" in text
    assert "Serial:       B905D541EF8C32DB" in text


def test_startup_without_matching_devices_enters_disconnected_prompt_and_exits_zero() -> None:
    session, output = make_session(port=None, discover=lambda: [])

    assert session.run() == 0
    assert session.connected is False
    assert "no relay USB serial devices found" in output.getvalue()
    assert "rp2350-relay[disconnected]$ " in output.getvalue()


def test_startup_selection_cancel_exits_without_disconnected_prompt() -> None:
    devices = [RelayUsbDevice("/dev/ttyACM0", "abc", "RP2350-Relay-6CH SMP CDC")]
    output = io.StringIO()
    session = RelaySession(
        SessionOptions(port=None, serial=None, baud=115200, timeout=2.0, retries=1),
        input_stream=io.StringIO("q\n"),
        output=output,
        client_factory=SessionFakeClient.connect,
        discover=lambda: devices,
        heartbeat_factory=lambda client, out: FakeHeartbeat(client, out),
    )

    assert session.run() == 0

    text = output.getvalue()
    assert "Relay USB candidates:" in text
    assert "port=/dev/ttyACM0 serial=abc product=RP2350-Relay-6CH SMP CDC" in text
    assert "disconnected: run 'connect'" not in text
    assert "rp2350-relay[disconnected]$ " not in text
    assert SessionFakeClient.instances == []


def test_missing_startup_serial_lists_available_devices_and_plain_connect_falls_back() -> None:
    selector_calls: list[str] = []
    devices = [RelayUsbDevice("COM8", "available", "RP2350-Relay-6CH")]

    def selector(usb_serial: str) -> RelayUsbDevice:
        selector_calls.append(usb_serial)
        raise RelayTransportError(f"no relay device found with USB serial {usb_serial}")

    output = io.StringIO()
    session = RelaySession(
        SessionOptions(port=None, serial="wanted", baud=115200, timeout=2.0, retries=1),
        input_stream=io.StringIO("1\n"),
        output=output,
        client_factory=SessionFakeClient.connect,
        discover=lambda: devices,
        serial_selector=selector,
        heartbeat_factory=lambda client, out: FakeHeartbeat(client, out),
    )

    assert session._connect_from_options(session.options, initial=True) is _ConnectResult.FAILED
    assert session.preferred_serial == "wanted"
    assert "Relay USB candidates:" in output.getvalue()
    assert "serial=available" in output.getvalue()

    session.handle_line("connect")

    assert selector_calls == ["wanted", "wanted"]
    assert session.connected is True
    assert SessionFakeClient.instances[0].port == "COM8"


def test_failed_startup_port_lists_available_devices_and_plain_connect_falls_back() -> None:
    SessionFakeClient.failing_ports = {"COM7"}
    devices = [RelayUsbDevice("COM8", "available", "RP2350-Relay-6CH")]
    output = io.StringIO()
    session = RelaySession(
        SessionOptions(port="COM7", serial=None, baud=115200, timeout=2.0, retries=1),
        input_stream=io.StringIO("1\n"),
        output=output,
        client_factory=SessionFakeClient.connect,
        discover=lambda: devices,
        heartbeat_factory=lambda client, out: FakeHeartbeat(client, out),
    )

    assert session._connect_from_options(session.options, initial=True) is _ConnectResult.FAILED
    assert session.preferred_port == "COM7"
    assert "transport error: cannot open COM7" in output.getvalue()
    assert "serial=available" in output.getvalue()

    session.handle_line("connect")

    assert session.connected is True
    assert [client.port for client in SessionFakeClient.instances] == ["COM8"]


def test_session_commands_reuse_same_client_and_one_based_channels() -> None:
    session, _output = make_session(port="COM7")
    session._connect_from_options(session.options)

    session.handle_line("set 6 on")
    session.handle_line("get 6")

    client = SessionFakeClient.instances[0]
    assert client.calls[-2:] == [("set_relay", (5, True)), ("get_relays", (5,))]
    assert len(SessionFakeClient.instances) == 1


def test_ctrl_c_cancels_input_line_without_closing_session(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    session, output = make_session(port="COM7")
    reads = 0

    def fake_read_input(prompt: str) -> str:
        nonlocal reads
        del prompt
        reads += 1
        if reads == 1:
            raise KeyboardInterrupt
        return "exit"

    monkeypatch.setattr(session, "_read_input", fake_read_input)

    assert session.run() == 0

    client = SessionFakeClient.instances[0]
    assert client.closed is True
    assert client.calls == [("identity", ()), ("status", ()), ("status", ())]
    assert "^C" in output.getvalue()


def test_connected_prompt_includes_port() -> None:
    session, _output = make_session(port="COM7")
    session._connect_from_options(session.options)

    assert session._prompt() == "rp2350-relay[COM7]$ "


def test_disconnected_prompt_uses_bracket_style() -> None:
    session, _output = make_session(port=None)

    assert session._prompt() == "rp2350-relay[disconnected]$ "


def test_colored_prompt_uses_green_name_blue_state_and_plain_dollar() -> None:
    assert _format_prompt("COM7", color=True) == (
        "\033[32mrp2350-relay\033[0m[\033[34mCOM7\033[0m]$ "
    )


def test_help_output_is_grouped_and_mentions_safe_exit() -> None:
    session, output = make_session(port=None)

    session.handle_line("help")

    text = output.getvalue()
    assert "Inspect:" in text
    assert "Control:" in text
    assert "Connection:" in text
    assert "Exit:" in text
    assert "Notes:" in text
    assert "smoke [--pulse-ms <ms>]" in text
    assert "Run off-all before disconnecting or exiting." in text
    assert "Use --force only when you intentionally accept unknown or active relay state." in text


def test_session_smoke_pulses_each_relay_and_tears_down() -> None:
    session, output = make_session(port="COM7")
    sleeps: list[float] = []
    session.sleep = sleeps.append
    session._connect_from_options(session.options)

    session.handle_line("smoke --pulse-ms 25")

    calls = SessionFakeClient.instances[0].calls
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
    assert "smoke test passed" in output.getvalue()


def test_connected_completion_suggests_all_session_commands() -> None:
    completer = RelaySessionCompleter(lambda: True)

    assert completer.complete("") == [
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
    ]


def test_session_completer_uses_prompt_toolkit_base_class() -> None:
    completer = RelaySessionCompleter(lambda: True)

    assert isinstance(completer, Completer)
    assert hasattr(completer, "get_completions_async")


def test_session_uses_in_memory_prompt_history() -> None:
    session, _output = make_session(port=None)

    assert isinstance(session.history, InMemoryHistory)


def test_prompt_toolkit_input_receives_session_history(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    session, _output = make_session(port=None)
    captured: dict[str, Any] = {}

    def fake_prompt(message: object, **kwargs: Any) -> str:
        captured["message"] = message
        captured.update(kwargs)
        return "status"

    monkeypatch.setattr(session, "_prompt_toolkit_enabled", lambda: True)
    monkeypatch.setattr(session_module, "prompt_toolkit_prompt", fake_prompt)
    monkeypatch.setattr(session_module.sys, "stdin", session.input_stream)
    monkeypatch.setattr(session_module.sys, "stdout", session.output)

    assert session._read_input("rp2350-relay[disconnected]$ ") == "status"
    assert captured["completer"] is session.completer
    assert captured["history"] is session.history


def test_disconnected_completion_suggests_only_available_commands() -> None:
    completer = RelaySessionCompleter(lambda: False)

    assert completer.complete("") == ["connect", "help", "exit", "quit"]
    assert completer.complete("s") == []


def test_completion_filters_partial_commands() -> None:
    completer = RelaySessionCompleter(lambda: True)

    assert completer.complete("st") == ["status"]
    assert completer.complete("set") == ["set", "set-all"]


def test_completion_suggests_channel_arguments() -> None:
    completer = RelaySessionCompleter(lambda: True)

    assert completer.complete("get ") == ["1", "2", "3", "4", "5", "6"]
    assert completer.complete("set ") == ["1", "2", "3", "4", "5", "6"]
    assert completer.complete("pulse ") == ["1", "2", "3", "4", "5", "6"]


def test_completion_suggests_set_state_argument() -> None:
    completer = RelaySessionCompleter(lambda: True)

    assert completer.complete("set 1 ") == ["on", "off"]
    assert completer.complete("set 1 o") == ["on", "off"]


def test_completion_suggests_supported_options() -> None:
    completer = RelaySessionCompleter(lambda: True)

    assert completer.complete("connect --") == ["--port", "--serial"]
    assert completer.complete("disconnect --") == ["--force"]
    assert completer.complete("exit --") == ["--force"]
    assert completer.complete("quit --") == ["--force"]


def test_disconnect_refuses_when_relay_is_on_and_force_closes_without_off_all() -> None:
    session, output = make_session(port="COM7")
    session._connect_from_options(session.options)
    SessionFakeClient.relay_state = 1

    session.handle_line("disconnect")
    assert session.connected is True
    assert "refusing to close" in output.getvalue()

    session.handle_line("disconnect --force")
    client = SessionFakeClient.instances[0]
    assert session.connected is False
    assert client.closed is True
    assert ("off_all", ()) not in client.calls
    assert FakeHeartbeat.instances[0].stopped is True


def test_disconnected_mode_rejects_relay_commands_and_allows_connect() -> None:
    session, output = make_session(port=None, discover=lambda: [])

    session.handle_line("status")
    session.handle_line("connect --port COM7")

    assert "not connected" in output.getvalue()
    assert session.connected is True


def test_status_prints_compact_health_line() -> None:
    session, output = make_session(port="COM7")
    session._connect_from_options(session.options)

    session.handle_line("status")

    text = output.getvalue()
    assert "[health]" in text
    assert "state:           degraded" in text
    assert "primary_reason:  comm_owner_timeout" in text


def test_connect_port_attaches_matching_usb_metadata() -> None:
    devices = [RelayUsbDevice("COM7", "abc", "RP2350-Relay-6CH")]
    session, output = make_session(port=None, discover=lambda: devices)

    session.handle_line("connect --port COM7")

    assert session.connected is True
    assert session.usb_serial == "abc"
    assert "Serial:       abc" in output.getvalue()


def test_transport_failure_enters_disconnected_state() -> None:
    session, output = make_session(port="COM7")
    session._connect_from_options(session.options)
    SessionFakeClient.fail_status = True

    session.handle_line("status")

    assert session.connected is False
    assert "transport error: lost" in output.getvalue()


def run_fake_heartbeat_poller(outcomes: list[RelayTimeoutError | dict[str, Any]]) -> str:
    poll_count = len(outcomes)

    class FakeEvent:
        def __init__(self) -> None:
            self.waits = 0

        def set(self) -> None:
            pass

        def wait(self, timeout: float) -> bool:
            del timeout
            self.waits += 1
            return self.waits > poll_count

    class FakeThread:
        def __init__(self, *, target: Any, daemon: bool) -> None:
            del daemon
            self.target = target

        def start(self) -> None:
            self.target()

        def join(self, timeout: float) -> None:
            del timeout

    output = io.StringIO()

    def heartbeat() -> dict[str, Any]:
        outcome = outcomes.pop(0)
        if isinstance(outcome, RelayTimeoutError):
            raise outcome
        return outcome

    poller = HeartbeatPoller(
        heartbeat,
        interval_s=0.1,
        output=output,
        stop_event_factory=FakeEvent,
        thread_factory=FakeThread,
    )

    poller.start()
    assert outcomes == []
    return output.getvalue()


def test_heartbeat_success_from_start_stays_silent() -> None:
    output = run_fake_heartbeat_poller([{"ok": True}, {"ok": True}])

    assert output == ""


def test_heartbeat_failure_warns_without_stopping_poller() -> None:
    output = run_fake_heartbeat_poller([RelayTimeoutError("late"), {"ok": True}])

    assert "heartbeat: no response" in output
    assert "heartbeat: restored" in output
    assert "late" not in output


def test_heartbeat_restored_prints_once_after_failure_streak() -> None:
    output = run_fake_heartbeat_poller(
        [
            RelayTimeoutError("first"),
            RelayTimeoutError("second"),
            {"ok": True},
            {"ok": True},
        ]
    )

    assert output.count("heartbeat: no response") == 2
    assert output.count("heartbeat: restored") == 1


def test_heartbeat_transport_failure_uses_concise_status_message() -> None:
    output = _format_heartbeat_error(
        RelayTransportError(
            "[Errno 2] could not open port /dev/ttyACM0: "
            "[Errno 2] No such file or directory: '/dev/ttyACM0'"
        )
    )

    assert output == "heartbeat: serial link unavailable"
    assert "/dev/ttyACM0" not in output
    assert "No such file or directory" not in output


def test_reboot_closes_client_and_reconnects_by_serial() -> None:
    SessionFakeClient.status_uptimes = [10_000, 10_700, 11_000, 250]
    selector_calls: list[str] = []

    def selector(usb_serial: str) -> RelayUsbDevice:
        selector_calls.append(usb_serial)
        return RelayUsbDevice("COM9", usb_serial, "RP2350-Relay-6CH")

    session, output = make_session(port=None, serial="abc", selector=selector)
    session._connect_from_options(session.options)

    session.handle_line("reboot")

    assert selector_calls == ["abc", "abc", "abc"]
    assert SessionFakeClient.instances[0].closed is True
    assert SessionFakeClient.instances[1].closed is True
    assert SessionFakeClient.instances[2].port == "COM9"
    assert session.connected is True
    assert "reboot requested" in output.getvalue()
    assert output.getvalue().count("Connection:   connected") == 2
    assert len(FakeHeartbeat.instances) == 2


def test_reboot_after_explicit_port_reconnects_by_attached_serial() -> None:
    SessionFakeClient.status_uptimes = [10_000, 250]
    selector_calls: list[str] = []

    def selector(usb_serial: str) -> RelayUsbDevice:
        selector_calls.append(usb_serial)
        return RelayUsbDevice("COM9", usb_serial, "RP2350-Relay-6CH")

    devices = [RelayUsbDevice("COM7", "abc", "RP2350-Relay-6CH")]
    session, output = make_session(
        port="COM7",
        discover=lambda: devices,
        selector=selector,
    )
    session._connect_from_options(session.options)

    session.handle_line("reboot")

    assert selector_calls == ["abc"]
    assert SessionFakeClient.instances[0].closed is True
    assert SessionFakeClient.instances[1].port == "COM9"
    assert session.connected is True
    assert "Port:         COM9" in output.getvalue()
    assert "Serial:       abc" in output.getvalue()


def test_reboot_reconnect_accepts_missing_uptime_after_settle_delay() -> None:
    SessionFakeClient.status_uptimes = [None, None]
    sleeps: list[float] = []

    def selector(usb_serial: str) -> RelayUsbDevice:
        return RelayUsbDevice("COM9", usb_serial, "RP2350-Relay-6CH")

    session, _output = make_session(port=None, serial="abc", selector=selector)
    session.sleep = sleeps.append
    session._connect_from_options(session.options)

    session.handle_line("reboot")

    assert sleeps == [session_module.REBOOT_RECONNECT_SETTLE_S]
    assert SessionFakeClient.instances[0].closed is True
    assert SessionFakeClient.instances[1].port == "COM9"
    assert session.connected is True
