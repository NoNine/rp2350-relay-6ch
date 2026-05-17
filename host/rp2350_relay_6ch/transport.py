"""Host transports for relay SMP packets."""

from __future__ import annotations

from collections.abc import Iterable
from typing import Protocol

from .exceptions import RelayTimeoutError, RelayTransportError
from .smp import decoded_serial_frames, serial_frames


class PacketTransport(Protocol):
    """Packet-level transport used by RelayClient."""

    def exchange(self, packet: bytes, timeout_s: float) -> bytes:
        """Send one encoded SMP packet and return one encoded SMP response."""


class SerialSmpTransport:
    """SMP-over-serial transport for Zephyr's SMP UART framing."""

    def __init__(self, port: str, baudrate: int = 115200) -> None:
        self.port = port
        self.baudrate = baudrate

    def exchange(self, packet: bytes, timeout_s: float) -> bytes:
        try:
            import serial
        except ImportError as exc:  # pragma: no cover - environment dependent
            raise RelayTransportError("pyserial is required for serial transport") from exc

        try:
            with serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=timeout_s,
                write_timeout=timeout_s,
            ) as ser:
                ser.reset_input_buffer()
                for frame in serial_frames(packet):
                    ser.write(frame)
                return decoded_serial_frames(
                    iter(ser.readline, b""),
                    "timed out waiting for SMP response",
                )
        except RelayTimeoutError:
            raise
        except Exception as exc:  # pragma: no cover - pyserial-specific failures
            raise RelayTransportError(str(exc)) from exc


class SimulatedPacketTransport:
    """Deterministic packet transport for host unit tests."""

    def __init__(self, responses: Iterable[bytes | Exception] = ()) -> None:
        self.responses = list(responses)
        self.requests: list[bytes] = []

    def queue_response(self, response: bytes | Exception) -> None:
        self.responses.append(response)

    def exchange(self, packet: bytes, timeout_s: float) -> bytes:
        del timeout_s
        self.requests.append(packet)
        if not self.responses:
            raise RelayTimeoutError("simulated timeout")
        response = self.responses.pop(0)
        if isinstance(response, Exception):
            raise response
        return response
