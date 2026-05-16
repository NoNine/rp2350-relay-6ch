# Phase 1 Plan: Board Bring-Up And Relay GPIO Control

## Summary

Phase 1 adds safe direct firmware control for the six relay outputs before USB
RPC or host tooling are introduced. The default build target remains
`rpi_pico2/rp2350a/m33/w` until a dedicated RP2350-Relay-6CH board definition
is added, and the app overlay models `CH1` through `CH6` as active-high GPIO
outputs on GPIO26 through GPIO31.

All relay outputs must initialize off on boot and return off during test and
manual smoke-test teardown.

## Implementation

- Add relay GPIO definitions in the firmware devicetree overlay.
- Add a relay module with channel validation, state reads, set one, set all,
  and off-all operations.
- Initialize all relays off from `main()` before the application reports ready.
- Add optional Zephyr shell commands for manual bring-up:
  - `relay get [channel]`
  - `relay set <channel> <on|off>`
  - `relay all <on|off>`
  - `relay off`
- Add firmware unit tests for default-off state, state transitions, all-off,
  set-all, and invalid channel rejection.
- Update the hardware smoke-test entry point so hardware validation has a
  repeatable checklist and an explicit all-off teardown step.

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
- Hardware smoke-test script prints the relay validation procedure and exits
  with success as a checklist helper. It does not switch relays or complete
  hardware validation by itself.

Recorded verification:

- See `docs/testing/phase-1-verification.md` for the 2026-05-16 hardware smoke
  test result.

## Assumptions

- Relay outputs are active high unless hardware testing proves otherwise.
- GPIO26 through GPIO31 are reserved for `CH1` through `CH6`.
- Pulse behavior remains Phase 2 scope; Phase 1 only provides the direct
  control foundation.
