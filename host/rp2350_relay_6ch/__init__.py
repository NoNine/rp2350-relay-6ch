"""Host tooling package for the RP2350 relay controller."""

from .client import RelayClient
from .exceptions import (
    RelayDeviceError,
    RelayError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)
from .transport import SerialSmpTransport, SimulatedPacketTransport

__all__ = [
    "RelayClient",
    "RelayDeviceError",
    "RelayError",
    "RelayProtocolError",
    "RelayTimeoutError",
    "RelayTransportError",
    "RelayValidationError",
    "SerialSmpTransport",
    "SimulatedPacketTransport",
    "__version__",
]

__version__ = "0.0.0"
