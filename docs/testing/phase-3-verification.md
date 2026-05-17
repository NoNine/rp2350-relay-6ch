# Phase 3 Verification

Date: 2026-05-17
Hardware: Not used
Firmware commit: Not committed
Interfaces: `native_sim`, build wrapper, smoke checklist script
Result: PASS

## Commands Run

- `west build -s firmware/tests/relay_mgmt -b native_sim -d build/firmware-tests/relay-mgmt`
- `build/firmware-tests/relay-mgmt/zephyr/zephyr.exe`
- `scripts/build-firmware.sh`
- `west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay`
- `build/firmware-tests/relay/zephyr/zephyr.exe`
- `scripts/test-host.sh`
- `scripts/smoke-hardware.sh`

## Results

- Relay management group test build passed on `native_sim`.
- Relay management group tests passed with 12 passed, 0 failed, and 0 skipped.
- Tests covered MCUmgr group registration, info, get, set, set-all, pulse,
  off-all, status counters, malformed CBOR on field-based and empty-request
  commands, missing fields, invalid channels, invalid pulse duration, and busy
  pulse behavior.
- Firmware build for `rpi_pico2/rp2350a/m33/w` passed.
- Existing relay unit tests passed on `native_sim` with 13 passed, 0 failed,
  and 0 skipped.
- Host package test passed with 1 passed, 0 failed, and 0 skipped.
- `scripts/smoke-hardware.sh` printed the Phase 3 manual hardware checklist and
  exited successfully.

## Notes

- Hardware was not used for automated verification in this run.
- The smoke checklist remains manual and does not switch relays itself.
- No automated test left a relay on; test teardown forced all relays off.
