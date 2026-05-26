# Phase 8b Plan: Host Daemon Mode

## Summary

Phase 8b adds daemon mode as the production Linux host-control workflow. The
daemon is independently useful before firmware communication-loss safety: it
owns the USB CDC serial device, serializes relay commands, handles reconnects,
and exposes a local API for short-lived CLI and Python clients.

Each daemon instance owns one relay controller. Multiple relay controllers on
one Linux host use multiple daemon instances with distinct explicit sockets.

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
- Select the daemon-owned controller by exactly one selector: `--port` or
  exact USB `--serial`.
- Use newline-delimited JSON over an explicit same-user Unix domain socket.
- Provide a `systemd --user` unit example for production operation.

## Deliverables

- Host daemon, daemon client, and daemon-client CLI under the host tooling
  package.
- Updated packaging entry points for `rp2350-relayd` and `rp2350-relayctl`.
- Implementer-facing daemon contract in [Host daemon mode](host-daemon-mode.md).
- Operator-facing CLI and host-library documentation updates.
- Daemon smoke-test procedure under `docs/testing/`.
- Examples-only static instance guidance using explicit serial numbers and
  socket paths; no named-instance CLI, environment-file parsing, or TOML
  configuration in Phase 8b.

## Acceptance Checks

Automated host checks:

```sh
scripts/test-host.sh
```

Expected results:

- Existing direct host library and CLI tests still pass.
- Daemon tests cover NDJSON parsing, malformed requests, response formatting,
  typed error mapping, persistent connections, frame limits, and command
  serialization.
- Client tests cover Python daemon client calls and `rp2350-relayctl` command
  behavior without hardware.
- Selector tests cover explicit `--port`, exact `--serial`, duplicate serial
  rejection, missing serial with `--wait-device`, and serial rediscovery after
  USB renumbering.
- Startup tests confirm the daemon queries `info` then `status` without sending
  `off-all`.
- Socket tests cover required explicit socket paths, permissions,
  already-running detection, path-in-use detection, and stale socket cleanup.
- `daemon-status` tests confirm daemon state is available while disconnected
  and does not include relay-state cache fields.
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

## Phase 9 Handoff

After Phase 8a and Phase 8b, firmware upgrade work can assume long-lived
manual session control and Linux production daemon control exist, but it must
not require daemon mode unless the Phase 9 plan explicitly chooses that
dependency for operator workflows.
