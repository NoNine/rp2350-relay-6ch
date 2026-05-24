"""USB serial discovery for relay session mode."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Protocol

from .exceptions import RelayTransportError, RelayValidationError

RELAY_USB_VID = 0x2E8A
RELAY_USB_PID = 0x0009
RELAY_USB_PRODUCT = "RP2350-Relay-6CH"


@dataclass(frozen=True)
class RelayUsbDevice:
    port: str
    serial_number: str | None = None
    product: str | None = None
    verified_product: bool = False


class PortInfo(Protocol):
    device: str
    vid: int | None
    pid: int | None
    product: str | None
    serial_number: str | None


def list_relay_devices(ports: Iterable[PortInfo] | None = None) -> list[RelayUsbDevice]:
    """Return USB CDC ports that are relay controller candidates."""

    if ports is None:
        try:
            from serial.tools import list_ports
        except ImportError as exc:  # pragma: no cover - environment dependent
            raise RelayTransportError("pyserial is required for USB discovery") from exc

        ports = list_ports.comports()

    devices: list[RelayUsbDevice] = []
    for port in ports:
        product = getattr(port, "product", None)
        serial_number = getattr(port, "serial_number", None)
        if getattr(port, "vid", None) != RELAY_USB_VID:
            continue
        if getattr(port, "pid", None) != RELAY_USB_PID:
            continue
        if product is not None and RELAY_USB_PRODUCT in product:
            devices.append(
                RelayUsbDevice(
                    port=str(getattr(port, "device")),
                    serial_number=serial_number,
                    product=product,
                    verified_product=True,
                )
            )
        elif product is None and serial_number:
            devices.append(
                RelayUsbDevice(
                    port=str(getattr(port, "device")),
                    serial_number=serial_number,
                    product=None,
                    verified_product=False,
                )
            )
    return devices


def select_device_by_serial(
    usb_serial: str,
    ports: Iterable[PortInfo] | None = None,
) -> RelayUsbDevice:
    matches = [
        device
        for device in list_relay_devices(ports)
        if device.serial_number == usb_serial
    ]
    if not matches:
        raise RelayValidationError(f"no relay device found with USB serial {usb_serial}")
    if len(matches) > 1:
        raise RelayValidationError(
            f"multiple relay devices found with USB serial {usb_serial}"
        )
    return matches[0]
