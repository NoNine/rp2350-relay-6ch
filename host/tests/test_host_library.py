from __future__ import annotations

import sys
from types import SimpleNamespace

import pytest

from rp2350_relay_6ch import RelayClient, SerialSmpTransport, SimulatedPacketTransport
from rp2350_relay_6ch.constants import (
    CMD_BUILD_INFO,
    CMD_CAPABILITIES,
    CMD_GET,
    CMD_GET_ALL,
    CMD_HEARTBEAT,
    CMD_HEALTH,
    CMD_IDENTITY,
    CMD_OFF_ALL,
    CMD_PULSE,
    CMD_SAFETY,
    CMD_SET,
    CMD_SET_ALL,
    ERR_BUSY,
    ERR_REBOOT_FAILED,
    GROUP_RELAY,
    OP_READ_RSP,
    OP_WRITE_RSP,
    COMMAND_MODEL_VERSION,
    DEVICE_ERROR_NAMES,
    PROTOCOL_VERSION,
    CMD_STATUS,
    CMD_TRANSPORT,
    CMD_WATCHDOG,
)
from rp2350_relay_6ch.exceptions import (
    RelayDeviceError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)
from rp2350_relay_6ch.smp import (
    SmpPacket,
    decode_packet,
    decode_payload,
    encode_packet,
    encode_payload,
    serial_frames,
)


class FakeSerial:
    def __init__(
        self,
        *,
        responses: list[bytes] | None = None,
        read_error: Exception | None = None,
        **kwargs: object,
    ) -> None:
        self.kwargs = kwargs
        self.responses = responses or []
        self.read_error = read_error
        self.timeout: float | None = kwargs.get("timeout")  # type: ignore[assignment]
        self.write_timeout: float | None = kwargs.get("write_timeout")  # type: ignore[assignment]
        self.writes: list[bytes] = []
        self.reset_count = 0
        self.closed = False

    def reset_input_buffer(self) -> None:
        self.reset_count += 1

    def write(self, data: bytes) -> None:
        self.writes.append(data)

    def readline(self) -> bytes:
        if self.read_error is not None:
            raise self.read_error
        if not self.responses:
            return b""
        return self.responses.pop(0)

    def close(self) -> None:
        self.closed = True


def response(command: int, seq: int, payload: dict[str, object], op: int = OP_READ_RSP) -> bytes:
    return encode_packet(
        SmpPacket(
            op=op,
            group=GROUP_RELAY,
            command=command,
            seq=seq,
            payload=encode_payload(payload),
        )
    )


def request_payload(transport: SimulatedPacketTransport, index: int = 0) -> dict[str, object]:
    packet = decode_packet(transport.requests[index])
    return decode_payload(packet.payload)


def framed_response(
    command: int,
    seq: int,
    payload: dict[str, object],
    op: int = OP_READ_RSP,
) -> list[bytes]:
    return serial_frames(response(command, seq, payload, op))


def install_fake_serial(
    monkeypatch: pytest.MonkeyPatch,
    serials: list[FakeSerial],
    *,
    read_error: Exception | None = None,
) -> None:
    def serial_factory(**kwargs: object) -> FakeSerial:
        fake = FakeSerial(
            responses=[
                *framed_response(CMD_GET, 0, {"state": 0}),
                *framed_response(CMD_GET, 1, {"state": 1}),
            ],
            read_error=read_error,
            **kwargs,
        )
        serials.append(fake)
        return fake

    monkeypatch.setitem(sys.modules, "serial", SimpleNamespace(Serial=serial_factory))


def test_cbor2_encodes_and_decodes_payload_maps() -> None:
    payload = {
        "channel": 2,
        "on": True,
        "duration_ms": 100,
        "transport": "usb_cdc_acm_smp",
    }

    assert decode_payload(encode_payload(payload)) == payload


def test_smp_packet_round_trips_header_and_payload() -> None:
    packet = SmpPacket(
        op=OP_WRITE_RSP,
        group=GROUP_RELAY,
        command=CMD_SET,
        seq=7,
        payload=encode_payload({"state": 1}),
    )

    assert decode_packet(encode_packet(packet)) == packet


def test_smp_package_encodes_serial_frames() -> None:
    packet = encode_packet(
        SmpPacket(
            op=OP_READ_RSP,
            group=GROUP_RELAY,
            command=CMD_GET,
            seq=1,
            payload=encode_payload({"state": 0}),
        )
    )

    frames = serial_frames(packet)

    assert frames
    assert frames[0].startswith(b"\x06\x09")


def test_get_info_is_not_part_of_role_oriented_host_api() -> None:
    assert not hasattr(RelayClient(SimulatedPacketTransport()), "get_info")


@pytest.mark.parametrize(
    ("method_name", "command", "payload"),
    [
        (
            "identity",
            CMD_IDENTITY,
            {"protocol_version": 8, "command_model_version": 2, "relay_count": 6},
        ),
        ("capabilities", CMD_CAPABILITIES, {"capabilities": 31}),
        ("build_info", CMD_BUILD_INFO, {"app_version": "0.8.0"}),
        ("status", CMD_STATUS, {"state": 0, "pulsing": 0, "health": "normal"}),
        ("health", CMD_HEALTH, {"health": "normal"}),
        ("transport_status", CMD_TRANSPORT, {"transport": "usb_cdc_acm_smp"}),
        ("safety", CMD_SAFETY, {"comm_loss_policy": "energized_only"}),
        ("watchdog", CMD_WATCHDOG, {"watchdog_enabled": False}),
    ],
)
def test_role_read_methods_each_send_one_read_request(
    method_name: str,
    command: int,
    payload: dict[str, object],
) -> None:
    transport = SimulatedPacketTransport([response(command, 0, payload)])
    client = RelayClient(transport)

    assert getattr(client, method_name)() == payload

    assert len(transport.requests) == 1
    packet = decode_packet(transport.requests[0])
    assert packet.op == 0
    assert packet.command == command
    assert request_payload(transport) == {}


def test_protocol_8_command_ids_are_grouped_by_role() -> None:
    assert PROTOCOL_VERSION == 8
    assert COMMAND_MODEL_VERSION == 2
    assert CMD_IDENTITY == 0x00
    assert CMD_CAPABILITIES == 0x01
    assert CMD_BUILD_INFO == 0x02
    assert CMD_GET == 0x10
    assert CMD_GET_ALL == 0x11
    assert CMD_STATUS == 0x12
    assert CMD_HEALTH == 0x13
    assert CMD_TRANSPORT == 0x14
    assert CMD_SAFETY == 0x15
    assert CMD_WATCHDOG == 0x16
    assert CMD_SET == 0x20
    assert CMD_SET_ALL == 0x21
    assert CMD_PULSE == 0x22
    assert CMD_HEARTBEAT == 0x30


def test_heartbeat_sends_write_request_with_empty_payload() -> None:
    transport = SimulatedPacketTransport(
        [response(CMD_HEARTBEAT, 0, {"ok": True}, OP_WRITE_RSP)]
    )
    client = RelayClient(transport)

    assert client.heartbeat() == {"ok": True}

    packet = decode_packet(transport.requests[0])
    assert packet.op == 2
    assert packet.command == CMD_HEARTBEAT
    assert request_payload(transport) == {}


def test_set_relay_sends_write_request_payload() -> None:
    transport = SimulatedPacketTransport([response(CMD_SET, 0, {"state": 1}, OP_WRITE_RSP)])
    client = RelayClient(transport)

    assert client.set_relay(0, True) == {"state": 1}

    packet = decode_packet(transport.requests[0])
    assert packet.op == 2
    assert packet.command == CMD_SET
    assert request_payload(transport) == {"channel": 0, "on": True}


def test_set_all_and_pulse_wrappers_validate_and_encode_payloads() -> None:
    transport = SimulatedPacketTransport(
        [
            response(CMD_SET_ALL, 0, {"state": 0x21}, OP_WRITE_RSP),
            response(CMD_PULSE, 1, {"state": 1, "pulsing": 1}, OP_WRITE_RSP),
        ]
    )
    client = RelayClient(transport)

    assert client.set_all_relays(0x21) == {"state": 0x21}
    assert client.pulse_relay(0, 100) == {"state": 1, "pulsing": 1}
    assert request_payload(transport, 0) == {"state": 0x21}
    assert request_payload(transport, 1) == {"channel": 0, "duration_ms": 100}


def test_get_relay_sends_single_relay_read_request() -> None:
    transport = SimulatedPacketTransport(
        [response(CMD_GET, 0, {"channel": 0, "on": True, "pulsing": False})]
    )
    client = RelayClient(transport)

    assert client.get_relay(0) == {"channel": 0, "on": True, "pulsing": False}

    packet = decode_packet(transport.requests[0])
    assert packet.op == 0
    assert packet.command == CMD_GET
    assert request_payload(transport) == {"channel": 0}


def test_get_all_relays_sends_all_relay_read_request() -> None:
    transport = SimulatedPacketTransport(
        [response(CMD_GET_ALL, 0, {"state": 0x21, "pulsing": 0})]
    )
    client = RelayClient(transport)

    assert client.get_all_relays() == {"state": 0x21, "pulsing": 0}

    packet = decode_packet(transport.requests[0])
    assert packet.op == 0
    assert packet.command == CMD_GET_ALL
    assert request_payload(transport) == {}


def test_off_all_relays_sends_write_request() -> None:
    transport = SimulatedPacketTransport(
        [response(CMD_OFF_ALL, 0, {"state": 0, "pulsing": 0}, OP_WRITE_RSP)]
    )
    client = RelayClient(transport)

    assert client.off_all_relays() == {"state": 0, "pulsing": 0}

    packet = decode_packet(transport.requests[0])
    assert packet.op == 2
    assert packet.command == CMD_OFF_ALL
    assert request_payload(transport) == {}


def test_invalid_host_arguments_raise_validation_errors() -> None:
    client = RelayClient(SimulatedPacketTransport())

    with pytest.raises(RelayValidationError):
        client.get_relay(6)
    with pytest.raises(RelayValidationError):
        client.set_relay(0, "on")  # type: ignore[arg-type]
    with pytest.raises(RelayValidationError):
        client.set_all_relays(0x40)
    with pytest.raises(RelayValidationError):
        client.pulse_relay(0, 1)


def test_device_error_payload_raises_structured_exception() -> None:
    transport = SimulatedPacketTransport(
        [
            response(
                CMD_PULSE,
                0,
                {"err": {"group": GROUP_RELAY, "rc": ERR_BUSY}},
                OP_WRITE_RSP,
            )
        ]
    )
    client = RelayClient(transport)

    with pytest.raises(RelayDeviceError) as exc:
        client.pulse_relay(0, 100)

    assert exc.value.group == GROUP_RELAY
    assert exc.value.rc == ERR_BUSY
    assert "busy" in str(exc.value)


def test_reboot_failed_error_name_is_mapped() -> None:
    assert ERR_REBOOT_FAILED == 6
    assert DEVICE_ERROR_NAMES[ERR_REBOOT_FAILED] == "reboot_failed"


def test_mismatched_response_header_raises_protocol_error() -> None:
    transport = SimulatedPacketTransport([response(CMD_GET, 99, {"state": 0})])
    client = RelayClient(transport)

    with pytest.raises(RelayProtocolError, match="sequence"):
        client.get_relay(0)


def test_timeout_retries_before_success() -> None:
    transport = SimulatedPacketTransport(
        [
            RelayTimeoutError("first timeout"),
            response(CMD_GET_ALL, 0, {"state": 0, "pulsing": 0}),
        ]
    )
    client = RelayClient(transport, retries=1)

    assert client.get_all_relays() == {"state": 0, "pulsing": 0}
    assert len(transport.requests) == 2


def test_timeout_after_retry_budget_is_exhausted() -> None:
    transport = SimulatedPacketTransport(
        [RelayTimeoutError("first timeout"), RelayTimeoutError("second timeout")]
    )
    client = RelayClient(transport, retries=1)

    with pytest.raises(RelayTimeoutError, match="second timeout"):
        client.get_all_relays()


def test_serial_transport_reuses_open_serial_port(monkeypatch: pytest.MonkeyPatch) -> None:
    serials: list[FakeSerial] = []
    install_fake_serial(monkeypatch, serials)
    transport = SerialSmpTransport("COM19")
    first = encode_packet(
        SmpPacket(op=2, group=GROUP_RELAY, command=CMD_GET, seq=0, payload=encode_payload({}))
    )
    second = encode_packet(
        SmpPacket(op=2, group=GROUP_RELAY, command=CMD_GET, seq=1, payload=encode_payload({}))
    )

    assert decode_payload(decode_packet(transport.exchange(first, 1.0)).payload) == {"state": 0}
    assert decode_payload(decode_packet(transport.exchange(second, 1.5)).payload) == {"state": 1}

    assert len(serials) == 1
    assert serials[0].kwargs["port"] == "COM19"
    assert serials[0].reset_count == 2
    assert serials[0].timeout == 1.5
    assert serials[0].write_timeout == 1.5


def test_serial_transport_close_allows_reopen(monkeypatch: pytest.MonkeyPatch) -> None:
    serials: list[FakeSerial] = []
    install_fake_serial(monkeypatch, serials)
    transport = SerialSmpTransport("COM19")
    packet = encode_packet(
        SmpPacket(op=2, group=GROUP_RELAY, command=CMD_GET, seq=0, payload=encode_payload({}))
    )

    transport.exchange(packet, 1.0)
    transport.close()
    transport.exchange(packet, 1.0)

    assert len(serials) == 2
    assert serials[0].closed is True
    assert serials[1].closed is False


def test_serial_transport_closes_on_serial_error(monkeypatch: pytest.MonkeyPatch) -> None:
    serials: list[FakeSerial] = []
    install_fake_serial(monkeypatch, serials, read_error=OSError("lost port"))
    transport = SerialSmpTransport("COM19")
    packet = encode_packet(
        SmpPacket(op=2, group=GROUP_RELAY, command=CMD_GET, seq=0, payload=encode_payload({}))
    )

    with pytest.raises(RelayTransportError, match="lost port"):
        transport.exchange(packet, 1.0)

    assert serials[0].closed is True


def test_relay_client_context_manager_closes_transport() -> None:
    class CloseTrackingTransport(SimulatedPacketTransport):
        def __init__(self) -> None:
            super().__init__()
            self.closed = False

        def close(self) -> None:
            self.closed = True

    transport = CloseTrackingTransport()

    with RelayClient(transport) as client:
        assert client.transport is transport

    assert transport.closed is True
