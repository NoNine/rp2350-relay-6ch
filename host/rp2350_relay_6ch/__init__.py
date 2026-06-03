"""Host tooling package for the RP2350 relay controller."""

from .client import RelayClient
from .daemon_client import RelayDaemonClient
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
    "RelayDaemonClient",
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

__version__ = "0.8.9"
