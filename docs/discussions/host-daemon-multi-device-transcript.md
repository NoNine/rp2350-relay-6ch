# Host daemon multi-device transcript

Date: 2026-05-26

Status: Transcript-style discussion record. This note preserves the substance
and chronology of the Phase 8b host daemon-mode discussion. It is not an
implementation contract and does not change the authoritative PRD,
implementation plan, daemon contract, protocol, host behavior, or verification
status unless those documents are updated explicitly.

Authoritative Phase 8b daemon behavior lives in
[Host Daemon Mode](../host-daemon-mode.md). The summarized decision record is
[Host daemon multi-device model](host-daemon-multi-device.md). The phase
summary is [Phase 8b Plan](../phase-8b-plan.md).

## Starting point

The discussion began with a request to continue study of host CLI daemon mode
and to read `docs/host-daemon-mode.md`. The initial scope was design-only: no
code implementation.

The existing daemon contract at that time said:

- Phase 8b daemon mode is Linux-only.
- `rp2350-relay` remains the direct serial diagnostic CLI.
- `rp2350-relayd` is the daemon process.
- `rp2350-relayctl` is the daemon-client CLI.
- `RelayDaemonClient` sits beside the direct `RelayClient`.
- The daemon uses newline-delimited JSON over a same-user Unix domain socket.
- `--port` is required.
- The daemon opens exactly one configured USB CDC serial port.
- The daemon serializes relay operations, queries firmware state on startup,
  keeps relay state when no client is connected, and attempts best-effort
  `off-all` on clean shutdown.
- Firmware communication-loss safety, daemon authentication, audit logs,
  network APIs, firmware heartbeat commands, and new firmware protocol fields
  are out of Phase 8b scope.

The existing host code showed a useful split:

- `host/rp2350_relay_6ch/cli.py` owns direct one-shot command parsing,
  formatting, exit codes, and error mapping.
- `host/rp2350_relay_6ch/session.py` owns long-lived direct session behavior.
- `host/rp2350_relay_6ch/discovery.py` already implements non-invasive USB
  discovery for relay candidates by VID, PID, product string, and USB serial.

## Status ambiguity

The first design ambiguity was the meaning of `status`.

The daemon contract said clients should continue to report daemon state while
the device is disconnected. The direct CLI already used `status` as a device
command. Overloading `status` would make disconnected behavior ambiguous.

Decision:

- Keep `status` device-oriented.
- Add a separate daemon-specific command named `daemon-status`.
- `rp2350-relayctl status` stays aligned with direct device status.
- `daemon-status` reports daemon process and connection state.

Rationale:

- Scripts and operators can continue to treat `status` as a device query.
- Daemon state remains available while disconnected.
- The daemon API avoids mixing process health with relay-controller state.

## Multi-device question

A socket-path collision question exposed a larger design issue: how should one
Linux host handle multiple attached relay controllers?

Two models were discussed:

- One daemon instance per relay controller.
- One daemon process managing all relay controllers.

The selected direction was one daemon instance per relay controller.

Rationale:

- Stronger failure isolation.
- Crisp serial ownership: one process owns one physical controller.
- Existing `RelayClient` shape maps cleanly into the daemon without adding a
  device selector to every IPC request.
- Each daemon has one reconnect loop, one worker queue, and one socket.
- A future orchestration layer can coordinate multiple daemon instances if
  needed.

Rejected alternative:

- A single daemon managing all controllers might later support inventory,
  aggregate status, and multi-board workflows, but it would require device
  selectors in IPC, per-device reconnect state, per-device errors, and a
  broader Python API. That was too much for Phase 8b.

The resulting model:

```text
human role/name -> daemon instance/socket -> relay selector -> current port
```

## Selector policy

The current contract's `--port` requirement was identified as weak for
multi-device production use because `/dev/ttyACM*` can renumber after firmware
reboot, USB reset, or unplug/replug.

Decision:

- `rp2350-relayd` requires exactly one selector: `--port` or `--serial`.
- `--port` selects an exact OS serial device path and retries that same path.
- `--serial` selects a relay controller by exact USB serial number using the
  existing non-invasive USB discovery logic.
- `--serial` is preferred for production and multi-device setups.
- If neither selector is provided, startup rejects the arguments.
- Interactive discovery remains in session mode, not daemon startup.

`--serial` startup behavior:

- No matching device without `--wait-device`: exit nonzero.
- No matching device with `--wait-device`: start disconnected and keep
  searching.
- Multiple matching devices: configuration error, even with `--wait-device`.
- Candidate opens but fails `info` or `status`: configuration error, even with
  `--wait-device`.

Reconnect behavior:

- With `--port`, retry the same path.
- With `--serial`, rediscover by exact serial each interval and open the
  current port.

## Static instance mapping

The production model was discussed as static mapping rather than dynamic
selection:

```text
bench-a -> daemon socket -> USB serial selector -> current port
```

Example:

```sh
rp2350-relayd \
  --serial E6614C311F4B8B2F \
  --socket "$XDG_RUNTIME_DIR/rp2350-relay/bench-a.sock" \
  --wait-device
```

Client:

```sh
rp2350-relayctl \
  --socket "$XDG_RUNTIME_DIR/rp2350-relay/bench-a.sock" \
  status
```

Three instance-configuration options were compared:

- Examples only.
- `systemd --user` template with `EnvironmentFile`.
- Project-owned TOML config.

Decision:

- Phase 8b uses examples only.
- The daemon supports explicit `--serial` and `--socket`.
- Documentation may show static unit examples.
- Phase 8b does not add named-instance CLI, environment-file parsing, TOML
  config, or `rp2350-relayctl --instance`.

Rationale:

- This keeps Phase 8b focused on daemon ownership, IPC, reconnect, and
  shutdown.
- It avoids adding config schema, path expansion, precedence, and validation
  policy before the daemon exists.
- Future config support remains possible.

## Socket policy

The original default socket path was useful for one board but ambiguous for
multiple controllers.

Decision:

- `rp2350-relayd` requires `--socket`.
- `rp2350-relayctl` requires `--socket`.
- `RelayDaemonClient.connect(socket_path, timeout_s=...)` requires an explicit
  socket path.

Socket setup decisions:

- Create a missing socket parent directory with mode `0700`.
- Bind the socket with mode `0600`.
- If the requested socket path has a valid live relay daemon, exit with an
  already-running error.
- If the socket path has a live non-daemon listener, exit with a path-in-use
  error.
- If the socket exists but has no listener, unlink it as stale and bind.

This separates stale socket cleanup from live-socket ownership and avoids
disrupting unrelated services.

## Daemon status shape

The daemon-specific status command was named `daemon-status`.

Decision:

- Expose it in both public daemon-client surfaces:
  `rp2350-relayctl daemon-status` and
  `RelayDaemonClient.get_daemon_status()`.
- Use snake_case JSON field names.
- While the daemon process is running, `daemon-status` exits successfully even
  when the relay controller is disconnected.

Required fields:

- `connected`
- `selector_type`
- `selector_value`
- `current_port`
- `socket_path`
- `reconnect_attempts`
- `last_error`
- `daemon_version`

Additional decision:

- Do not include device identity in `daemon-status`.
- Device identity belongs to the `info` command.

## Relay-state cache discussion

The discussion considered exposing last-known relay state through
`daemon-status`. This was rejected.

Reason:

- Pulse state is time-dependent. A pulse command can return `state` and
  `pulsing` masks while the firmware later turns the relay off internally.
- Without device-originated events, the daemon cannot know exactly when a pulse
  ended unless it queries the device again.
- Exposing last-known relay masks in `daemon-status` risks making stale state
  look authoritative.

Decision:

- `daemon-status` does not expose relay state, pulsing masks, or last-known
  relay-output cache fields.
- Authoritative relay state comes from device commands such as `status`.
- While disconnected, mirrored device commands including `status` fail with a
  transport error.
- While disconnected, `daemon-status` still succeeds.

The daemon may use readiness responses internally, but it does not publish them
as cached relay state.

## Readiness and reconnect

The daemon should not mark a connection usable just because the serial port
opened.

Decision:

- Startup and reconnect readiness runs `info` and then `status`.
- These readiness queries must not change relay outputs.
- `reconnect_attempts` counts the current consecutive failed open/readiness
  streak and resets to `0` after readiness succeeds.
- `last_error` clears after readiness succeeds.
- Protocol mismatch is a configuration error, even with `--wait-device`.

Rationale:

- `info` confirms relay firmware identity and protocol shape.
- `status` confirms the device command path before accepting control commands.
- Readiness remains deterministic and testable.

## Reboot behavior

Three behaviors were considered for daemon-client `reboot`:

- Return once firmware accepts the reboot.
- Wait until reconnect completes.
- Reject reboot in daemon mode.

Decision:

- Return success once firmware accepts `reboot`.
- The daemon then enters disconnected/reconnecting state asynchronously.
- Queued or new device commands fail until readiness succeeds again.

Rationale:

- Short-lived clients do not need to remain open across USB reset.
- Waiting for reconnect would complicate client timeout semantics.
- Rejecting reboot would remove a useful controlled firmware workflow.

## IPC and queueing

The daemon protocol uses newline-delimited JSON over Unix domain sockets.

Decisions:

- Connections are persistent. A client may send multiple request frames on one
  socket and receive one response per request ID.
- Commands are serialized FIFO by accepted complete request frame across all
  client sockets.
- Client timeouts cover socket connection and waiting for the daemon response.
- Client timeouts do not override the daemon's configured firmware timeout.
- If a client disconnects while its firmware command is in progress, the daemon
  finishes the firmware command and logs that the response could not be
  delivered.
- Both daemon client surfaces and the daemon protocol handler validate
  arguments.

This keeps short-lived CLI behavior simple while allowing the Python daemon
client to reuse one connection.

## Protocol hygiene

Request ID decision:

- Request `id` must be a string or integer.
- The daemon echoes `id` unchanged.
- Missing IDs and IDs with other JSON types are validation errors.

Frame-size decision:

- Request lines are limited to 4096 bytes.
- Oversized frames return a protocol error when possible or close the
  connection when a structured response cannot be produced safely.

Error object decision:

- Error objects require `kind` and `message`.
- Error kinds are `validation`, `transport`, `timeout`, `protocol`, `device`,
  and `daemon`.
- `device` errors may also include firmware `group` and `rc` so clients can
  reconstruct `RelayDeviceError`.
- No broader daemon error-code taxonomy is added in Phase 8b.

## Shutdown behavior

On `SIGINT` or `SIGTERM`, the daemon behavior was settled as:

- Stop accepting new clients.
- Let the active firmware command finish or time out.
- Do not drain queued client commands.
- Attempt best-effort `off-all` if the serial connection is open.
- Remove the socket path and close serial resources.
- Exit with status `0` even if best-effort `off-all` fails.
- Log any shutdown `off-all` failure.

Rationale:

- Interrupting an in-flight firmware request is less deterministic.
- Draining queued commands can delay shutdown unexpectedly.
- Exiting nonzero on requested shutdown could trigger unwanted systemd restart
  with `Restart=on-failure`.

## Direct diagnostics

The direct `rp2350-relay` CLI remains available for diagnostics and simple
checks. The daemon contract treats it as an escape hatch.

Decision:

- Operators should stop the daemon before using direct serial commands against
  the same device.
- Phase 8b does not add active lock enforcement between direct CLI and daemon.

Rationale:

- Enforcement would require lock-file or registry policy outside the smallest
  reliable daemon path.
- The direct CLI remains useful for manual troubleshooting.

## Documentation update

After the design decisions stabilized, the documentation was updated:

- `docs/host-daemon-mode.md` became the detailed implementation contract for
  the new selector, socket, `daemon-status`, IPC, reconnect, reboot, and
  shutdown behavior.
- `docs/phase-8b-plan.md` was updated as the shorter phase-level summary.
- `docs/implementation-plan.md` was adjusted to remove stale language about
  cached recent status and default runtime socket paths.
- `docs/discussions/host-daemon-multi-device.md` was added as a decision-log
  discussion note.

Those changes were committed as:

```text
3cb0490 Document daemon multi-device model
```

Commit context recorded that the discussion chose one daemon per relay
controller, exact `--serial`, required explicit sockets, examples-only static
instance guidance, `daemon-status`, no public relay-state cache, and clarified
socket collision, reconnect readiness, reboot, FIFO serialization, IPC
timeouts, and shutdown policy.

## Follow-up transcript request

After the decision-log note was committed, the user asked to also save the
"typescript records". That was clarified to mean a conversation
transcript-style record rather than a terminal `script` log or TypeScript code.

The chosen transcript plan was:

- Add `docs/discussions/host-daemon-multi-device-transcript.md`.
- Preserve the chronological substance of the daemon discussion.
- Include key user choices and rationale.
- Avoid raw tool output unless it materially affected the discussion.
- Do not commit the transcript file unless explicitly requested later.

This file is the result of that follow-up request.

## Original prompt

The detailed restart prompt that framed this discussion is preserved here for
traceability. It is context for the discussion, not an implementation contract.

```text
You are working in /home/ubuntu/zephyrproject/apps/rp2350-relay-6ch.

  Task: continue design discussion only. Do not implement code unless explicitly asked later.

  Context:
  We have been discussing Phase 8b host daemon mode for the RP2350 relay controller project, especially the daemon mapping model when multiple relay
  controllers are attached to one Linux host.

  Important repo docs:
  - docs/host-daemon-mode.md is the current implementation contract.
  - docs/phase-8b-plan.md is the Phase 8b plan and points to host-daemon-mode.md.
  - docs/host-session-mode.md is Phase 8a direct session mode.
  - Current host CLI code is under host/rp2350_relay_6ch/.
  - Existing discovery support is in host/rp2350_relay_6ch/discovery.py.

  Current daemon contract summary:
  - Linux-only daemon mode.
  - Keep rp2350-relay as direct serial diagnostic CLI.
  - Add rp2350-relayd for daemon process.
  - Add rp2350-relayctl for daemon-client commands.
  - Add RelayDaemonClient beside direct RelayClient.
  - Use NDJSON over a same-user Unix domain socket.
  - Current contract says --port is required and daemon opens exactly one configured USB CDC serial port.
  - Direct firmware communication-loss safety, daemon auth, audit logs, network APIs, firmware heartbeat commands, and new protocol fields are out of Phase
  8b scope.
  - Daemon should serialize relay operations, query firmware state on startup without changing relays, keep relay state when no client is connected, best-
  effort off-all on clean shutdown, reconnect after serial failure, and continue serving daemon status while disconnected.

  Discussion decisions/direction so far:
  1. Multi-device model:
     Prefer one daemon instance per relay controller, not one daemon managing all controllers.
     Rationale:
     - Stronger failure isolation.
     - Keeps serial ownership crisp: one process owns one physical board.
     - Matches existing direct RelayClient shape and current daemon contract.
     - Avoids adding a device selector to every daemon IPC request.
     - A future orchestration layer can coordinate multiple daemon instances if needed.

  2. Multi-device implication:
     Multiple boards on one Linux host should be handled by multiple daemon instances, each with a distinct socket and one configured relay selector.

  3. Device selection issue:
     Current contract says --port is required, but this is weak for multi-device production because /dev/ttyACM0 can renumber.
     Proposed revision:
     - Daemon requires exactly one selector: --port or --serial.
     - --port selects an exact OS serial device path and retries that same path.
     - --serial selects a relay controller by exact USB serial number using existing USB discovery logic.
     - --serial is preferred for production and multi-device setups.
     - If --serial matches zero devices:
       * without --wait-device: exit nonzero.
       * with --wait-device: start disconnected and keep searching.
     - If --serial matches multiple devices: treat as configuration/validation error.
     - If neither selector is provided: reject; interactive discovery belongs to session mode, not daemon startup.

  4. Static systemd instance mapping:
     The likely production model is statically configured daemon instances:
     - rp2350-relayd@bench-a.service maps to a config/env file for instance bench-a.
     - That config binds bench-a to a stable USB serial number and socket path.
     - Example conceptual mapping:
       human role/name -> daemon instance/socket -> USB serial selector -> current port
     - Example:
       rp2350-relayd --serial E6614C311F4B8B2F --socket "$XDG_RUNTIME_DIR/rp2350-relay/bench-a.sock" --wait-device
     - Client:
       rp2350-relayctl --socket "$XDG_RUNTIME_DIR/rp2350-relay/bench-a.sock" status
     - Static mapping is intentional for production safety. Dynamic discovery belongs to setup/manual tooling, not normal daemon startup.

  5. Status API ambiguity:
     User chose to keep `status` as device-oriented and add a separate daemon status surface.
     Implication:
     - `rp2350-relayctl status` should stay aligned with direct CLI/device status.
     - Add a daemon-specific command such as `daemon-status` or similar for daemon process/connection/reconnect/cache state.
     - This avoids overloading `status` with daemon metadata.

  6. Existing socket path issue:
     A previous question about stale sockets exposed the multi-device issue.
     No final decision yet.
     Need discuss in terms of one daemon per instance:
     - What should happen if the socket path already exists?
     - Probably: if a live daemon is listening, exit with "already running for this socket/instance"; if stale, unlink and bind.
     - But this should be framed with instance/socket ownership, not "multiple daemons for same relay."

  7. GPU analogy used:
     Multi-GPU software often uses crisp per-device ownership/context/worker and explicit targeting/visibility, with orchestration layered above.
     Lesson applied:
     - Keep per-relay daemon ownership simple.
     - Use stable identity/targeting.
     - Add orchestration above multiple daemon instances later if needed.

  Potential next discussion topics:
  - Should the Phase 8b contract be amended now to include --serial daemon selection, or keep --port-only for first implementation and document --serial as
  Phase 8c?
  - What should the daemon-specific status command be named: daemon-status, health, daemon-info, or something else?
  - What exact fields should daemon-status return in human/json modes?
    Suggested fields:
    connected/disconnected
    selector_type: port|serial
    selector_value
    current_port if connected
    socket_path
    last_known_state/cache freshness
    reconnect_attempts or last_error
    daemon version
  - What should the default socket path be when using --serial or named instances?
    Current default $XDG_RUNTIME_DIR/rp2350-relay.sock only works for single-board setups.
    Multi-instance may need explicit --socket, or a default derived from instance name/serial later.
  - Should named instance support be in Phase 8b CLI directly, or only systemd/example docs?
  - How should systemd template config be represented?
    Options:
    * EnvironmentFile=%h/.config/rp2350-relay/%i.env
    * config file under ~/.config/rp2350-relay/instances/%i.toml
    * keep Phase 8b as raw ExecStart examples only.
  - Should rp2350-relayctl get a `discover` command, or should discovery remain only in rp2350-relay session for Phase 8b?
  - How to handle socket path collision/stale socket safely.
  - How direct serial diagnostics should warn or fail if daemon owns the same device. Current contract says operators should stop daemon before direct
  serial use; no enforcement planned.

  Important constraints from repo AGENTS/docs:
  - Do not implement future-looking features unless explicitly promoted into PRD/implementation plan/phase plan.
  - Avoid adding network control, persistent relay-on state, telemetry claims, SmartPDU-like behavior, audit logs, aliases, monitoring, or dual-core scope
  incidentally.
  - Keep v1 smallest reliable path: local relay control, deterministic host RPC, firmware update/rollback later.
  - Do not create/update phase verification reports unless explicitly requested.
  - Do not retrofit completed phase plans unless explicitly asked.
  - Before adding/refining repo rules/docs, search existing guidance and update the single most authoritative location.

  Suggested stance:
  Recommend updating docs/host-daemon-mode.md and docs/phase-8b-plan.md later, if the user asks for documentation edits, to:
  - Replace "--port required" with "exactly one selector: --port or --serial".
  - State one daemon owns exactly one relay controller.
  - State multiple controllers use multiple daemon instances with distinct sockets.
  - Add daemon-status as separate from device status.
  - Clarify socket collision behavior.
  - Clarify systemd user template/static instance mapping as production model or future extension depending on user preference.

  Remember: user asked for discussion/study, no implementation unless they explicitly ask.
```

## Deferred or rejected scope

The discussion repeatedly kept the following out of Phase 8b:

- One daemon process managing all relay controllers.
- Network control.
- Daemon authentication, group access, or audit logs.
- Firmware heartbeat commands.
- Firmware communication-loss timeout commands.
- Firmware reboot-on-silence behavior.
- Persistent relay-on state.
- Public relay-state cache in `daemon-status`.
- Named-instance daemon CLI.
- Environment-file or TOML config parsing.
- Dynamic controller discovery in normal daemon startup.
- `rp2350-relayctl discover`.
- Active direct-CLI-versus-daemon lock enforcement.

These may be considered later only if explicitly promoted into the PRD,
implementation plan, or a future phase plan.
