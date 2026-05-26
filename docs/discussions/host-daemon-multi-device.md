# Host daemon multi-device model

Date: 2026-05-26

Status: Discussion and decision record. This note records the design reasoning
behind the Phase 8b host daemon multi-device model. It does not change the
authoritative PRD, implementation plan, daemon contract, protocol, host
behavior, or verification status unless those documents are updated explicitly.

Authoritative Phase 8b daemon behavior lives in
[Host Daemon Mode](../host-daemon-mode.md). The phase summary lives in
[Phase 8b Plan](../phase-8b-plan.md).

## Summary

The daemon mode discussion started from one practical problem: a Linux host may
have more than one RP2350 relay controller attached, and `/dev/ttyACM*` names
can renumber after USB reset, firmware reboot, or unplug/replug. The daemon
needs stable production targeting without turning Phase 8b into a multi-device
management platform.

The chosen model is one daemon instance per relay controller. Each daemon owns
one physical controller, one serial connection, one command queue, and one
explicit Unix socket. Multiple controllers on the same host are represented as
multiple daemon instances with distinct sockets. Higher-level orchestration can
coordinate those instances later if needed.

Phase 8b should provide the core primitives directly: exact `--port`, exact
USB `--serial`, explicit `--socket`, daemon-client IPC, reconnect, readiness
checks, and clean shutdown. It should not add named-instance CLI options,
configuration-file parsing, audit logs, network APIs, firmware heartbeat, or
relay-state caching.

## Current contract result

The resulting Phase 8b daemon contract uses this shape:

```sh
rp2350-relayd (--port /dev/ttyACM0 | --serial <usb-serial>) \
  --socket <path> \
  [--baud 115200] \
  [--timeout 2.0] \
  [--retries 1] \
  [--reconnect-interval 1.0] \
  [--wait-device]
```

Daemon clients also require an explicit socket:

```sh
rp2350-relayctl --socket <path> daemon-status
rp2350-relayctl --socket <path> status
```

The important ownership mapping is:

```text
human role/name -> daemon instance/socket -> relay selector -> current port
```

For example:

```sh
SOCKET="$XDG_RUNTIME_DIR/rp2350-relay/bench-a.sock"
rp2350-relayd \
  --serial E6614C311F4B8B2F \
  --socket "$SOCKET" \
  --wait-device
```

This can be wrapped by a `systemd --user` unit for production operation. Phase
8b documents explicit unit examples only; it does not define named instances,
environment-file schema, TOML schema, or `rp2350-relayctl --instance`.

## One daemon per controller

Two daemon ownership models were considered:

- One daemon instance per physical relay controller.
- One daemon process managing all attached relay controllers.

The chosen model is one daemon instance per controller.

Rationale:

- Failure isolation is stronger. A rebooting, disconnected, or misconfigured
  controller does not affect unrelated controllers.
- Serial ownership stays crisp: one process owns one physical controller.
- The daemon API can mirror the existing direct `RelayClient` without adding a
  device selector to every IPC request.
- The worker, reconnect loop, socket state, and shutdown policy stay
  straightforward enough for Phase 8b tests.
- Multi-controller orchestration can be layered above multiple daemon
  instances later.

The rejected single-daemon-for-all-controllers model could eventually support
central inventory, aggregate status, and coordinated multi-board actions, but
it would require a multi-device IPC schema, target selectors on every command,
per-device reconnect state, per-device error reporting, and a larger Python
API. That is more than Phase 8b needs.

The safety invariant is the same in both models: two normal control owners
should not operate the same physical relay controller at the same time.

## Selector policy

The original daemon contract required `--port`. That was acceptable for a
single-board prototype but weak for production multi-device use because Linux
CDC ACM names can change.

The chosen selector policy is:

- Require exactly one selector: `--port` or `--serial`.
- `--port` opens one exact OS serial device path and retries that same path.
- `--serial` uses existing session-mode USB discovery by VID, PID, product
  string, and exact USB serial number.
- `--serial` is preferred for production and multi-device setups.
- Interactive discovery belongs to session mode, not daemon startup.

`--serial` behavior:

- If zero matching devices are found without `--wait-device`, daemon startup
  exits nonzero.
- If zero matching devices are found with `--wait-device`, the daemon starts
  disconnected and keeps searching.
- If multiple matching devices are found, startup exits nonzero as a
  configuration error, even with `--wait-device`.
- On disconnect or USB renumbering, reconnect repeats exact serial discovery
  each interval and opens the current port when found.
- If the selected candidate opens but `info` or `status` proves it is not a
  valid relay controller, startup exits nonzero as a configuration error.

This keeps daemon startup deterministic. Operators can use session-mode
discovery or setup notes to identify serial numbers, then put stable serials
into daemon commands or service files.

## Socket and instance targeting

The default socket path from the earlier daemon draft worked for one board, but
it became ambiguous for multiple controllers. The decision was to require an
explicit socket for both daemon and daemon clients.

Rationale:

- A required socket makes target selection visible at every daemon-client call.
- Multiple boards can coexist without a hidden default target.
- The Phase 8b implementation does not need a role-name registry, config file,
  or instance name resolver.
- `systemd --user` units can still give operators stable names by choosing
  stable socket paths.

The socket setup policy is:

- The daemon may create the socket parent directory with mode `0700`.
- The socket should be bound with mode `0600`.
- If the socket path has a valid live relay daemon, the new daemon exits with
  an already-running error.
- If the socket path has a live non-daemon listener, the new daemon exits with
  a path-in-use error.
- If the socket path exists but has no listener, it may be unlinked as stale.

This avoids disrupting a live unrelated service while still recovering from
normal stale Unix socket files after a crash.

## Static examples, not instance config

Three instance-configuration directions were compared:

- Examples only.
- A `systemd --user` template with `EnvironmentFile`.
- Project-owned TOML config.

Phase 8b chooses examples only. The daemon exposes `--serial` and `--socket`;
operators or service files provide those values explicitly.

Rationale:

- It keeps Phase 8b focused on daemon ownership, IPC, reconnect, and shutdown.
- It avoids adding config precedence, schema validation, path expansion, and
  config-file tests before the daemon exists.
- It does not block future systemd template or TOML support if repeated
  deployment shows a real need.

An example service can map a human role to a serial and socket:

```ini
[Service]
Type=simple
ExecStart=%h/.local/bin/rp2350-relayd \
  --serial E6614C311F4B8B2F \
  --socket %t/rp2350-relay/bench-a.sock \
  --wait-device
Restart=on-failure
RestartSec=2
```

For multiple controllers, copy or write multiple explicit unit files with
different serial numbers and socket paths.

## Device status versus daemon status

The direct CLI already has a device-oriented `status` command. Overloading that
command with daemon state would make disconnected behavior ambiguous.

The chosen API split is:

- `status` remains a device command.
- `daemon-status` reports daemon process and connection state.
- While disconnected, mirrored device commands including `status` fail with a
  transport error.
- While disconnected, `daemon-status` succeeds if the daemon process is
  running.

The required `daemon-status` fields are:

- `connected`
- `selector_type`
- `selector_value`
- `current_port`
- `socket_path`
- `reconnect_attempts`
- `last_error`
- `daemon_version`

`daemon-status` uses snake_case fields and does not include device identity.
Device identity belongs to `info`.

## No public relay-state cache

The discussion initially considered including last-known relay state in
`daemon-status`, but pulse behavior makes that easy to misread.

A pulse response can report a relay as on and pulsing while the firmware later
turns it off internally. Without device-originated events, the daemon cannot
know exactly when that happened unless it queries the device again. Presenting
last-known relay masks in `daemon-status` would look useful but could be
mistaken for current physical output state.

The decision is:

- `daemon-status` must not expose relay state, pulsing masks, or last-known
  relay-output cache fields.
- Authoritative relay state comes from device commands such as `status`.
- The daemon may use readiness responses internally, but it does not publish
  them as cached relay state.

This keeps the operator-facing semantic simple: if relay output state matters,
ask the device.

## Readiness, reconnect, and reboot

The daemon should not consider a serial connection usable merely because the OS
opened the port. On startup and reconnect, it runs `info` and then `status`.
Those queries confirm relay protocol identity and command path readiness
without changing outputs.

Reconnect behavior:

- Treat transport failures, open failures, and device disappearance as
  disconnected state.
- Reject device commands while disconnected.
- Serve `daemon-status` while disconnected.
- Track `reconnect_attempts` as the current failed reconnect streak and reset
  it to `0` after readiness succeeds.
- Clear `last_error` after readiness succeeds.

The daemon `reboot` command returns success once firmware accepts the reboot.
The daemon then enters reconnecting state and rejects queued or new device
commands until readiness succeeds again. It does not hold the client open until
the device returns.

## IPC and concurrency

The daemon protocol uses newline-delimited JSON over a Unix domain socket.
Connections are persistent: a client may send multiple request frames on one
socket and receive one response per request ID.

Concurrency decisions:

- Commands are serialized FIFO by accepted complete request frame across all
  client sockets.
- Client timeouts cover socket connection and daemon response wait time. They
  do not override the daemon's firmware request timeout.
- If a client disconnects while its firmware command is already in progress,
  the daemon finishes the firmware command and logs that the response could not
  be delivered.
- Raw NDJSON clients cannot bypass validation; daemon clients and the daemon
  protocol handler both validate arguments.

Protocol hygiene decisions:

- Request IDs must be strings or integers and are echoed unchanged.
- Request lines are limited to 4096 bytes.
- Error objects require `kind` and `message`.
- Device errors may include firmware `group` and `rc` so Python clients can
  reconstruct `RelayDeviceError`.

These rules keep the protocol scriptable without creating a large daemon error
taxonomy in Phase 8b.

## Shutdown policy

On `SIGINT` or `SIGTERM`, the daemon should stop accepting new clients, let the
active firmware command finish or time out, attempt best-effort `off-all` if
the serial connection is open, remove the socket path, close serial resources,
and exit with status `0`.

Rationale:

- Finishing only the active command avoids silently cutting a firmware request
  in half.
- Draining queued client commands could delay shutdown unpredictably.
- A nonzero exit after a requested signal could make `systemd` restart the
  daemon because of `Restart=on-failure`.
- Failed shutdown `off-all` must be logged, but the daemon should not loop
  indefinitely trying to prove state it may no longer be able to observe.

## Direct diagnostics

`rp2350-relay` remains the direct serial diagnostic CLI. It is an escape hatch
for manual checks and troubleshooting, not the normal production control path
when the daemon owns a device.

The chosen Phase 8b behavior is documentation and operator discipline, not
active cross-tool enforcement. Operators should stop the daemon before using
direct serial commands against the same controller. Enforcing that relationship
would require lock or registry policy that is outside Phase 8b.

## Rejected or deferred scope

The daemon discussion explicitly did not promote these features into Phase 8b:

- One daemon process managing all relay controllers.
- Network APIs.
- Daemon authentication or group access.
- Audit logs.
- Firmware heartbeat or communication-loss timeout commands.
- Firmware reboot-on-silence behavior.
- Persistent relay-on state.
- Public daemon relay-state cache.
- Named-instance CLI flags.
- Environment-file or TOML config parsing.
- Dynamic controller discovery in normal daemon startup.
- `rp2350-relayctl discover`.
- Direct-serial lock enforcement between `rp2350-relay` and `rp2350-relayd`.

Those ideas can be revisited later, but Phase 8b should stay focused on the
smallest reliable Linux production ownership model: one local daemon, one
relay controller, one explicit socket, deterministic local RPC.
