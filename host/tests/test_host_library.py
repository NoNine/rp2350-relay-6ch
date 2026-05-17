from __future__ import annotations

import pytest

from rp2350_relay_6ch import RelayClient, SimulatedPacketTransport
from rp2350_relay_6ch.constants import (
    CMD_GET,
    CMD_INFO,
    CMD_PULSE,
    CMD_SET,
    CMD_SET_ALL,
    ERR_BUSY,
    GROUP_RELAY,
    OP_READ_RSP,
    OP_WRITE_RSP,
)
from rp2350_relay_6ch.exceptions import (
    RelayDeviceError,
    RelayProtocolError,
    RelayTimeoutError,
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


def test_get_info_returns_decoded_success_response() -> None:
    transport = SimulatedPacketTransport(
        [response(CMD_INFO, 0, {"protocol_version": 1, "relay_count": 6})]
    )
    client = RelayClient(transport)

    assert client.get_info() == {"protocol_version": 1, "relay_count": 6}


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


def test_invalid_host_arguments_raise_validation_errors() -> None:
    client = RelayClient(SimulatedPacketTransport())

    with pytest.raises(RelayValidationError):
        client.get_relays(6)
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


def test_mismatched_response_header_raises_protocol_error() -> None:
    transport = SimulatedPacketTransport([response(CMD_GET, 99, {"state": 0})])
    client = RelayClient(transport)

    with pytest.raises(RelayProtocolError, match="sequence"):
        client.get_relays()


def test_timeout_retries_before_success() -> None:
    transport = SimulatedPacketTransport(
        [
            RelayTimeoutError("first timeout"),
            response(CMD_GET, 0, {"state": 0, "pulsing": 0}),
        ]
    )
    client = RelayClient(transport, retries=1)

    assert client.get_relays() == {"state": 0, "pulsing": 0}
    assert len(transport.requests) == 2


def test_timeout_after_retry_budget_is_exhausted() -> None:
    transport = SimulatedPacketTransport(
        [RelayTimeoutError("first timeout"), RelayTimeoutError("second timeout")]
    )
    client = RelayClient(transport, retries=1)

    with pytest.raises(RelayTimeoutError, match="second timeout"):
        client.get_relays()
