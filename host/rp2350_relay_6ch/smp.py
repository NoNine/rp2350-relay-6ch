"""SMP packet helpers for the relay management protocol."""

from __future__ import annotations

from dataclasses import dataclass

import cbor2
from smp import header as smp_header
from smp import packet as smp_packet
from smp.exceptions import SMPBadContinueDelimiter, SMPBadCRC, SMPBadStartDelimiter

from .exceptions import RelayProtocolError, RelayTimeoutError

SMP_VERSION_2 = 1


@dataclass(frozen=True)
class SmpPacket:
    op: int
    group: int
    command: int
    seq: int
    payload: bytes
    flags: int = 0
    version: int = SMP_VERSION_2


def encode_packet(packet: SmpPacket) -> bytes:
    header = smp_header.Header(
        op=smp_header.OP(packet.op),
        version=smp_header.Version(packet.version),
        flags=smp_header.Flag(packet.flags),
        length=len(packet.payload),
        group_id=packet.group,
        sequence=packet.seq,
        command_id=packet.command,
    )
    return bytes(header) + packet.payload


def decode_packet(data: bytes) -> SmpPacket:
    if len(data) < smp_header.Header.SIZE:
        raise RelayProtocolError("SMP packet is shorter than the header")
    try:
        header = smp_header.Header.loads(data[: smp_header.Header.SIZE])
    except (AssertionError, ValueError) as exc:
        raise RelayProtocolError(str(exc)) from exc
    payload = data[smp_header.Header.SIZE :]
    if header.length != len(payload):
        raise RelayProtocolError(
            f"SMP length mismatch: header={header.length} actual={len(payload)}"
        )
    return SmpPacket(
        op=int(header.op),
        group=int(header.group_id),
        command=int(header.command_id),
        seq=header.sequence,
        payload=payload,
        flags=int(header.flags),
        version=int(header.version),
    )


def encode_payload(values: dict[str, object]) -> bytes:
    return cbor2.dumps(values)


def decode_payload(data: bytes) -> dict[str, object]:
    try:
        value = cbor2.loads(data)
    except Exception as exc:
        raise RelayProtocolError(str(exc)) from exc
    if not isinstance(value, dict):
        raise RelayProtocolError("CBOR payload is not a map")
    return value


def serial_frames(packet: bytes) -> list[bytes]:
    return list(smp_packet.encode(packet))


def decoded_serial_frames(lines: object, timeout_note: str) -> bytes:
    decoder = smp_packet.decode()
    next(decoder)

    for line in lines:
        if not line:
            raise RelayTimeoutError(timeout_note)
        try:
            decoder.send(line)
        except StopIteration as exc:
            return bytes(exc.value)
        except (SMPBadContinueDelimiter, SMPBadCRC, SMPBadStartDelimiter) as exc:
            raise RelayProtocolError(str(exc)) from exc

    raise RelayTimeoutError(timeout_note)
