# Phase 7 Verification

Date: 2026-05-23
Hardware: Raspberry Pi Pico 2 W development fixture
Firmware commit: d8580f2
Interfaces: USB CDC ACM SMP on `COM19`
Fixture: relays on `GP2` through `GP7`, RGB LED data on `GP8`, buzzer PWM on
`GP9`
Result: PASS

## Commands Run

- `west build -s firmware/tests/indicator -b native_sim -d build/firmware-tests/indicator-dev-buzzer`
- `build/firmware-tests/indicator-dev-buzzer/zephyr/zephyr.exe`
- `TARGET=pico2w RELAY_OVERLAY=firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay scripts/build-firmware.sh --pristine`
- `west flash -d build/firmware-pico2w`
- `rp2350-relay --port COM19 info`
- `rp2350-relay --port COM19 status`
- `rp2350-relay --port COM19 set 1 on`
- `rp2350-relay --port COM19 get 1`
- `rp2350-relay --port COM19 set 1 off`
- `rp2350-relay --port COM19 pulse 1 100`
- `python tools/usb_rpc_smoke.py --port COM19 invalid-channel`
- `python tools/usb_rpc_smoke.py --port COM19 invalid-pulse`
- `rp2350-relay --port COM19 pulse 1 1000`
- `rp2350-relay --port COM19 pulse 1 100`
- `rp2350-relay --port COM19 set-all 0x21`
- `rp2350-relay --port COM19 smoke`
- `rp2350-relay --port COM19 reboot`
- `TARGET=pico2w RELAY_OVERLAY=firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay scripts/build-firmware.sh`
- `west flash -d build/firmware-pico2w`
- `rp2350-relay --port COM19 reboot`
- `rp2350-relay --port COM19 info`
- `rp2350-relay --port COM19 status`
- `rp2350-relay --port COM19 off-all`

## Results

- Native indicator test passed: `8 passed`, `1 skipped`.
- Pico 2 W buzzer-enabled firmware build passed.
- Generated Pico 2 W firmware config included
  `CONFIG_RP2350_RELAY_6CH_INDICATORS=y` and
  `CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK=y`.
- Hardware Sections 0 through 4 were reported PASS by the operator.
- Firmware invalid-command checks were reported PASS by the operator using
  `tools/usb_rpc_smoke.py`.
- Busy pulse rejection was reported PASS by the operator.
- Multi-relay active indication was reported PASS by the operator using
  `set-all 0x21`.
- Full CLI smoke across all six relay channels was reported PASS by the
  operator.
- Initial reboot-pending hardware check produced only one short beep, exposing
  that the firmware reboot delay was too short for the documented three-beep
  pattern.
- After increasing the reboot-pending delay and rebuilding/flashing Pico 2 W
  firmware, the reboot-pending hardware check was reported PASS by the
  operator.
- Final teardown was reported PASS by the operator.
- Boot/ready, accepted command, single relay-active, multi-relay active, pulse,
  invalid-command, busy, reboot-pending, all-channel smoke, and teardown
  status-indicator paths passed.
- Buzzer feedback was bounded for accepted, invalid/rejected, busy, and
  reboot-pending paths.
- Final `off-all` left all relays off and cleared relay-active indication.

## Coverage

- Verified on hardware: boot/ready, accepted command, single relay-active,
  multi-relay active, pulse, invalid command, busy rejection, reboot pending,
  all-channel smoke, and final teardown.
- Not hardware-forced: firmware fault red blink, repeating fault chirp, and
  indicator device failure.
- Not Phase 7 scope: firmware upload, test-image, confirmation, rollback, and
  update-specific indications.
- Not run by request scope: Pico 2, Waveshare, quiet-mode, and release-style
  no-buzzer hardware builds.

## Hardware

- Hardware testing used only the Pico 2 W development fixture.
- Pico 2, Waveshare, quiet-mode, and release-style no-buzzer hardware checks
  were not run during this verification pass.
- No hazardous relay loads were reported connected during hardware testing.

## Notes

- Hardware results were reported by the human operator.
- The working tree included uncommitted buzzer-default documentation and
  firmware configuration changes after commit `d8580f2`; those changes affected
  the tested Pico 2 W firmware configuration.
- The working tree also included a reboot-pending delay fix in
  `firmware/src/relay_mgmt.c`; the final reboot-pending hardware result was
  reported after rebuilding and flashing that firmware.
- The Pico 2 W target build showed the existing Zephyr
  `hwinfo_rpi_pico.c` unused-variable warning.
- The report records only commands that were run or reported as run during
  Phase 7 verification.

## Boot-Ready Beep Follow-Up

Date: 2026-05-23
Hardware: Raspberry Pi Pico 2 W development fixture
Firmware basis: commit `7a324b7` plus working-tree changes for boot-ready
one-long-beep behavior and 20% RGB brightness dimming
Interfaces: USB CDC ACM SMP on `COM19`
Result: PASS

Commands and checks run or operator-reported:

- `west build -s firmware/tests/indicator -b native_sim -d build/firmware-tests/indicator`
- `build/firmware-tests/indicator/zephyr/zephyr.exe`
- `west build -s firmware/tests/indicator -b native_sim -d build/firmware-tests/indicator-buzzer -- -DCONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK=y`
- `build/firmware-tests/indicator-buzzer/zephyr/zephyr.exe`
- `TARGET=pico2w RELAY_OVERLAY=firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay scripts/build-firmware.sh`
- Flash or copy `build/firmware-pico2w/zephyr/zephyr.uf2` to the Pico 2 W
- `rp2350-relay --port COM19 info`
- `rp2350-relay --port COM19 status`
- `rp2350-relay --port COM19 set 1 on`
- `rp2350-relay --port COM19 get 1`
- `rp2350-relay --port COM19 set 1 off`
- `rp2350-relay --port COM19 get 1`
- `python tools/usb_rpc_smoke.py --port COM19 invalid-channel`
- `rp2350-relay --port COM19 reboot`
- `rp2350-relay --port COM19 status`
- `rp2350-relay --port COM19 get`
- `rp2350-relay --port COM19 off-all`
- `rp2350-relay --port COM19 get`

Results:

- Default native indicator test passed: `9 passed`, `2 skipped`.
- Buzzer-enabled native indicator test passed: `11 passed`.
- Pico 2 W buzzer-enabled firmware build passed and generated
  `build/firmware-pico2w/zephyr/zephyr.uf2`.
- Hardware Section 0 passed: relays stayed off during boot, RGB reached
  dim-green ready, and the buzzer produced one long boot-ready beep with no
  repeated alarm afterward.
- Hardware Section 1 passed: `info` and `status` responded normally and no
  extra boot-ready long beep repeated during RPC reachability checks.
- Hardware Section 2 passed: accepted `set` commands still produced one short
  beep, CH1 turned on and off as commanded, and `get 1` matched the expected
  relay state.
- Hardware Section 3 passed: invalid-channel rejection still produced two
  short beeps, not the boot-ready long beep, and `status` remained responsive.
- Hardware Section 4 passed: controlled reboot produced three short
  reboot-pending beeps before reboot, then one long boot-ready beep after the
  board returned; `get` reported `state: 0x00`, `on: none`, and
  `pulsing: none`.
- Hardware Section 5 passed: final `off-all` and `get` left all relays off,
  RGB ready/idle, and no repeated buzzer alarm active.
