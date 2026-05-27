# Host Daemon Mode

This document is the Phase 8b implementation contract for Linux daemon mode.
It is implementer-facing first; operator-facing setup and usage belong in
`docs/cli.md`, `docs/host-library.md`, and a daemon smoke-test procedure once
the implementation lands.

## Scope Boundaries

- Phase 8b daemon mode is Linux-only.
- Direct manual operation on Windows and Linux is covered by Phase 8a session
  mode. Diagnostics and simple checks remain supported through direct serial
  tooling, for example `rp2350-relay --port COM7 info`.
- The daemon is independent from firmware communication-loss safety.
- Do not add new firmware heartbeat commands, communication-loss timeout
  commands, reboot-on-silence behavior, daemon authentication, audit logs,
  network APIs, or new relay protocol fields.
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
rp2350-relayd (--port /dev/ttyACM0 | --serial <usb-serial>) \
  --socket <path> \
  [--baud 115200] \
  [--timeout 2.0] \
  [--retries 1] \
  [--reconnect-interval 1.0] \
  [--wait-device]
```

- Exactly one device selector is required: `--port` or `--serial`.
- `--port` selects an exact OS serial device path. Reconnect retries the same
  path every `--reconnect-interval` seconds.
- `--serial` selects a relay controller by exact USB serial number using the
  same non-invasive USB VID/PID/product/serial discovery rules as session mode.
  It is preferred for production and multi-device setups because it can follow
  `/dev/ttyACM*` renumbering.
- If `--serial` matches multiple relay candidates, exit nonzero as a
  configuration error, even with `--wait-device`.
- `--socket` is required. One daemon instance owns one relay controller, and
  multiple relay controllers on one Linux host use multiple daemon instances
  with distinct sockets.
- When the socket parent directory does not exist, create it with mode `0700`.
  Bind the socket with mode `0600`.
- If the requested socket path already has a valid live relay daemon, exit
  nonzero with an already-running error. If a live non-daemon listener exists,
  exit nonzero with a path-in-use error. If the socket has no listener, unlink
  it as stale and bind.
- `--timeout` and `--retries` configure firmware request behavior through the
  underlying direct `RelayClient`.
- `--reconnect-interval` is the delay between serial reopen attempts after a
  transport failure.
- Without `--wait-device`, invalid arguments, selector failures, initial serial
  open failures, or readiness-query failures exit nonzero.
- With `--wait-device`, a missing selected device or initially busy serial
  device starts the daemon in disconnected state and retries until the device
  opens. Invalid arguments, duplicate `--serial` matches, and protocol mismatch
  still exit nonzero.
- Log lifecycle events, socket path, serial open/close, client command
  failures, reconnect attempts, and shutdown results to stdout/stderr. Use
  Python logging so systemd can collect the same output through journald.
- Handle `SIGINT` and `SIGTERM` by stopping client accept, letting the active
  firmware command finish or time out, making a best-effort `off-all` attempt
  if the serial connection is open, removing the socket path, closing serial
  resources, and exiting with status `0`. Log any shutdown `off-all` failure.

## Daemon Behavior

- Open exactly one configured relay controller and keep direct firmware access
  inside the daemon.
- Serialize all relay operations through one worker or command queue in the
  order complete request frames are accepted across all client sockets.
- Query `info` and then `status` on startup and after reconnect. These
  readiness queries must not change relay outputs.
- Poll the existing relay-management `heartbeat` command every 5 seconds while
  connected. This is host link-health detection only: successful polls are
  silent, and heartbeat failures do not change relay outputs.
- Keep relay state as commanded when no local client is connected.
- On clean shutdown, make a best-effort `off-all` attempt, then exit even if
  the command fails.
- Reconnect after serial transport failure, firmware reboot, USB reset, or
  device renumbering according to the configured selector. For `--port`, retry
  the same path. For `--serial`, rediscover by exact USB serial number each
  retry and open the current port.
- If a client disconnects while its firmware command is already in progress,
  finish the command and log that the response could not be delivered.
- Log daemon lifecycle, client command failures, reconnect attempts, and clean
  shutdown results.

## Reconnect Policy

- Treat `RelayTransportError`, serial open failures, and disconnected serial
  devices as daemon device-disconnected state.
- Treat heartbeat timeout or transport failure as daemon
  device-disconnected state: close the daemon's current serial client, clear
  `current_port`, set `last_error`, and let the normal reconnect loop recover.
  Do not close daemon IPC client sockets or run `off-all` because of heartbeat
  failure.
- Reject mirrored device commands while disconnected with a daemon transport
  error response. Do not queue device commands across reconnect.
- Continue serving `daemon-status` while disconnected so clients can report the
  current daemon state. `daemon-status` exits successfully when the daemon is
  running, even if the device is disconnected.
- Track `reconnect_attempts` as the current consecutive failed open/readiness
  streak since the last successful connection. Reset it to `0` after readiness
  succeeds.
- Clear `last_error` after readiness succeeds.
- After reconnect, run `info` and then `status` before accepting device
  commands again.
- If a selected port or serial candidate opens but `info` or `status` proves it
  is not a valid relay controller, exit nonzero as a configuration error, even
  with `--wait-device`.
- The daemon `reboot` command returns success once firmware accepts the reboot.
  The daemon then enters disconnected/reconnecting state and rejects queued or
  new device commands until readiness succeeds again.
- Wildcard `/dev/ttyACM*` discovery and interactive selection are deferred
  beyond Phase 8b.

## NDJSON Protocol

Use newline-delimited JSON over an explicit Unix domain socket. Limit
first-phase socket access to the same operator user through parent directory
ownership and socket permissions.

Mirror existing relay commands: `info`, `build-info`, `get`, `set`, `set-all`,
`pulse`, `off-all`, `status`, and `reboot`. Add `daemon-status` for daemon
process and connection state.

Request frames are one JSON object per line:

```json
{"id":"1","command":"set","args":{"channel":0,"on":true}}
```

- `id` is required, may be a string or integer, and is echoed unchanged.
- Other `id` types, including `null`, booleans, objects, and arrays, are
  validation errors.
- `command` is required and must be one of the mirrored relay commands or
  `daemon-status`.
- `args` is optional for commands with no arguments and required for commands
  that need arguments.
- Request lines are limited to 4096 bytes.
- A client may send multiple request frames on one socket. The daemon sends one
  response frame for each request `id` until either side closes the connection.
- Client timeouts on `rp2350-relayctl` and `RelayDaemonClient` cover connecting
  to the daemon socket and waiting for the daemon response. They do not override
  the daemon's configured firmware timeout.

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
- Error objects always include `kind` and `message`. For `device` errors, the
  object may also include firmware `group` and `rc` fields so clients can
  reconstruct `RelayDeviceError`.
- Malformed JSON, missing `id`, missing `command`, unknown commands, invalid
  arguments, firmware errors, and disconnected-device state return structured
  errors without exiting the daemon.
- For malformed JSON where no `id` can be decoded, return `id: null`.
- Oversized request lines return a protocol error when possible, or close the
  connection when a structured response cannot be produced safely.
- Validate arguments in both the daemon client surfaces and the daemon protocol
  handler so raw NDJSON clients cannot bypass host-side validation.

`daemon-status` returns daemon state only. It must not include relay state,
pulsing masks, or last-known relay-output cache fields:

```json
{
  "id": "1",
  "ok": true,
  "result": {
    "connected": false,
    "selector_type": "serial",
    "selector_value": "E6614C311F4B8B2F",
    "current_port": null,
    "socket_path": "/run/user/1000/rp2350-relay/bench-a.sock",
    "reconnect_attempts": 7,
    "last_error": "no relay device found with USB serial E6614C311F4B8B2F",
    "daemon_version": "0.8.5"
  }
}
```

- Required `daemon-status` fields are `connected`, `selector_type`,
  `selector_value`, `current_port`, `socket_path`, `reconnect_attempts`,
  `last_error`, and `daemon_version`.
- Use snake_case field names.
- `selector_type` is `port` or `serial`.
- `current_port` is the open serial path while connected and `null` while
  disconnected.
- `last_error` is `null` after successful readiness.
- Device identity belongs to the `info` command, not `daemon-status`.

## Python API

Add `RelayDaemonClient` beside the direct `RelayClient`.

```python
from rp2350_relay_6ch import RelayDaemonClient

with RelayDaemonClient.connect(timeout_s=2.0) as relay:
    relay.set_relay(0, True)
    relay.off_all()
```

- `RelayDaemonClient.connect(socket_path, timeout_s=2.0)` requires an explicit
  daemon socket path.
- Expose methods matching direct `RelayClient`: `get_info()`,
  `get_build_info()`, `get_relays(channel=None)`, `set_relay(channel, on)`,
  `set_all_relays(state)`, `pulse_relay(channel, duration_ms)`, `off_all()`,
  `get_status()`, and `reboot()`.
- Expose `get_daemon_status()` for `daemon-status`.
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
rp2350-relayctl --socket <path> [--timeout 2.0] \
  [--output human|json] <command>
```

- Commands mirror the direct CLI: `info`, `build-info`, `get`, `set`,
  `set-all`, `pulse`, `off-all`, `status`, and `reboot`.
- Add `daemon-status` for daemon process and connection state. While the daemon
  is running, `daemon-status` exits `0` even when the relay controller is
  disconnected.
- Keep CLI channel arguments one-based and board-label aligned: `1` is `CH1`
  and `6` is `CH6`.
- `rp2350-relayctl` does not accept `--port`, `--baud`, or direct serial
  options in Phase 8b.
- Human and JSON output should match direct CLI output shapes where practical.
- Reuse direct CLI exit codes where practical: `2` for argument or validation
  errors, `3` for daemon unavailable or transport errors, `4` for timeouts,
  `5` for protocol errors, and `6` for device-side relay management errors.

## Named Instances and Systemd User Unit

For production operation, named instances live in one TOML file at
`${XDG_CONFIG_HOME:-~/.config}/rp2350-relay/config.toml`:

```toml
[instances.bench-a]
serial = "E6614C311F4B8B2F"
socket = "${XDG_RUNTIME_DIR}/rp2350-relay/bench-a.sock"
wait_device = true
```

The daemon and daemon-client CLI accept `--instance bench-a`. Explicit CLI
values override environment variables, which override TOML values. Supported
environment overrides use the `RP2350_RELAY_*` prefix, including
`RP2350_RELAY_CONFIG`, `RP2350_RELAY_INSTANCE`, `RP2350_RELAY_PORT`,
`RP2350_RELAY_SERIAL`, `RP2350_RELAY_SOCKET`, and timing/log settings.

Install the generated template unit with:

```sh
rp2350-relayctl systemd install
```

The helper writes `~/.config/systemd/user/rp2350-relayd@.service` and a sample
config file if missing. The generated unit uses the absolute Python interpreter
from the environment that ran the helper:

```ini
ExecStart=/path/to/python -m rp2350_relay_6ch.daemon --instance %i
```

This supports `pipx`, conda, and venv installs without relying on shell
activation or systemd's inherited `PATH`. Operators can pass
`--python /path/to/env/bin/python` to select a specific environment.

Document these operator commands:

```sh
systemctl --user daemon-reload
systemctl --user start rp2350-relayd@bench-a
systemctl --user stop rp2350-relayd@bench-a
systemctl --user status rp2350-relayd@bench-a
journalctl --user -u rp2350-relayd@bench-a
rp2350-relayctl systemd doctor --instance bench-a
```

Do not recommend lingering in Phase 8b. Operators can start the user service
from their normal session.

For multiple relay controllers, add multiple TOML instances and start multiple
template units such as `rp2350-relayd@bench-a.service` and
`rp2350-relayd@bench-b.service`.

## Tests

Automated tests should cover:

- NDJSON parsing, malformed requests, response formatting, typed error mapping,
  request ID validation, frame-size limits, persistent client connections, and
  FIFO command serialization.
- Python daemon client calls without hardware.
- `rp2350-relayctl` command parsing, output modes, and rejection of direct
  serial options.
- Required explicit socket arguments for the daemon and daemon clients.
- `--serial` selection, missing serial with and without `--wait-device`,
  duplicate serial rejection, and serial rediscovery after renumbering.
- Startup `info` then `status` readiness without sending `off-all`.
- `daemon-status` success while disconnected and mirrored device-command
  transport errors while disconnected.
- Socket parent creation, permissions, already-running detection, path-in-use
  detection, and stale socket unlinking.
- No-client disconnect without relay state changes.
- Clean shutdown with in-flight command handling and best-effort `off-all`.
- Reconnect after simulated serial failure, firmware reboot, and recovery
  without concurrent command races.
- Compatibility for existing direct `RelayClient` and `rp2350-relay` behavior.

Manual Linux smoke check, when hardware is available:

```sh
SOCKET="$XDG_RUNTIME_DIR/rp2350-relay/bench-a.sock"
rp2350-relayd --serial <usb-serial> --socket "$SOCKET" --wait-device
rp2350-relayctl --socket "$SOCKET" daemon-status
rp2350-relayctl --socket "$SOCKET" info
rp2350-relayctl --socket "$SOCKET" status
rp2350-relayctl --socket "$SOCKET" pulse 1 100
rp2350-relayctl --socket "$SOCKET" off-all
```

Expected results:

- The daemon starts as the operator user without root.
- Short-lived client commands complete through the daemon socket.
- The daemon owns the serial port while running.
- All relays are off after `off-all` and after clean daemon shutdown.
