# Phase 8b Verification

Date: 2026-05-26
Hardware: RP2350 relay controller on `/dev/ttyACM0` reported by operator
Commit: a70a7e3 plus working-tree daemon heartbeat and CLI UX fixes
Interfaces: USB CDC ACM SMP through `rp2350-relayd` and Unix socket IPC
Result: PASS

## Commands Run

- `scripts/test-host.sh`
- `${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/python -m build --wheel`
- Wheel metadata and content inspection for
  `dist/rp2350_relay_6ch-0.8.0-py3-none-any.whl`
- `sha256sum dist/rp2350_relay_6ch-0.8.0-py3-none-any.whl`

Operator-reported manual daemon hardware checks:

- Installed the test wheel
  `dist/rp2350_relay_6ch-0.8.0-py3-none-any.whl`
- Started `rp2350-relayd` with `/dev/ttyACM0` and a Unix socket
- Ran `rp2350-relayctl --socket "$SOCKET" daemon-status`
- Ran `rp2350-relayctl --socket "$SOCKET" status`
- Waited while idle and ran `status` again to confirm heartbeat activity
  through firmware counters
- Disconnected the device and confirmed daemon disconnected state
- Reconnected the device and confirmed daemon recovery to `connected: true`
- Ran `rp2350-relayctl --socket "$SOCKET" off-all`

## Results

- Host test gate passed: `126 passed in 0.85s`.
- Wheel build passed and produced
  `dist/rp2350_relay_6ch-0.8.0-py3-none-any.whl`.
- Wheel metadata reported package name `rp2350-relay-6ch` and version `0.8.0`.
- Wheel entry points included `rp2350-relay`, `rp2350-relayctl`, and
  `rp2350-relayd`.
- Wheel content inspection confirmed the daemon heartbeat loop and
  human-readable `daemon-status` formatter were present.
- Wheel SHA256:
  `8e518935063dd0d3951b5a7e99ddf11a223fc5b571d8279d99f0cd0720a436e9`.
- Manual daemon hardware smoke testing was reported PASS by the operator.
- Final `off-all` teardown was reported PASS by the operator.

## Operator-Found Issues

During manual daemon-mode verification, the operator found two issues:

- `rp2350-relayctl daemon-status` defaulted to JSON-shaped output even though
  `--output human` is the CLI default. This made daemon status hard to scan
  during normal operator use. The formatter was updated so default
  `daemon-status` output is aligned human-readable key/value text, while
  `--output json` remains unchanged.
- `rp2350-relayd` did not issue regular heartbeat commands while idle. The
  daemon now polls the existing relay-management `heartbeat` command every
  5 seconds while connected. Heartbeat timeout or transport failure closes only
  the daemon's current serial client, marks device state disconnected, and lets
  the reconnect loop recover. It does not close daemon IPC clients or run
  `off-all`.

Both fixes were covered by automated host tests and were reported PASS in
manual hardware verification.

## Coverage

- Verified by automated tests: daemon NDJSON handling, daemon client behavior,
  `rp2350-relayctl` command behavior, human and JSON daemon-status output,
  daemon heartbeat success and failure behavior, heartbeat serialization,
  reconnect behavior, shutdown behavior, and compatibility with existing host
  CLI and session tests.
- Verified by wheel inspection: daemon and daemon-client entry points,
  daemon heartbeat implementation, and human daemon-status formatter inclusion.
- Verified by operator hardware smoke: daemon startup on `/dev/ttyACM0`,
  human-readable `daemon-status`, relay `status`, idle heartbeat activity
  through firmware counters, heartbeat-failure disconnected state, reconnect
  recovery, and final all-off teardown.
- Not changed in this phase verification: firmware protocol fields, firmware
  communication-loss safety actions, network control, audit logs, and direct
  session heartbeat behavior.

## Hardware

- Hardware smoke testing was performed by the human operator with an RP2350
  relay controller attached as `/dev/ttyACM0`.
- Hazardous relay-side loads were not reported connected during hardware
  testing.

## Safety Notes

- Daemon heartbeat failure does not call `off-all`; it marks the daemon device
  state disconnected and lets reconnect recover.
- Clean daemon shutdown and explicit operator teardown still use best-effort
  `off-all`.
- No relay was reported left energized after verification.

## Notes

- Hardware results were reported by the human operator.
- The report records only commands that were run locally or reported as run
  during Phase 8b verification.
