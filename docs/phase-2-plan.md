# Phase 2 Plan: Pulse Handling And Relay Safety Semantics

## Summary

Phase 2 completes relay behavior that affects safety and concurrency before
USB RPC or host tooling expose relay commands. The existing Phase 1 relay
module already validates channels, tracks state, sets individual relays, sets
all relays, and forces all relays off. Phase 2 adds bounded pulse handling on
top of that foundation.

Pulse requests must turn the selected relay on immediately, return it off when
the requested duration expires, and reject overlapping pulse requests for the
same relay. Relay state must remain queryable during and after each pulse, and
all tests and manual procedures must leave every relay off during teardown.

## Implementation

- Define supported pulse duration bounds in firmware and protocol
  documentation.
- Add a relay pulse API that:
  - validates relay indexes
  - validates pulse duration bounds
  - turns the requested relay on immediately
  - turns the relay off when the duration expires
  - rejects overlapping pulse requests for a busy relay
- Track per-relay pulse activity so direct state queries remain valid during
  and after a pulse.
- Ensure `relay_off_all()` cancels active pulses and leaves all relays off.
- Add teardown-safe test coverage that forces all relays off before and after
  pulse tests.
- Update the relay smoke-test procedure with manual pulse checks and an
  explicit all-off teardown step.

The optional session timeout foundation remains out of scope unless it can be
added without complicating v1 pulse behavior.

## Acceptance Checks

Run:

```sh
scripts/build-firmware.sh
west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay
build/firmware-tests/relay/zephyr/zephyr.exe
scripts/smoke-hardware.sh
```

Expected results:

- Firmware builds for the configured RP2350 development target.
- Relay unit tests pass on `native_sim`.
- Unit tests cover valid pulse behavior, duration bounds, invalid channel
  rejection, busy relay rejection, and final off state.
- Hardware smoke-test notes include a short pulse check for each relay and an
  all-off teardown step.
- No automated or manual test leaves a relay on as a side effect.

Recorded verification:

- Save Phase 2 verification results in
  `docs/testing/phase-2-verification.md` before marking the phase complete.

## Dependencies

- Phase 1 complete and verified.
- Relay GPIO mapping remains `CH1` through `CH6` on GPIO26 through GPIO31.
- Relay outputs remain active high unless hardware testing proves otherwise.

## Deliverables

- Pulse API and implementation in the relay firmware module.
- Firmware relay tests for pulse behavior and teardown safety.
- Documented pulse duration bounds in `docs/protocol/relay-management.md`.
- Updated relay smoke-test procedure in `docs/testing/relay-smoke-test.md`.
