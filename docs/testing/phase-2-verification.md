# Phase 2 Verification

Date: 2026-05-17
Hardware: Waveshare RP2350-Relay-6CH
Firmware commit: b24e69b
Interfaces: `native_sim`, build wrapper, UART0 relay shell
Result: PASS

## Commands Run

- `scripts/build-firmware.sh`
- `west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay`
- `build/firmware-tests/relay/zephyr/zephyr.exe`
- `scripts/smoke-hardware.sh`

## Results

- Firmware build for `rpi_pico2/rp2350a/m33/w` passed; Ninja reported no work
  to do because the build was already up to date.
- Relay unit tests passed on `native_sim`.
- The relay suite reported 13 passed, 0 failed, and 0 skipped tests.
- Unit tests covered valid pulse behavior, duration bounds, invalid channel
  rejection, busy relay rejection, final off state, all-off pulse teardown, and
  direct-set pulse cancellation.
- `scripts/smoke-hardware.sh` printed the Phase 2 manual hardware checklist and
  exited successfully.
- Human operator reported PASS for the manual Phase 2 relay smoke test from
  `docs/testing/relay-smoke-test.md`.
- Manual smoke testing confirmed relay pulse checks passed on hardware.

## Notes

- Manual hardware results were reported by a human operator.
- Final teardown used the documented `relay off` and `relay get` procedure.
