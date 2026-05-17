"""Typed exceptions for RP2350 relay host operations."""

from __future__ import annotations


class RelayError(Exception):
    """Base class for host relay library errors."""


class RelayTransportError(RelayError):
    """Transport setup, read, or write failed."""


class RelayTimeoutError(RelayTransportError):
    """A request timed out before a matching response arrived."""


class RelayProtocolError(RelayError):
    """The device returned malformed or mismatched protocol data."""


class RelayValidationError(RelayError):
    """The caller supplied an invalid host-side argument."""


class RelayDeviceError(RelayError):
    """The device rejected a well-formed relay command."""

    def __init__(self, group: int, rc: int, message: str | None = None) -> None:
        self.group = group
        self.rc = rc
        super().__init__(message or f"device error group={group} rc={rc}")
