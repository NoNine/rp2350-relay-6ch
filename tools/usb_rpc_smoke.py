#!/usr/bin/env python3
"""Manual Phase 4 USB CDC SMP smoke-test client."""

from __future__ import annotations

import argparse
import base64
import json
import struct
import sys
from collections.abc import Iterator
from typing import Any

import serial


GROUP_RELAY = 64
OP_READ = 0
OP_READ_RSP = 1
OP_WRITE = 2
OP_WRITE_RSP = 3
SMP_VERSION_2 = 1
SERIAL_HDR_PKT = 0x0609
SERIAL_HDR_FRAG = 0x0414

CMD_INFO = 0
CMD_GET = 1
CMD_SET = 2
CMD_SET_ALL = 3
CMD_PULSE = 4
CMD_OFF_ALL = 5
CMD_STATUS = 6


def crc16_itu_t(data: bytes, seed: int = 0) -> int:
    crc = seed
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encode_uint(value: int) -> bytes:
    if value < 0:
        raise ValueError("negative integers are not supported")
    if value < 24:
        return bytes([value])
    if value <= 0xFF:
        return b"\x18" + bytes([value])
    if value <= 0xFFFF:
        return b"\x19" + struct.pack(">H", value)
    return b"\x1a" + struct.pack(">I", value)


def encode_text(value: str) -> bytes:
    data = value.encode("utf-8")
    return encode_type_len(3, len(data)) + data


def encode_type_len(major: int, length: int) -> bytes:
    if length < 24:
        return bytes([(major << 5) | length])
    if length <= 0xFF:
        return bytes([(major << 5) | 24, length])
    if length <= 0xFFFF:
        return bytes([(major << 5) | 25]) + struct.pack(">H", length)
    return bytes([(major << 5) | 26]) + struct.pack(">I", length)


def encode_value(value: Any) -> bytes:
    if isinstance(value, bool):
        return b"\xf5" if value else b"\xf4"
    if isinstance(value, int):
        return encode_uint(value)
    if isinstance(value, str):
        return encode_text(value)
    if isinstance(value, dict):
        return encode_map(value)
    raise TypeError(f"unsupported CBOR value: {value!r}")


def encode_map(values: dict[str, Any]) -> bytes:
    encoded = bytearray(encode_type_len(5, len(values)))
    for key, value in values.items():
        encoded += encode_text(key)
        encoded += encode_value(value)
    return bytes(encoded)


class CborReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def read(self, count: int) -> bytes:
        if self.offset + count > len(self.data):
            raise ValueError("truncated CBOR payload")
        chunk = self.data[self.offset : self.offset + count]
        self.offset += count
        return chunk

    def next_is_break(self) -> bool:
        return self.offset < len(self.data) and self.data[self.offset] == 0xFF

    def read_break(self) -> None:
        if not self.next_is_break():
            raise ValueError("missing CBOR break")
        self.offset += 1

    def read_len(self, additional: int) -> int:
        if additional < 24:
            return additional
        if additional == 24:
            return self.read(1)[0]
        if additional == 25:
            return struct.unpack(">H", self.read(2))[0]
        if additional == 26:
            return struct.unpack(">I", self.read(4))[0]
        if additional == 27:
            return struct.unpack(">Q", self.read(8))[0]
        raise ValueError(f"unsupported CBOR additional info: {additional}")

    def read_indefinite_text(self) -> str:
        chunks = bytearray()

        while not self.next_is_break():
            initial = self.read(1)[0]
            major = initial >> 5
            additional = initial & 0x1F
            if major != 3 or additional == 31:
                raise ValueError("invalid indefinite CBOR text chunk")
            chunks += self.read(self.read_len(additional))

        self.read_break()
        return chunks.decode("utf-8")

    def read_map_entry(self, result: dict[str, Any]) -> None:
        key = self.read_value()
        if not isinstance(key, str):
            raise ValueError("CBOR map key is not text")
        result[key] = self.read_value()

    def read_value(self) -> Any:
        initial = self.read(1)[0]
        major = initial >> 5
        additional = initial & 0x1F

        if major == 0:
            return self.read_len(additional)
        if major == 3:
            if additional == 31:
                return self.read_indefinite_text()
            length = self.read_len(additional)
            return self.read(length).decode("utf-8")
        if major == 5:
            result: dict[str, Any] = {}
            if additional == 31:
                while not self.next_is_break():
                    self.read_map_entry(result)
                self.read_break()
                return result

            length = self.read_len(additional)
            for _ in range(length):
                self.read_map_entry(result)
            return result
        if major == 7 and additional in (20, 21):
            return additional == 21
        raise ValueError(f"unsupported CBOR item: major={major} additional={additional}")


def decode_map(data: bytes) -> dict[str, Any]:
    reader = CborReader(data)
    value = reader.read_value()
    if reader.offset != len(data):
        raise ValueError("trailing CBOR data")
    if not isinstance(value, dict):
        raise ValueError("CBOR payload is not a map")
    return value


def encode_smp(op: int, group: int, command: int, seq: int, payload: bytes) -> bytes:
    first = op | (SMP_VERSION_2 << 3)
    return struct.pack(">BBHHBB", first, 0, len(payload), group, seq, command) + payload


def serial_frame(packet: bytes) -> bytes:
    crc = crc16_itu_t(packet)
    raw = struct.pack(">H", len(packet) + 2) + packet + struct.pack(">H", crc)
    return struct.pack(">H", SERIAL_HDR_PKT) + base64.b64encode(raw) + b"\n"


def decoded_frames(lines: Iterator[bytes], timeout_note: str) -> bytes:
    packet_len: int | None = None
    decoded = bytearray()

    for line in lines:
        if not line:
            raise TimeoutError(timeout_note)
        line = line.strip()
        if len(line) < 2:
            continue

        marker = struct.unpack(">H", line[:2])[0]
        if marker not in (SERIAL_HDR_PKT, SERIAL_HDR_FRAG):
            continue

        fragment = base64.b64decode(line[2:], validate=False)
        if marker == SERIAL_HDR_PKT:
            if len(fragment) < 2:
                raise ValueError("serial packet frame is missing length")
            packet_len = struct.unpack(">H", fragment[:2])[0]
            decoded = bytearray(fragment[2:])
        else:
            decoded += fragment

        if packet_len is not None and len(decoded) >= packet_len:
            if len(decoded) != packet_len:
                raise ValueError("received more bytes than the SMP packet length")
            if crc16_itu_t(bytes(decoded)) != 0:
                raise ValueError("bad SMP serial CRC")
            return bytes(decoded[:-2])

    raise TimeoutError(timeout_note)


def call(port: str, baud: int, command: int, op: int, payload: dict[str, Any]) -> dict[str, Any]:
    request = encode_smp(op, GROUP_RELAY, command, 1, encode_map(payload))
    expected_op = OP_READ_RSP if op == OP_READ else OP_WRITE_RSP

    with serial.Serial(port=port, baudrate=baud, timeout=2, write_timeout=2) as ser:
        ser.reset_input_buffer()
        ser.write(serial_frame(request))
        response = decoded_frames(iter(ser.readline, b""), "timed out waiting for SMP response")

    first, _flags, length, group, seq, rsp_command = struct.unpack(">BBHHBB", response[:8])
    rsp_op = first & 0x07
    rsp_version = (first >> 3) & 0x03
    body = response[8:]

    if rsp_version != SMP_VERSION_2:
        raise ValueError(f"unexpected SMP version {rsp_version}")
    if rsp_op != expected_op:
        raise ValueError(f"unexpected SMP response op {rsp_op}, expected {expected_op}")
    if length != len(body):
        raise ValueError(f"SMP length mismatch: header={length} actual={len(body)}")
    if group != GROUP_RELAY or seq != 1 or rsp_command != command:
        raise ValueError("SMP response header does not match the request")

    return decode_map(body)


def print_response(response: dict[str, Any]) -> None:
    print(json.dumps(response, indent=2, sort_keys=True))


def run_one(args: argparse.Namespace) -> dict[str, Any]:
    payload: dict[str, Any] = {}
    op = OP_READ
    command = args.command_id

    if args.action == "get" and args.channel is not None:
        payload["channel"] = args.channel
    elif args.action == "set":
        op = OP_WRITE
        payload = {"channel": args.channel, "on": args.on == "on"}
    elif args.action == "set-all":
        op = OP_WRITE
        payload = {"state": args.state}
    elif args.action == "pulse":
        op = OP_WRITE
        payload = {"channel": args.channel, "duration_ms": args.duration_ms}
    elif args.action in ("off-all", "invalid-channel", "invalid-pulse"):
        op = OP_WRITE
        if args.action == "invalid-channel":
            command = CMD_SET
            payload = {"channel": 6, "on": True}
        elif args.action == "invalid-pulse":
            command = CMD_PULSE
            payload = {"channel": 0, "duration_ms": 1}

    return call(args.port, args.baud, command, op, payload)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_smoke(args: argparse.Namespace) -> None:
    info = call(args.port, args.baud, CMD_INFO, OP_READ, {})
    print_response(info)
    require(info.get("protocol_version") == 1, "protocol_version is not 1")
    require(info.get("relay_count") == 6, "relay_count is not 6")
    require(info.get("hardware") == "Waveshare RP2350-Relay-6CH", "unexpected hardware name")

    status = call(args.port, args.baud, CMD_STATUS, OP_READ, {})
    print_response(status)
    require(status.get("transport") == "usb_cdc_acm_smp", "unexpected transport")
    require(status.get("usb_cdc_acm") is True, "usb_cdc_acm is not true")
    require(status.get("smp_uart") is True, "smp_uart is not true")
    require(status.get("state") == 0, "relays are not all off at start")

    print_response(call(args.port, args.baud, CMD_SET, OP_WRITE, {"channel": 0, "on": True}))
    print_response(call(args.port, args.baud, CMD_GET, OP_READ, {"channel": 0}))
    print_response(call(args.port, args.baud, CMD_SET, OP_WRITE, {"channel": 0, "on": False}))
    print_response(call(args.port, args.baud, CMD_SET_ALL, OP_WRITE, {"state": 0x21}))
    print_response(call(args.port, args.baud, CMD_OFF_ALL, OP_WRITE, {}))
    print_response(call(args.port, args.baud, CMD_PULSE, OP_WRITE, {"channel": 0, "duration_ms": 100}))
    print_response(call(args.port, args.baud, CMD_GET, OP_READ, {"channel": 0}))

    invalid_channel = call(args.port, args.baud, CMD_SET, OP_WRITE, {"channel": 6, "on": True})
    print_response(invalid_channel)
    require(invalid_channel.get("err", {}).get("group") == GROUP_RELAY, "missing group error")
    require(invalid_channel.get("err", {}).get("rc") == 2, "invalid channel was not rejected")

    invalid_pulse = call(args.port, args.baud, CMD_PULSE, OP_WRITE, {"channel": 0, "duration_ms": 1})
    print_response(invalid_pulse)
    require(invalid_pulse.get("err", {}).get("group") == GROUP_RELAY, "missing group error")
    require(invalid_pulse.get("err", {}).get("rc") == 2, "invalid pulse was not rejected")

    final_state = call(args.port, args.baud, CMD_OFF_ALL, OP_WRITE, {})
    print_response(final_state)
    require(final_state.get("state") == 0, "off_all did not leave relays off")
    print("USB RPC smoke test passed")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="CDC ACM serial port, for example COM7")
    parser.add_argument("--baud", type=int, default=115200)
    subparsers = parser.add_subparsers(dest="action", required=True)

    commands = [
        ("info", CMD_INFO, "read device and protocol information"),
        ("status", CMD_STATUS, "read transport and relay status"),
        ("off-all", CMD_OFF_ALL, "turn all relays off"),
        ("invalid-channel", CMD_SET, "send an invalid channel request"),
        ("invalid-pulse", CMD_PULSE, "send an invalid pulse-duration request"),
    ]
    for name, command_id, help_text in commands:
        child = subparsers.add_parser(name, help=help_text)
        child.set_defaults(command_id=command_id)

    get = subparsers.add_parser("get", help="read all relays or one zero-based channel")
    get.add_argument("--channel", type=int, choices=range(6))
    get.set_defaults(command_id=CMD_GET)

    set_one = subparsers.add_parser("set", help="set one zero-based relay channel")
    set_one.add_argument("channel", type=int, choices=range(6))
    set_one.add_argument("on", choices=("on", "off"))
    set_one.set_defaults(command_id=CMD_SET)

    set_all = subparsers.add_parser("set-all", help="set all relays from a six-bit mask")
    set_all.add_argument("state", type=lambda value: int(value, 0))
    set_all.set_defaults(command_id=CMD_SET_ALL)

    pulse = subparsers.add_parser("pulse", help="pulse one zero-based relay channel")
    pulse.add_argument("channel", type=int, choices=range(6))
    pulse.add_argument("duration_ms", type=int)
    pulse.set_defaults(command_id=CMD_PULSE)

    smoke = subparsers.add_parser("smoke", help="run the Phase 4 USB RPC smoke sequence")
    smoke.set_defaults(command_id=None)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        if args.action == "smoke":
            run_smoke(args)
        else:
            print_response(run_one(args))
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
