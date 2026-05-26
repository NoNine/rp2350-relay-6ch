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

Use the client as a context manager when sending multiple commands. This keeps
the serial port open for the sequence and closes it when the block exits:

```python
with RelayClient.connect("COM31", timeout_s=2.0, retries=1) as client:
    client.set_relay(0, False)
    relays = client.get_relays()
```

The `rp2350-relay session` CLI uses the same direct connection model for
long-lived manual operation. It does not add a separate Python client API.

## Daemon Client

On Linux, `RelayDaemonClient` connects to a running `rp2350-relayd` Unix socket
instead of opening the serial port directly:

```python
from rp2350_relay_6ch import RelayDaemonClient

with RelayDaemonClient.connect(
    "/run/user/1000/rp2350-relay/bench-a.sock",
    timeout_s=2.0,
) as relay:
    relay.set_relay(0, True)
    relay.off_all()
```

The daemon client exposes the same relay methods as `RelayClient`, with
zero-based channel numbers in Python. It also exposes `get_daemon_status()` for
daemon process and reconnect state:

```python
status = relay.get_daemon_status()
```

Daemon-client timeouts cover connecting to the Unix socket and waiting for the
daemon response. Firmware request timeout and retry behavior are configured on
the daemon process.

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

## Planned Device Events

The current host package is request/response only. A future event-capable
protocol version is planned in
[Relay Management Protocol](protocol/relay-management.md#planned-event) using
best-effort device-originated SMP event frames.

When implemented, the host library should expose events first so session mode,
daemon mode, and direct library consumers share one transport foundation. The
serial transport will need one persistent reader that demultiplexes normal
responses from event frames. Existing synchronous `RelayClient` command methods
should remain compatible, while event-capable consumers use a callback or
nonblocking drain API for advisory events.

Event delivery will remain best effort. Library users must confirm critical
state with normal commands such as `get_status()` after reconnect.

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
