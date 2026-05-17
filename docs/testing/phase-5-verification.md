# Phase 5 Verification

Date: 2026-05-17
Hardware: Waveshare RP2350-Relay-6CH
Firmware commit: 6bff119
Interfaces: Temporary UART1 SMP route on GPIO4/GPIO5
Result: PASS

## Commands Run

- `scripts/build-firmware.sh`
- `scripts/test-host.sh`
- `${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay`
- `build/firmware-tests/relay/zephyr/zephyr.exe`
- `${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/west build -s firmware/tests/relay_mgmt -b native_sim -d build/firmware-tests/relay-mgmt`
- `build/firmware-tests/relay-mgmt/zephyr/zephyr.exe`
- `${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/python tools/host_library_hardware_smoke.py --help`
- `${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/python tools/host_library_hardware_smoke.py --port <serial-port>`

## Results

- Firmware built successfully with the temporary UART1 SMP devicetree route.
- Host tests passed: `14 passed`.
- Firmware relay native test passed: `13 passed`.
- Firmware relay management native test passed: `12 passed`.
- The host-library hardware smoke helper printed its help successfully after it
  was added.
- The host-library hardware smoke helper completed successfully against
  hardware.
- Python host-library `info`, `status`, `get`, `set`, `set_all`, `pulse`, and
  `off_all` operations completed through the temporary UART1 SMP route.
- Invalid channel and invalid pulse requests returned structured relay group
  errors with group `64` and rc `2`.
- Direct `west build` commands without the Zephyr virtual environment on `PATH`
  failed with `west: command not found`; the same builds passed when run through
  the Zephyr workspace virtual environment.

## Hardware

- Hardware smoke testing through the Python host library passed.
- An attempted run of the earlier hardware smoke helper reached the invalid
  channel step, then failed locally with `channel must be 0..5` before the P2
  fix was applied.
- A successful post-fix hardware run was reported after the smoke helper was
  updated to send invalid requests through the lower-level request path.

## Notes

- `firmware/app.overlay` has an uncommitted temporary change that routes
  `zephyr,uart-mcumgr` to UART1 on GPIO4/GPIO5 for bench verification.
- `tools/host_library_hardware_smoke.py` was used as the Python host-library
  hardware smoke helper for the passing manual check.
- No hazardous relay loads were reported connected during the attempted manual
  hardware check.
- The attempted pre-fix manual hardware check printed a final `off_all` response
  with `state` 0 and `pulsing` 0.
- The passing hardware smoke helper ends with `off_all()` and reported no relay
  left energized.
