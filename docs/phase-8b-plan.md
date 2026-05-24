# Phase 8b Plan: Host Daemon Mode

## Summary

Phase 8b adds daemon mode as the production Linux host-control workflow. The
daemon is independently useful before firmware communication-loss safety: it
owns the USB CDC serial device, serializes relay commands, handles reconnects,
and exposes a local API for short-lived CLI and Python clients.

Phase 8a covers cross-platform direct manual session mode separately. Linux
production automation uses daemon ownership.

Use [Host daemon mode](host-daemon-mode.md) as the decision-complete implementation
contract for daemon lifecycle, local IPC, client APIs, reconnect policy,
systemd user operation, and tests.

This phase must not add firmware heartbeat commands, communication-loss
timeouts, firmware reboot-on-silence behavior, or new relay protocol fields.
Those safety features can be planned later as an extension to daemon mode.

## Scope

- Add Linux-only daemon mode for production host operation.
- Keep direct manual operation on the Phase 8a session path and direct
  diagnostics on the existing serial CLI path.
- Keep `rp2350-relay` as the direct serial diagnostic CLI.
- Add `rp2350-relayd` as the foreground-capable daemon process.
- Add `rp2350-relayctl` as the short-lived daemon-client CLI.
- Add `RelayDaemonClient` beside the existing direct `RelayClient`.
- Use newline-delimited JSON over a same-user Unix domain socket.
- Provide a `systemd --user` unit for production operation.

## Deliverables

- Host daemon, daemon client, and daemon-client CLI under the host tooling
  package.
- Updated packaging entry points for `rp2350-relayd` and `rp2350-relayctl`.
- Implementer-facing daemon contract in [Host daemon mode](host-daemon-mode.md).
- Operator-facing CLI and host-library documentation updates.
- Daemon smoke-test procedure under `docs/testing/`.

## Acceptance Checks

Automated host checks:

```sh
scripts/test-host.sh
```

Expected results:

- Existing direct host library and CLI tests still pass.
- Daemon tests cover NDJSON parsing, malformed requests, response formatting,
  typed error mapping, and command serialization.
- Client tests cover Python daemon client calls and `rp2350-relayctl` command
  behavior without hardware.
- Startup tests confirm the daemon queries state without sending `off-all`.
- No-client tests confirm relay state is not changed when the last client
  disconnects.
- Shutdown tests confirm a best-effort `off-all` is attempted.
- Reconnect tests simulate serial failure and recovery without concurrent
  command races.
- CLI tests confirm `rp2350-relayctl` rejects direct serial options.
- Compatibility tests confirm existing direct `RelayClient` and `rp2350-relay`
  behavior still passes unchanged.

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

## Phase 9 Handoff

After Phase 8a and Phase 8b, firmware upgrade work can assume long-lived
manual session control and Linux production daemon control exist, but it must
not require daemon mode unless the Phase 9 plan explicitly chooses that
dependency for operator workflows.
