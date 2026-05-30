# Communication-Loss Verification

Date: 2026-05-30
Hardware: RP2350 relay controller on COM7 reported by operator
Commit: c2c7f3e
Interfaces: USB CDC ACM SMP through direct one-shot `rp2350-relay` CLI and
session mode
Result: PASS

## Commands Run

Operator-reported manual hardware checks:

- `rp2350-relay --port COM7 info`
- `rp2350-relay --port COM7 off-all`
- `rp2350-relay --port COM7 status`
- `rp2350-relay --port COM7 set 1 on`
- Waited at least 7 seconds without issuing another command.
- `rp2350-relay --port COM7 status`
- `rp2350-relay --port COM7 pulse 1 10000`
- Waited at least 7 seconds without issuing another command.
- `rp2350-relay --port COM7 status`
- `rp2350-relay --port COM7 set 1 on`
- `rp2350-relay --port COM7 off-all`
- Waited at least 7 seconds.
- `rp2350-relay --port COM7 status`
- `rp2350-relay --port COM7 off-all`
- `rp2350-relay --port COM7 status`
- `rp2350-relay --port COM7 session`
- Session command: `off-all`
- Session command: `status`
- Session command: `set 1 on`
- Waited at least 7 seconds while session heartbeat remained active.
- Session command: `status`
- Session command: `disconnect --force`
- Waited at least 7 seconds with the session disconnected.
- Session command: `connect --port COM7`
- Session command: `status`
- Session command: `off-all`
- Session command: `status`
- Session command: `exit`

## Results

- Firmware reported communication-loss policy `energized-only`.
- Firmware reported communication-loss timeout `comm_loss_timeout_ms=5000`.
- One-shot `set 1 on` armed the energized-only timeout and CH1 turned off
  after roughly 5 seconds without heartbeat renewal.
- Long `pulse 1 10000` was cancelled by the communication-loss timeout before
  the full 10-second pulse duration.
- `off-all` disarmed the energized-only timeout and all relays remained off
  after the wait.
- Session heartbeat renewal kept CH1 energized past the 5-second
  communication-loss timeout while the session remained connected.
- After `disconnect --force`, CH1 turned off by itself after roughly 5 seconds
  without heartbeat renewal.
- Final teardown was reported PASS with every relay off and no channel pulsing.

## Coverage

- Verified by operator hardware smoke: standard communication-loss policy,
  one-shot relay-on timeout, long-pulse cancellation, `off-all` disarm behavior,
  session heartbeat renewal, timeout after forced session disconnect, and final
  all-off teardown.
- Not changed by this verification: firmware protocol fields, host CLI
  behavior, daemon behavior, session behavior, network control, audit logs, or
  relay persistent-state behavior.

## Hardware

- Hardware smoke testing was performed by the human operator with an RP2350
  relay controller attached as COM7.
- Hazardous relay-side loads were disconnected during hardware testing.

## Safety Notes

- The one-shot direct CLI does not own a heartbeat loop, so direct
  communication-loss timeout was verified by waiting after relay-control
  commands instead of unplugging USB.
- Session heartbeat ownership was verified separately by keeping CH1 on past
  the timeout while connected, then using `disconnect --force` to stop
  heartbeat renewal without sending `off-all`.
- No relay was reported left energized after verification.

## Notes

- Hardware results were reported by the human operator.
- The report records only commands that were reported as run during
  communication-loss verification.

## Addendum: Boardfarm Always-On-Owner Verification

Date: 2026-05-31
Hardware: RP2350 relay controller on COM7 reported by operator
Commit: 178342d
Product build: `rp2350_relay_6ch-boardfarm-userdebug`
Interfaces: USB CDC ACM SMP through direct one-shot `rp2350-relay` CLI and
session mode
Result: PASS

### Commands Run

Local product build:

- `scripts/build.sh --lunch rp2350_relay_6ch-boardfarm-userdebug`

Operator-reported manual hardware checks:

- Flashed `dist/rp2350_relay_6ch-0.8.8-waveshare.uf2`.
- Installed the matching host wheel
  `dist/rp2350_relay_6ch-0.8.8-py3-none-any.whl`.
- `rp2350-relay --port COM7 info`
- `rp2350-relay --port COM7 off-all`
- `rp2350-relay --port COM7 status`
- `rp2350-relay --port COM7 set 1 on`
- Waited at least 7 seconds without issuing another command.
- `rp2350-relay --port COM7 status`
- `rp2350-relay --port COM7 pulse 1 10000`
- Waited at least 7 seconds without issuing another command.
- `rp2350-relay --port COM7 status`
- `rp2350-relay --port COM7 session`
- Session command: `off-all`
- Session command: `status`
- Session command: `set 1 on`
- Waited at least 7 seconds while session heartbeat remained active.
- Session command: `status`
- Session command: `disconnect --force`
- Waited at least 7 seconds with the session disconnected.
- Session command: `connect --port COM7`
- Session command: `status`
- Session command: `off-all`
- Session command: `status`
- Session command: `disconnect --force`
- Waited at least 7 seconds while disconnected with all relays off.
- Session command: `connect --port COM7`
- Session command: `status`
- Session command: `off-all`
- Session command: `status`
- Session command: `exit`

### Build Results

- Product build passed and produced
  `dist/rp2350_relay_6ch-0.8.8-py3-none-any.whl`.
- Product build passed and produced
  `dist/rp2350_relay_6ch-0.8.8-waveshare.uf2`.
- Product build passed and produced
  `dist/rp2350_relay_6ch-0.8.8-pico2.uf2`.
- Product manifest confirmed boardfarm firmware fragments:
  `firmware/profiles/always_on_owner.conf` and
  `firmware/profiles/display_rotated_180.conf`.
- Waveshare and Pico 2 firmware builds both merged
  `firmware/profiles/always_on_owner.conf`.

### Manual Results

- Firmware reported communication-loss policy `always-on-owner`.
- Firmware reported communication-loss timeout `comm_loss_timeout_ms=5000`.
- Baseline `off-all` and `status` reported all relays off with no pulsing
  channels.
- One-shot `set 1 on` timed out to all-off after roughly 5 seconds without
  heartbeat renewal.
- Long `pulse 1 10000` was cancelled by the communication-loss timeout before
  the full 10-second pulse duration.
- Session heartbeat renewal kept CH1 energized past the 5-second
  communication-loss timeout while the session remained connected.
- After `disconnect --force`, CH1 turned off by itself after roughly 5 seconds
  without heartbeat renewal.
- Idle all-off always-on-owner timeout left the board reachable with no special
  fault or reboot expected.
- Final teardown was reported PASS with every relay off and no channel pulsing.

### Coverage

- Verified by product build: boardfarm release-config resolution, host wheel
  build, Waveshare UF2 build, Pico 2 UF2 build, and artifact presence.
- Verified by operator hardware smoke: boardfarm always-on-owner policy,
  one-shot relay-on timeout, long-pulse cancellation, session heartbeat
  renewal, timeout after forced session disconnect, idle all-off timeout
  behavior, and final all-off teardown.
- Not changed by this verification: firmware protocol fields, host CLI
  behavior, daemon behavior, network control, audit logs, relay
  persistent-state behavior, or release version metadata.

### Hardware

- Hardware smoke testing was performed by the human operator with an RP2350
  relay controller attached as COM7.
- Hazardous relay-side loads were disconnected during hardware testing.

### Safety Notes

- The one-shot direct CLI does not own a heartbeat loop, so direct
  communication-loss timeout was verified by waiting after relay-control
  commands instead of unplugging USB.
- Session heartbeat ownership was verified by keeping CH1 on past the timeout
  while connected, then using `disconnect --force` to stop heartbeat renewal
  without sending `off-all`.
- The idle all-off always-on-owner timeout was verified as reachable and
  non-faulting from the operator perspective.
- No relay was reported left energized after verification.

### Notes

- Hardware results were reported by the human operator.
- The operator's final `pas` response is recorded as PASS for final teardown.
- The report records only commands that were run locally or reported as run
  during boardfarm communication-loss verification.
