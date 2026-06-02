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
    relays = client.get_all_relays()
```

The `rp2350-relay session` CLI uses the same direct connection model for
long-lived manual operation. It does not add a separate Python client API.

## Device Discovery

Python scripts can discover relay USB candidates with the discovery helpers:

```python
from rp2350_relay_6ch.discovery import list_relay_devices

for device in list_relay_devices():
    print(
        device.port,
        device.serial_number,
        device.product,
        device.verified_product,
    )
```

`list_relay_devices()` returns `RelayUsbDevice` records with:

- `port`: OS serial device path, such as `COM31` or `/dev/ttyACM0`.
- `serial_number`: USB serial number when the host OS reports it.
- `product`: USB product string when available.
- `verified_product`: `True` when the product string identifies the relay
  controller.

Discovery inspects USB metadata only. It does not open ports or send relay
protocol commands, so treat returned devices as candidates until the script
opens the port and verifies `identity()` and `status()`.

To target a known controller by USB serial number:

```python
from rp2350_relay_6ch import RelayClient
from rp2350_relay_6ch.discovery import select_device_by_serial

device = select_device_by_serial("E6614C311F4B8B2F")

with RelayClient.connect(device.port, timeout_s=2.0, retries=1) as relay:
    identity = relay.identity()
    status = relay.status()
    relay.pulse_relay(0, 100)
    relay.off_all_relays()
```

`select_device_by_serial()` raises `RelayValidationError` when no relay
candidate matches the serial number or when multiple candidates report the same
serial number.

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
    relay.off_all_relays()
```

The daemon client exposes the same relay methods as `RelayClient`, with
zero-based channel numbers in Python. It also exposes `daemon_status()` for
daemon process and reconnect state:

```python
status = relay.daemon_status()
```

Daemon-client timeouts cover connecting to the Unix socket and waiting for the
daemon response. Firmware request timeout and retry behavior are configured on
the daemon process.

## Heartbeat Polling

`RelayClient.heartbeat()` sends the relay-management heartbeat request. Direct
Python automation that owns the serial connection should call it every 2.5 s
on a background interval and serialize heartbeat calls with foreground commands
that share the same serial client. The 2.5 s cadence is fixed host behavior,
not derived from `comm_loss_timeout_ms`.

```python
import threading

from rp2350_relay_6ch import RelayClient, RelayError
from rp2350_relay_6ch.discovery import select_device_by_serial

stop = threading.Event()
lock = threading.Lock()


def heartbeat_loop(relay: RelayClient) -> None:
    while not stop.wait(2.5):
        try:
            with lock:
                relay.heartbeat()
        except RelayError as exc:
            print(f"heartbeat: {exc}")


device = select_device_by_serial("E6614C311F4B8B2F")

with RelayClient.connect(device.port, timeout_s=2.0, retries=1) as relay:
    thread = threading.Thread(target=heartbeat_loop, args=(relay,), daemon=True)
    thread.start()
    try:
        with lock:
            relay.identity()
            relay.pulse_relay(0, 100)
    finally:
        stop.set()
        thread.join(timeout=6.0)
        with lock:
            relay.off_all_relays()
```

When using `RelayDaemonClient`, do not implement a direct serial heartbeat
loop. The daemon owns the serial port, polls heartbeat internally, and exposes
connection state through `daemon_status()`.

## Multiple Devices

A direct Python script may control multiple relay controllers by opening one
`RelayClient` per serial port. Do not open the same serial port from multiple
clients or processes at the same time.

```python
from rp2350_relay_6ch import RelayClient
from rp2350_relay_6ch.discovery import list_relay_devices

clients = []
try:
    for device in list_relay_devices():
        client = RelayClient.connect(device.port, timeout_s=2.0, retries=1)
        client.identity()
        client.status()
        clients.append((device, client))

    clients[0][1].set_relay(0, True)
    clients[1][1].pulse_relay(2, 100)
finally:
    for _device, client in clients:
        try:
            client.off_all_relays()
        finally:
            client.close()
```

On Linux, production multi-device operation should use one daemon instance per
physical relay controller, each with a distinct Unix socket. Python clients then
connect to the target daemon socket:

```python
from rp2350_relay_6ch import RelayDaemonClient
from rp2350_relay_6ch.config import resolve_socket_for_instance

socket_path = resolve_socket_for_instance(instance="bench-a")

with RelayDaemonClient.connect(socket_path, timeout_s=2.0) as relay:
    relay.daemon_status()
    relay.pulse_relay(0, 100)
```

## API

The client methods return decoded response dictionaries from the firmware:

```python
identity = client.identity()
capabilities = client.capabilities()
build = client.build_info()
status = client.status()
health = client.health()
transport = client.transport_status()
safety = client.safety()
watchdog = client.watchdog()
relays = client.get_all_relays()
channel_0 = client.get_relay(0)

client.set_relay(0, True)
client.set_relay(0, False)
client.set_all_relays(0x21)
client.pulse_relay(0, 100)
client.off_all_relays()
```

`get_info()` is not part of the protocol `8`, command model `2` host API.
Call role-specific read methods directly; each method sends exactly one
firmware command.

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
state with normal commands such as `status()` or `get_all_relays()` after
reconnect.

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

Hardware scripts and manual sessions must finish with `off_all_relays()`. Do
not leave relays energized after tests, and keep hazardous relay loads
disconnected during bring-up.
