# Host Daemon Mode

This document is the Phase 8b implementation contract for Linux daemon mode.
It is implementer-facing first; operator-facing setup and usage belong in
`docs/cli.md`, `docs/host-library.md`, and a daemon smoke-test procedure once
the implementation lands.

## Scope Boundaries

- Phase 8b daemon mode is Linux-only.
- Windows production operation is covered by Phase 8a session mode. Windows
  diagnostics and simple checks remain supported through direct serial tooling,
  for example `rp2350-relay --port COM7 info`.
- The daemon is independent from firmware communication-loss safety.
- Do not add firmware heartbeat commands, communication-loss timeout commands,
  reboot-on-silence behavior, daemon authentication, audit logs, network APIs,
  or new relay protocol fields.
- Treat direct serial diagnostics as an escape hatch. Operators should stop the
  daemon before using `rp2350-relay --port ...` against the same device.

## Entry Points

- Keep `rp2350-relay` as the direct serial diagnostic CLI.
- Add `rp2350-relayd` for the daemon process.
- Add `rp2350-relayctl` for daemon-client commands.
- Add `RelayDaemonClient` beside the direct `RelayClient`.

## Daemon CLI

`rp2350-relayd` runs in the foreground by default so tests, service managers,
and developers can supervise the same process shape.

Command shape:

```sh
rp2350-relayd --port /dev/ttyACM0 \
  [--baud 115200] \
  [--socket <path>] \
  [--timeout 2.0] \
  [--retries 1] \
  [--reconnect-interval 1.0] \
  [--wait-device]
```

- `--port` is required in Phase 8b. Explicit-port retry is the only rediscovery
  policy.
- `--socket` defaults to `$XDG_RUNTIME_DIR/rp2350-relay.sock`, falling back to
  `/run/user/$UID/rp2350-relay.sock`.
- `--timeout` and `--retries` configure firmware request behavior through the
  underlying direct `RelayClient`.
- `--reconnect-interval` is the delay between serial reopen attempts after a
  transport failure.
- Without `--wait-device`, invalid arguments or initial serial open failure
  exits nonzero.
- With `--wait-device`, a missing or initially busy serial device starts the
  daemon in disconnected state and retries until the device opens.
- Log lifecycle events, socket path, serial open/close, client command
  failures, reconnect attempts, and shutdown results to stdout/stderr. Use
  Python logging so systemd can collect the same output through journald.
- Handle `SIGINT` and `SIGTERM` by stopping client accept, making a best-effort
  `off-all` attempt if the serial connection is open, removing the socket path,
  closing serial resources, and exiting.

## Daemon Behavior

- Open exactly one configured USB CDC serial port and keep direct firmware
  access inside the daemon.
- Serialize all relay operations through one worker or command queue.
- Query firmware state on startup and cache the result without changing relay
  outputs.
- Keep relay state as commanded when no local client is connected.
- On clean shutdown, make a best-effort `off-all` attempt, then exit even if
  the command fails.
- Reconnect after serial transport failure, firmware reboot, USB reset, or
  device renumbering only through explicit-port retry in Phase 8b.
- Log daemon lifecycle, client command failures, reconnect attempts, and clean
  shutdown results.

## Reconnect Policy

- Treat `RelayTransportError`, serial open failures, and disconnected serial
  devices as daemon device-disconnected state.
- Reject relay-control requests while disconnected with a daemon transport
  error response. Do not queue relay-control requests across reconnect.
- Continue serving daemon status while disconnected so clients can report the
  current daemon state.
- Retry opening the same explicit `--port` every `--reconnect-interval`
  seconds.
- After reconnect, query firmware state and refresh the daemon cache before
  accepting relay-control requests again.
- Wildcard `/dev/ttyACM*` discovery, USB VID/PID matching, and serial-number
  matching are deferred beyond Phase 8b.

## NDJSON Protocol

Use newline-delimited JSON over a Unix domain socket. Place the socket at
`$XDG_RUNTIME_DIR/rp2350-relay.sock`, falling back to
`/run/user/$UID/rp2350-relay.sock`. Limit first-phase socket access to the same
operator user through runtime directory ownership and socket permissions.

Mirror existing relay commands: `info`, `build-info`, `get`, `set`, `set-all`,
`pulse`, `off-all`, `status`, and `reboot`.

Request frames are one JSON object per line:

```json
{"id":"1","command":"set","args":{"channel":0,"on":true}}
```

- `id` is required, may be a string or integer, and is echoed unchanged.
- `command` is required and must be one of the mirrored relay commands.
- `args` is optional for commands with no arguments and required for commands
  that need arguments.

Success responses include `id`, `ok: true`, and `result`:

```json
{"id":"1","ok":true,"result":{"state":1}}
```

Error responses include `id`, `ok: false`, and `error`:

```json
{
  "id":"1",
  "ok":false,
  "error":{"kind":"validation","message":"channel must be 0..5"}
}
```

- Error kinds are `validation`, `transport`, `timeout`, `protocol`, `device`,
  and `daemon`.
- Malformed JSON, missing `id`, missing `command`, unknown commands, invalid
  arguments, firmware errors, and disconnected-device state return structured
  errors without exiting the daemon.
- For malformed JSON where no `id` can be decoded, return `id: null`.

## Python API

Add `RelayDaemonClient` beside the direct `RelayClient`.

```python
from rp2350_relay_6ch import RelayDaemonClient

with RelayDaemonClient.connect(timeout_s=2.0) as relay:
    relay.set_relay(0, True)
    relay.off_all()
```

- `RelayDaemonClient.connect(socket_path=None, timeout_s=2.0)` connects to the
  default socket when `socket_path` is omitted.
- Expose methods matching direct `RelayClient`: `get_info()`,
  `get_build_info()`, `get_relays(channel=None)`, `set_relay(channel, on)`,
  `set_all_relays(state)`, `pulse_relay(channel, duration_ms)`, `off_all()`,
  `get_status()`, and `reboot()`.
- Keep channel numbers zero-based in the Python API.
- Use the same host-side validation rules and typed exceptions as the direct
  client where practical.
- Map daemon error kinds to existing typed exceptions: `validation` to
  `RelayValidationError`, `transport` to `RelayTransportError`, `timeout` to
  `RelayTimeoutError`, `protocol` to `RelayProtocolError`, and `device` to
  `RelayDeviceError`. Map `daemon` to `RelayTransportError` unless a dedicated
  daemon exception is later justified.

## Client CLI

Command shape:

```sh
rp2350-relayctl [--socket <path>] [--timeout 2.0] \
  [--output human|json] <command>
```

- Commands mirror the direct CLI: `info`, `build-info`, `get`, `set`,
  `set-all`, `pulse`, `off-all`, `status`, and `reboot`.
- Keep CLI channel arguments one-based and board-label aligned: `1` is `CH1`
  and `6` is `CH6`.
- `rp2350-relayctl` does not accept `--port`, `--baud`, or direct serial
  options in Phase 8b.
- Human and JSON output should match direct CLI output shapes where practical.
- Reuse direct CLI exit codes where practical: `2` for argument or validation
  errors, `3` for daemon unavailable or transport errors, `4` for timeouts,
  `5` for protocol errors, and `6` for device-side relay management errors.

## Systemd User Unit

Install the example unit at `~/.config/systemd/user/rp2350-relayd.service`.

```ini
[Unit]
Description=RP2350 Relay daemon
After=default.target

[Service]
Type=simple
ExecStart=%h/.local/bin/rp2350-relayd --port /dev/ttyACM0 --wait-device
Restart=on-failure
RestartSec=2

[Install]
WantedBy=default.target
```

Document these operator commands during implementation:

```sh
systemctl --user start rp2350-relayd
systemctl --user stop rp2350-relayd
systemctl --user status rp2350-relayd
journalctl --user -u rp2350-relayd
```

Do not recommend lingering in Phase 8b. Operators can start the user service
from their normal session.

## Tests

Automated tests should cover:

- NDJSON parsing, malformed requests, response formatting, typed error mapping,
  and command serialization.
- Python daemon client calls without hardware.
- `rp2350-relayctl` command parsing, output modes, and rejection of direct
  serial options.
- Startup state query without sending `off-all`.
- No-client disconnect without relay state changes.
- Clean shutdown with best-effort `off-all`.
- Reconnect after simulated serial failure and recovery without concurrent
  command races.
- Compatibility for existing direct `RelayClient` and `rp2350-relay` behavior.

Manual Linux smoke check, when hardware is available:

```sh
rp2350-relayd --port /dev/ttyACM0
rp2350-relayctl info
rp2350-relayctl status
rp2350-relayctl pulse 1 100
rp2350-relayctl off-all
```

Expected results:

- The daemon starts as the operator user without root.
- Short-lived client commands complete through the daemon socket.
- The daemon owns the serial port while running.
- All relays are off after `off-all` and after clean daemon shutdown.
