# Host Session Mode

This document is the Phase 8a implementation contract for cross-platform host
session mode. It is implementer-facing first; operator-facing setup and usage
belong in `docs/cli.md`, `docs/host-library.md`, and a session smoke-test
procedure once the implementation lands.

## Scope Boundaries

- Phase 8a session mode is supported on Windows and Linux.
- Session mode is a direct serial operator workflow. It owns one relay
  controller serial connection while connected and serializes operator commands
  through that connection.
- Linux production automation remains covered by Phase 8b daemon mode. Linux
  session mode is for manual operation, diagnostics, and simple direct use.
- The session is independent from firmware communication-loss safety.
- Phase 8a may add only a dummy firmware `heartbeat` command for session
  polling. Do not add communication-loss timeout commands,
  reboot-on-silence behavior, firmware safety actions, heartbeat health state,
  daemon IPC, keepalive sidecars, network APIs, audit logs, or other new relay
  protocol fields.
- Keep existing one-shot direct serial commands available for diagnostics and
  simple checks.

## Entry Point

Keep `rp2350-relay` as the direct serial CLI and add `session` as a subcommand:

```sh
rp2350-relay [--port <serial-port>] [--serial <usb-serial>] \
  [--baud 115200] [--timeout 2.0] [--retries 1] session
```

- `--port` connects directly to the provided serial port, for example `COM7`
  or `/dev/ttyACM0`.
- `--serial` selects a relay controller by exact USB serial number.
- `--port` and `--serial` are mutually exclusive.
- Without `--port` or `--serial`, the session discovers relay controllers and
  prompts the operator to choose from a list.
- `--baud`, `--timeout`, and `--retries` configure the underlying direct
  `RelayClient`.
- Session output is human-readable. The top-level `--output json` option is
  for one-shot commands and must not make the interactive session emit JSON.

## Device Discovery

Use pyserial's `serial.tools.list_ports.comports()` for session-only discovery.
Discovery inspects USB metadata and must not probe arbitrary serial ports with
relay protocol commands.

Discovery returns relay USB candidates. A candidate becomes a confirmed relay
controller only after the session opens the serial port and the startup `info`
and `status` relay protocol requests succeed.

Match relay USB candidates by:

- USB VID `0x2E8A`.
- USB PID `0x0009`.
- Verified candidate: USB product string containing `RP2350-Relay-6CH`.
- Unverified candidate: product string is missing, but a USB serial number is
  present.

Reject ports with wrong VID or PID. Reject anonymous VID/PID-only ports with no
product string and no serial number.

Display matching candidates with at least:

- Port, such as `COM7` or `/dev/ttyACM0`.
- USB serial number, or `unknown` when the host OS does not report one.
- Product string when available.
- `status=unverified` when the product string is missing and the candidate was
  accepted only by VID, PID, and serial number.

Example candidate list:

```text
Relay USB candidates:
  1. port=COM19 serial=B905D541EF8C32DB product=unknown status=unverified
  2. port=/dev/ttyACM0 serial=B905D541EF8C32DB product=RP2350-Relay-6CH SMP CDC
```

Selection rules:

- `rp2350-relay session` always shows the candidate list and asks for a
  numbered selection, even when only one device is present.
- If no matching candidates are found at session launch, print a clear message and
  enter disconnected mode so the operator can plug in hardware and run
  `connect`.
- If the operator enters an invalid selection, report it and prompt again until
  a valid selection is made or the operator cancels.
- If the operator cancels startup selection, exit the session with status `0`
  without entering disconnected mode.
- `--serial <usb-serial>` filters by exact serial number after candidate
  matching. It connects only when exactly one matching candidate exists.
- If a startup `--serial` selector does not match, print the failure, list any
  currently available matching candidates, preserve the serial as the
  preferred reconnect target, and enter disconnected mode.
- If a startup `--port` cannot be opened or queried, print the typed failure,
  list any currently available matching candidates, preserve the port as the
  preferred reconnect target, and enter disconnected mode.
- A selected candidate must pass startup `info` and `status` before the session
  prints a connected banner. A generic Pico 2 or other non-relay firmware may
  appear as an unverified candidate but must fail safely into disconnected mode
  if it does not speak the relay protocol.
- Exact `--port` startup and `connect --port <serial-port>` always open the
  requested port. If that exact port matches a verified or unverified
  candidate, attach its serial and product metadata to the session. Never
  substitute a different discovered port.

USB serial numbers are expected to be unique because the firmware enables
Zephyr hardware-info support for the RP Pico chip ID and the CDC ACM helper uses
hardware info for the USB serial-number descriptor.

## Session Startup

After selecting a device, open exactly one `RelayClient` and keep it open until
the session disconnects or exits.

On successful connection:

- Run `info`.
- Run `status`.
- Print a `/status`-style boxed startup summary containing port, USB serial
  when known, hardware, protocol version, relay count, current state mask,
  channels that are on, and channels that are pulsing.
- Keep exact terminal readability and display style aligned with
  [REPL Plus CLI UX contract](host-cli-ux-repl-plus.md).
- Do not send `off-all` during startup.

If startup `info` or `status` fails after the serial port opens, print the
typed error using the existing CLI labels, close the client, and enter
disconnected mode.

Disconnected startup exits with status `0` if the operator exits without ever
connecting; the startup failure is handled inside the interactive session.

## Prompt Grammar

Parse prompt input with shell-like tokenization, using Python `shlex` behavior.
Blank lines are ignored. Do not add scripting, command history, aliases,
batch-file execution, or stdin batch mode in Phase 8a.

Supported commands while connected:

```text
info
build-info
get [channel]
set <channel> <on|off>
set-all <mask>
pulse <channel> <duration-ms>
off-all
status
reboot
disconnect [--force]
connect [--port <serial-port>|--serial <usb-serial>]
help
exit [--force]
quit [--force]
```

Do not add `smoke` inside the session.

Prompt command semantics:

- Reuse existing one-shot CLI command names and argument conventions.
- Keep session channel arguments one-based and board-label aligned: `1` is
  `CH1` and `6` is `CH6`.
- Convert channels to zero-based values only at the `RelayClient` boundary.
- Reuse existing host-side validation for channel, state mask, on/off state,
  and pulse duration where practical.
- Print command results with the existing human output formatting where
  practical.

## Connected And Disconnected States

The session has two explicit connection states.

Connected state:

- Holds one open `RelayClient`.
- Sends relay commands through that client only.
- Processes one operator command at a time.
- Does not open a second client while the first is connected.
- Runs background heartbeat polling while connected.

Disconnected state:

- Holds no open `RelayClient`.
- Allows only `connect`, `help`, `exit`, and `quit`.
- Rejects relay-control and diagnostic commands with a clear
  not-connected message.
- Preserves the last known port and USB serial for operator visibility only;
  it must not claim current relay state is authoritative after disconnect.

`connect` behavior:

- `connect` with no selector retries a saved startup `--port` or `--serial`
  selector first, then falls back to discovery and shows the numbered device
  list if that retry still fails.
- `connect --port <serial-port>` directly opens that port.
- `connect --serial <usb-serial>` uses exact serial-number discovery.
- `connect` is valid in disconnected mode and may also be used while connected
  only after `disconnect` closes the current client.

Disconnected state is entered when:

- Session launch finds no matching device.
- Session launch cannot find the requested USB serial number.
- Startup `info` or `status` fails after the serial port opens.
- `disconnect` or `disconnect --force` closes the current client.
- `reboot` closes the current client and reconnect by known USB serial number
  does not complete.
- A foreground command gets a transport error that closes or invalidates the
  current client.
- Manual `connect` fails while the session is already disconnected.

Background heartbeat failures do not enter disconnected state.

## USB Removal And Recovery

USB removal and reinsert is handled as an operator recovery flow, not as
automatic reconnect in Phase 8a.

While connected:

- Heartbeat failures print concise status warnings only.
- Heartbeat does not enter disconnected mode, rediscover USB devices, or switch
  the session to a new serial port.
- If the host OS reuses the same serial port after reinsert, a later heartbeat
  or foreground command may work through the existing client again. If a
  heartbeat succeeds after one or more heartbeat failures, print
  `heartbeat: restored` once for that failure streak.
- If the host OS assigns a different serial port after reinsert, heartbeat keeps
  trying the old port until the operator reconnects.

When a foreground command gets a transport error after USB removal, the session
closes the current client and enters disconnected mode. The normal recovery
command is `connect`.

Recovery behavior:

- `connect --serial <usb-serial>` is the preferred recovery command when the
  device USB serial number is known, because it can follow the device to a new
  host serial port.
- `connect --port <serial-port>` opens exactly that port and does not
  substitute another discovered port.
- Plain `connect` retries a saved startup `--port` or `--serial` selector
  first, then falls back to USB discovery and the numbered device list.
- Only the `reboot` command path attempts automatic reconnect by USB serial
  number.

## Heartbeat Polling

Phase 8a adds a dummy relay-management `heartbeat` command for host session
polling:

- Command ID: `9`.
- SMP op: Write.
- Request: empty CBOR map.
- Response: `{"ok": true}`.
- Relay protocol version: `3`.

Firmware heartbeat behavior in this phase is intentionally limited to decoding
the request, returning success, and updating the normal management command
counters. It must not change relay state, enforce communication-loss timeout,
record heartbeat health state, expose new status fields, call `off-all`, or
schedule a reboot.

Session behavior:

- Poll `heartbeat` every 5 seconds while connected.
- Do not print successful background heartbeat polls.
- Print concise status warnings for heartbeat failures.
- After one or more heartbeat failures, print `heartbeat: restored` once when a
  later heartbeat succeeds. Do not repeat restored messages while heartbeat
  remains healthy.
- Keep the same client connected after a heartbeat failure; the next foreground
  command uses that client and succeeds or fails normally.
- Do not expose `heartbeat` as a one-shot CLI command or session prompt command
  in Phase 8a.

## Exit And Disconnect Safety

Normal `exit`, `quit`, and `disconnect` must verify relay state before closing
the client.

Safety check:

- Query `status` through the current client.
- Treat any nonzero `state` or nonzero `pulsing` mask as unsafe to close.
- Treat timeout, transport, protocol, device, or malformed status response as
  unknown and unsafe to close.

Normal close behavior:

- Refuse `exit`, `quit`, or `disconnect` when relay state is on, pulsing, or
  unknown.
- Print the current on and pulsing channels when known.
- Tell the operator to run `off-all` first or use the matching `--force`
  command.

Forced close behavior:

- `exit --force`, `quit --force`, and `disconnect --force` close the client
  without changing relay state.
- Print a warning that the session closed without confirmed all-off state.
- Do not send implicit `off-all` on forced close.

The session must not silently leave an operator thinking relay state was made
safe when `off-all` did not actually run and succeed.

## Reboot And Reconnect

`reboot` sends the existing firmware reboot command through the current client.

After a successful reboot request:

- Print the command result.
- Close the current client because USB CDC may reset.
- If the session knows the device USB serial number, rediscover by exact serial
  number and reconnect when the device reappears.
- If the session does not know a USB serial number, enter disconnected mode and
  tell the operator to run `connect`.

Reconnect after reboot:

- Rediscovery uses the same VID, PID, product, and exact serial matching rules
  as startup discovery.
- If exactly one matching serial-number device appears, connect and run the
  normal startup `info` and `status` banner.
- If reconnect fails or no matching serial-number device appears, enter
  disconnected mode.
- Do not queue relay-control commands across reboot or reconnect.

Future event-capable firmware and host-library versions may deliver the planned
`reset_executing` device event described in
[Relay Management Protocol](protocol/relay-management.md#planned-event). When
available, session mode may print `reset executing` and suppress expected
heartbeat noise during the intentional reset window. The event is advisory;
session reconnect must still confirm the new boot through normal startup
`info` and `status` behavior.

If `reboot` returns a typed error, print it and keep the existing session state
unless the underlying transport error already closed the client.

## Error Handling

Inside an already-open session, command errors do not end the session by
default.

Map errors to the existing CLI labels:

- `RelayValidationError`: `argument error`.
- `RelayTransportError`: `transport error`.
- `RelayTimeoutError`: `timeout error`.
- `RelayProtocolError`: `protocol error`.
- `RelayDeviceError`: `device error`.
- Other `RelayError`: `relay error`.

After a transport error that closes or invalidates the serial connection, close
the current client and enter disconnected mode. Validation, timeout, protocol,
and device errors may keep the session connected when the client remains usable.

## Architecture Notes

- Keep `RelayClient`'s public API unchanged unless a small non-breaking helper
  is needed.
- Implement session orchestration separately from one-shot command handlers.
- Factor shared argument parsing and human formatting only where it avoids real
  duplication with the existing CLI.
- Use a small session state object for current client, port, USB serial,
  product, and disconnected reason.
- Use a small discovery helper so tests can inject deterministic port metadata
  without real USB hardware.
- Do not make discovery a firmware protocol feature in Phase 8a.

## Tests

Automated tests should cover:

- `session` parser entry and missing/invalid selector handling.
- Discovery with no devices, one device, multiple devices, missing serial
  numbers, exact `--serial`, and explicit `--port`.
- Interactive numbered selection without hardware.
- Startup `info` and `status` banner generation.
- Command dispatch for `info`, `build-info`, `get`, `set`, `set-all`, `pulse`,
  `off-all`, `status`, and `reboot`.
- One-connection session ownership across multiple commands.
- Validation, timeout, transport, protocol, and device errors continuing the
  session where appropriate.
- Transport failure moving the session into disconnected mode.
- Background heartbeat polling while connected, including silent healthy polls,
  concise failure status, and one-time restored status after a failure streak.
- Disconnected-mode command restrictions.
- `connect`, `connect --port`, and `connect --serial`.
- `exit`, `quit`, and `disconnect` blocking when relay state is on, pulsing, or
  unknown.
- `exit --force`, `quit --force`, and `disconnect --force`.
- `reboot` closing the client and reconnecting by known USB serial number.
- Existing one-shot direct `rp2350-relay --port <port> <command>` behavior
  remaining unchanged.

Manual smoke checks, when hardware is available:

Windows:

```powershell
rp2350-relay session
status
pulse 1 100
off-all
exit
```

Linux:

```sh
rp2350-relay session
status
pulse 1 100
off-all
exit
```

Expected results:

- The session lists matching relay controllers with port and USB serial number.
- Operator selection opens one long-lived serial connection.
- Session commands complete through that connection.
- Channel arguments remain board-label aligned: `1` is `CH1` and `6` is
  `CH6`.
- Normal exit succeeds only after relays are confirmed off.
