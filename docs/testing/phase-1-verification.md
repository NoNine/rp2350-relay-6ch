# Phase 1 Verification

Date: 2026-05-16
Hardware: Waveshare RP2350-Relay-6CH
Firmware commit: 94e3f00
Shell interface: UART0, 115200 8N1
Result: PASS

## Commands Run

- `scripts/build-firmware.sh`
- `scripts/test-host.sh`
- Manual UART0 relay smoke test from `docs/testing/relay-smoke-test.md`

## Results

- Firmware build passed.
- Host package test passed.
- UART0 Zephyr shell was reachable from the operator PC.
- `CH1` through `CH6` switched independently.
- `relay off` de-energized all relays during teardown.
- Final `relay get` showed all relays off.
- Relay polarity matched the documented active-high assumption.
- Reset and power-cycle default-off behavior was not separately recorded in
  this test pass.

## Notes

- USB CDC remained unused for shell access and is reserved for future host
  control protocol work.
- UART1 remained reserved for the isolated RS485 path.
- No hazardous relay loads were connected during bring-up.
