# Phase 8a Plan: Cross-Platform Session Mode

## Summary

Phase 8a adds session mode as a cross-platform manual/direct operator
workflow. The session is independently useful before firmware
communication-loss safety: it owns one relay-controller serial connection
while connected, serializes relay commands through the existing direct host
client path, and gives an operator one long-lived control surface instead of
repeated short serial opens.

Use [Host session mode](host-session-mode.md) as the decision-complete
implementation contract for session CLI shape, USB discovery, prompt grammar,
safe exit and disconnect behavior, reconnect behavior, and tests.

Linux production automation remains covered by Phase 8b daemon mode. Linux
session mode is for manual operation, diagnostics, and simple direct use.

This phase may add only a dummy firmware `heartbeat` command for session
polling. It must not add communication-loss timeouts, firmware
reboot-on-silence behavior, firmware safety actions, heartbeat health state,
daemon IPC, keepalive sidecars, network APIs, audit logs, or other new relay
protocol fields. Those safety features can be planned later as extensions to
the session and daemon ownership models.

## Scope

- Add `rp2350-relay session` under the existing direct serial CLI.
- Support direct `--port <serial-port>`, exact `--serial <usb-serial>`, and
  interactive USB discovery and selection for session startup.
- Keep one-shot direct serial commands, for example
  `rp2350-relay --port COM7 info`, available and behavior-compatible.
- Reuse the existing direct `RelayClient`, validation rules, typed exceptions,
  one-based CLI channel arguments, and human output conventions where
  practical.
- Open one selected serial connection while connected and serialize all session
  relay operations through that connection.
- Poll the dummy firmware `heartbeat` command while connected.
- Keep session mode local to the host process.

## Deliverables

- Cross-platform session command under the existing `rp2350-relay` CLI.
- Host tests for discovery, session command parsing, command dispatch, typed
  error handling, one-connection session ownership, safe close behavior,
  reconnect behavior, and compatibility with existing one-shot direct commands.
- Operator-facing CLI and host-library documentation updates for session usage.
- Cross-platform session smoke-test procedure under `docs/testing/`.

## Acceptance Checks

Automated host checks:

```sh
scripts/test-host.sh
```

Expected results:

- Existing direct host library and CLI tests still pass.
- Session tests cover parser entry, USB discovery and selection, explicit
  `--port`, exact `--serial`, command dispatch, validation errors, transport
  errors, timeouts, protocol errors, and device-side errors without hardware.
- Session tests confirm commands in one connected session share one direct
  `RelayClient` connection and are processed sequentially.
- Session tests confirm normal `exit`, `quit`, and `disconnect` refuse to close
  when relay state is on, pulsing, or unknown, and that `--force` closes with a
  warning.
- Session tests confirm firmware `reboot` closes the current client and
  reconnects by known USB serial number when possible.
- Session tests confirm background heartbeat polling is silent on success,
  warns on failure, and does not by itself move the session into disconnected
  state.
- Compatibility tests confirm existing one-shot direct commands still behave
  unchanged.
- Tests do not require firmware communication-loss timeout or safety behavior.

Manual Windows smoke check, when hardware is available:

```powershell
rp2350-relay session
```

Manual Linux smoke check, when hardware is available:

```sh
rp2350-relay session
```

Within the session, run:

```text
status
pulse 1 100
off-all
exit
```

Expected results:

- The session lists matching relay controllers with port and USB serial number
  when started without an explicit selector.
- Session commands complete through one long-lived serial connection.
- The session keeps relay channel arguments board-label aligned: `1` is `CH1`
  and `6` is `CH6`.
- Normal exit succeeds only after relays are confirmed off.

## Phase 8b Handoff

After Phase 8a, Windows and Linux have a long-lived direct manual session
workflow. Phase 8b remains the Linux production daemon plan and must not depend
on session internals beyond shared direct host library behavior.
