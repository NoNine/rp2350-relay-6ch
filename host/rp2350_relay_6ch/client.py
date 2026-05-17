"""Python RPC client for the RP2350 relay management group."""

from __future__ import annotations

from typing import Any

from .constants import (
    CMD_GET,
    CMD_INFO,
    CMD_OFF_ALL,
    CMD_PULSE,
    CMD_REBOOT,
    CMD_SET,
    CMD_SET_ALL,
    CMD_STATUS,
    DEVICE_ERROR_NAMES,
    GROUP_RELAY,
    OP_READ,
    OP_READ_RSP,
    OP_WRITE,
    OP_WRITE_RSP,
    PULSE_MAX_MS,
    PULSE_MIN_MS,
    RELAY_COUNT,
    RELAY_MASK,
)
from .exceptions import (
    RelayDeviceError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayValidationError,
)
from .smp import (
    SMP_VERSION_2,
    SmpPacket,
    decode_packet,
    decode_payload,
    encode_packet,
    encode_payload,
)
from .transport import PacketTransport, SerialSmpTransport


class RelayClient:
    """High-level host API for relay management commands."""

    def __init__(
        self,
        transport: PacketTransport,
        *,
        timeout_s: float = 2.0,
        retries: int = 0,
    ) -> None:
        if timeout_s <= 0:
            raise RelayValidationError("timeout_s must be positive")
        if retries < 0:
            raise RelayValidationError("retries must be non-negative")
        self.transport = transport
        self.timeout_s = timeout_s
        self.retries = retries
        self._seq = 0

    @classmethod
    def connect(
        cls,
        port: str,
        *,
        baudrate: int = 115200,
        timeout_s: float = 2.0,
        retries: int = 0,
    ) -> "RelayClient":
        return cls(
            SerialSmpTransport(port, baudrate),
            timeout_s=timeout_s,
            retries=retries,
        )

    def get_info(self) -> dict[str, Any]:
        return self._request(CMD_INFO, OP_READ, {})

    def get_relays(self, channel: int | None = None) -> dict[str, Any]:
        payload = {}
        if channel is not None:
            self._validate_channel(channel)
            payload["channel"] = channel
        return self._request(CMD_GET, OP_READ, payload)

    def set_relay(self, channel: int, on: bool) -> dict[str, Any]:
        self._validate_channel(channel)
        if not isinstance(on, bool):
            raise RelayValidationError("on must be a bool")
        return self._request(CMD_SET, OP_WRITE, {"channel": channel, "on": on})

    def set_all_relays(self, state: int) -> dict[str, Any]:
        self._validate_state_mask(state)
        return self._request(CMD_SET_ALL, OP_WRITE, {"state": state})

    def pulse_relay(self, channel: int, duration_ms: int) -> dict[str, Any]:
        self._validate_channel(channel)
        if (
            not isinstance(duration_ms, int)
            or duration_ms < PULSE_MIN_MS
            or duration_ms > PULSE_MAX_MS
        ):
            raise RelayValidationError(
                f"duration_ms must be {PULSE_MIN_MS}..{PULSE_MAX_MS}"
            )
        return self._request(
            CMD_PULSE,
            OP_WRITE,
            {"channel": channel, "duration_ms": duration_ms},
        )

    def off_all(self) -> dict[str, Any]:
        return self._request(CMD_OFF_ALL, OP_WRITE, {})

    def get_status(self) -> dict[str, Any]:
        return self._request(CMD_STATUS, OP_READ, {})

    def reboot(self) -> dict[str, Any]:
        return self._request(CMD_REBOOT, OP_WRITE, {})

    def _request(self, command: int, op: int, payload: dict[str, Any]) -> dict[str, Any]:
        seq = self._next_seq()
        request = SmpPacket(
            op=op,
            group=GROUP_RELAY,
            command=command,
            seq=seq,
            payload=encode_payload(payload),
        )
        encoded = encode_packet(request)
        expected_op = OP_READ_RSP if op == OP_READ else OP_WRITE_RSP
        last_timeout: RelayTimeoutError | None = None

        for _ in range(self.retries + 1):
            try:
                response = decode_packet(self.transport.exchange(encoded, self.timeout_s))
                return self._decode_response(response, expected_op, command, seq)
            except RelayTimeoutError as exc:
                last_timeout = exc

        assert last_timeout is not None
        raise last_timeout

    def _decode_response(
        self,
        response: SmpPacket,
        expected_op: int,
        command: int,
        seq: int,
    ) -> dict[str, Any]:
        if response.version != SMP_VERSION_2:
            raise RelayProtocolError(f"unexpected SMP version {response.version}")
        if response.op != expected_op:
            raise RelayProtocolError(
                f"unexpected SMP response op {response.op}, expected {expected_op}"
            )
        if response.group != GROUP_RELAY:
            raise RelayProtocolError("SMP response group does not match the request")
        if response.command != command:
            raise RelayProtocolError("SMP response command does not match the request")
        if response.seq != seq:
            raise RelayProtocolError("SMP response sequence does not match the request")

        decoded = decode_payload(response.payload)
        err = decoded.get("err")
        if isinstance(err, dict):
            group = int(err.get("group", -1))
            rc = int(err.get("rc", -1))
            name = DEVICE_ERROR_NAMES.get(rc, "unknown")
            raise RelayDeviceError(group, rc, f"device error {name} ({rc})")
        return decoded

    def _next_seq(self) -> int:
        seq = self._seq
        self._seq = (self._seq + 1) & 0xFF
        return seq

    @staticmethod
    def _validate_channel(channel: int) -> None:
        if not isinstance(channel, int) or channel < 0 or channel >= RELAY_COUNT:
            raise RelayValidationError(f"channel must be 0..{RELAY_COUNT - 1}")

    @staticmethod
    def _validate_state_mask(state: int) -> None:
        if not isinstance(state, int) or state < 0 or state & ~RELAY_MASK:
            raise RelayValidationError(f"state must be a six-bit mask 0..0x{RELAY_MASK:x}")
