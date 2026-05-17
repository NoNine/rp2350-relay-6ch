# Phase 6 Verification

Date: 2026-05-17
Hardware: Waveshare RP2350-Relay-6CH
Firmware commit: a344c4a
Interfaces: Host CLI through configured SMP serial route
Result: PASS

## Commands Run

- `scripts/test-host.sh`
- `${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/python tools/rp2350_relay_cli.py --help`
- `${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/python tools/rp2350_relay_cli.py --port <serial-port> smoke`

## Results

- Host tests passed: `25 passed`.
- CLI help printed successfully through the Zephyr workspace virtual
  environment.
- The Phase 6 CLI hardware smoke command was reported PASS by the hardware
  operator.
- The CLI smoke command queried controller information and status.
- The CLI smoke command pulsed `CH1` through `CH6`.
- The CLI smoke command attempted final `off-all` teardown and left all relay
  states off.

## Hardware

- Hardware smoke testing through the Phase 6 CLI passed.
- The exact serial port used for the passing hardware check was not provided.
- No hazardous relay loads were reported connected during the manual hardware
  check.

## Notes

- `firmware/app.overlay` has an uncommitted temporary change that was not part
  of commit `a344c4a`.
- The verification report records only commands that were run or reported as
  run during Phase 6 verification.
