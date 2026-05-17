# Host Library

The Python host library exposes the RP2350 relay management protocol over
Zephyr SMP on the USB CDC ACM serial device.

Install the package in the Zephyr workspace virtual environment:

```sh
python -m pip install -e .
```

The package uses `smp` for Zephyr SMP headers and serial framing, and `cbor2`
for CBOR payloads through `smp`'s dependency set.

## Connection

Use an explicit serial port from the operator PC:

```python
from rp2350_relay_6ch import RelayClient

client = RelayClient.connect("COM31", timeout_s=2.0, retries=1)
```

On Linux, the port is typically similar to `/dev/ttyACM0`. On Windows, use the
assigned `COM` port.

## API

The client methods return decoded response dictionaries from the firmware:

```python
info = client.get_info()
status = client.get_status()
relays = client.get_relays()
channel_0 = client.get_relays(channel=0)

client.set_relay(0, True)
client.set_relay(0, False)
client.set_all_relays(0x21)
client.pulse_relay(0, 100)
client.off_all()
```

`reboot()` sends the firmware reboot command:

```python
client.reboot()
```

## Exceptions

Import typed exceptions from the package root:

```python
from rp2350_relay_6ch import (
    RelayDeviceError,
    RelayProtocolError,
    RelayTimeoutError,
    RelayTransportError,
    RelayValidationError,
)
```

- `RelayValidationError`: invalid host-side arguments such as channel `6` or a
  pulse duration outside `10..60000` ms.
- `RelayTimeoutError`: no matching response arrived within the configured
  timeout and retry budget.
- `RelayTransportError`: serial setup, read, or write failed.
- `RelayProtocolError`: the response packet or CBOR payload was malformed or
  did not match the request.
- `RelayDeviceError`: firmware returned a structured relay management error.
  The exception exposes `group` and `rc` attributes.

## Testing Without Hardware

Use `SimulatedPacketTransport` for unit tests that need deterministic packets:

```python
from rp2350_relay_6ch import RelayClient, SimulatedPacketTransport

transport = SimulatedPacketTransport()
client = RelayClient(transport)
```

The host test suite uses the simulated transport before requiring hardware.

## Safety

Hardware scripts and manual sessions must finish with `off_all()`. Do not leave
relays energized after tests, and keep hazardous relay loads disconnected during
bring-up.
