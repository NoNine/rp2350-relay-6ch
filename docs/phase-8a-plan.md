# Phase 8a Plan: Windows Session Mode

## Summary

Phase 8a adds session mode as the main Windows PC operator workflow. The
session is independently useful before firmware communication-loss safety: it
owns the Windows COM port while open, serializes relay commands through the
existing direct host client path, and gives an operator one long-lived control
surface instead of repeated short serial opens.

This phase complements Phase 8b Linux daemon mode. Windows production operation
uses session ownership; Linux production operation uses daemon ownership.

This phase must not add firmware heartbeat commands, communication-loss
timeouts, firmware reboot-on-silence behavior, or new relay protocol fields.
Those safety features can be planned later as an extension to the session and
daemon ownership models.

## Scope

- Add `rp2350-relay --port COM7 session` as the Windows PC main operator
  workflow.
- Keep one-shot direct serial commands, for example
  `rp2350-relay --port COM7 info`, available for diagnostics and simple checks.
- Reuse the existing direct `RelayClient` connection, validation rules, typed
  exceptions, channel numbering, and command output conventions where
  practical.
- Open one configured Windows COM port for the session lifetime and serialize
  all relay operations through that connection.
- Keep session mode local to the host process. Do not add daemon IPC, a
  keepalive sidecar, network access, audit logs, or a separate session entry
  point.
- Defer final prompt grammar, command history, scripting behavior, exit relay
  policy, and future heartbeat behavior to the implementation phase.

## Deliverables

- Windows session command under the existing `rp2350-relay` CLI.
- Host tests for session command parsing, command dispatch, typed error
  handling, one-connection session ownership, and compatibility with existing
  one-shot direct commands.
- Operator-facing CLI documentation updates for Windows session usage.
- Windows session smoke-test procedure under `docs/testing/`.

## Acceptance Checks

Automated host checks:

```sh
scripts/test-host.sh
```

Expected results:

- Existing direct host library and CLI tests still pass.
- Session tests cover parser entry, missing-port handling, command dispatch,
  validation errors, transport errors, timeouts, protocol errors, and
  device-side errors without hardware.
- Session tests confirm commands in one session share one direct `RelayClient`
  connection and are processed sequentially.
- Compatibility tests confirm existing one-shot `rp2350-relay --port COM7`
  commands still behave unchanged.
- Tests do not require firmware heartbeat or new protocol fields.

Manual Windows smoke check, when hardware is available:

```powershell
rp2350-relay --port COM7 session
```

Within the session, run:

```text
status
pulse 1 100
off-all
exit
```

Expected results:

- The session starts on the assigned Windows COM port.
- Session commands complete through the same long-lived serial connection.
- The session keeps relay channel arguments board-label aligned: `1` is `CH1`
  and `6` is `CH6`.
- All relays are off after the manual `off-all` command.

## Phase 8b Handoff

After Phase 8a, Windows has a long-lived host-control workflow. Phase 8b keeps
the Linux daemon plan separate and must not depend on Windows session mode.
