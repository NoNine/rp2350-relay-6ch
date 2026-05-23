# Changelog

Release history for `rp2350-relay-6ch`.

This project keeps concise release history here and uses GitHub Releases for
release assets. The section shape is inspired by Adafruit CircuitPython release
notes: highlights, install/assets, verification, known limitations, and safety
notes.

## 0.7.0 - 2026-05-24

Local status indicator release for the current relay CLI and firmware line.

- Tag: `v0.7.0`
- Commit: the `v0.7.0` tag target.

### Highlights

- Added controller-level RGB LED and passive buzzer status indication.
- Added indicator priority handling for boot, ready, relay-active, command
  feedback, update/reboot, degraded, and fault conditions.
- Added Waveshare RP2350-Relay-6CH indicator GPIO definitions for the WS2812
  RGB LED and passive buzzer.
- Added a Pico 2 W relay development fixture overlay for indicator hardware
  verification.
- Added host status output for indicator availability.
- Recorded Phase 7 Pico 2 W indicator verification.

### Install / Assets

- Install the host CLI from the GitHub Release wheel:
  `rp2350_relay_6ch-0.7.0-py3-none-any.whl`.
- Flash one of the matching firmware artifacts from the same release:
  `rp2350_relay_6ch-0.7.0-waveshare.uf2` or
  `rp2350_relay_6ch-0.7.0-pico2.uf2`.
- The source distribution `rp2350_relay_6ch-0.7.0.tar.gz` is available as an
  additional artifact.

### Verification

- Clean host tests completed with `scripts/test-host.sh`.
- Clean relay firmware unit tests built and passed under `native_sim`.
- Clean relay-management firmware unit tests built and passed under
  `native_sim`.
- Clean indicator firmware unit tests built and passed under `native_sim`.
- Clean host package build completed with `python -m build`.
- Clean Waveshare firmware build completed with `scripts/build-firmware.sh
  --pristine`.
- Clean Pico 2 firmware build completed with `TARGET=pico2
  RELAY_OVERLAY=firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay
  scripts/build-firmware.sh --pristine`.

### Known Limitations

- MCUboot-compatible firmware upload, test-image, confirm-image, and rollback
  workflows remain planned but not implemented.
- Communication-loss safety and daemon mode remain planned but not implemented.
- Pico 2 and Pico 2 W DIY builds require explicit relay overlays and external
  relay driver hardware.
- RS485 and wireless host communication are not v1 control paths.
- Application-layer authentication is not implemented; this release assumes
  trusted local USB access.

### Safety Notes

- Keep hazardous relay-side loads disconnected during bring-up.
- Confirm all relays are off after flashing, reset, smoke tests, and teardown.
- Local indicators report controller state and commanded relay state only; they
  do not measure contact closure, load voltage, load current, or external
  equipment health.

## 0.6.0 - 2026-05-20

Release packaging and documentation update for the current relay CLI and
firmware line.

- Tag: `v0.6.0`
- Commit: the `v0.6.0` tag target.

### Highlights

- Added Pico 2 and Pico 2 W DIY relay target support through explicit six-relay
  devicetree overlays.
- Updated the host package and firmware build metadata to version `0.6.0`.
- Documented the required GitHub Release artifact set and shortened firmware
  artifact names to `waveshare`, `pico2`, and optional `pico2w` qualifiers.
- Clarified operator install and firmware flashing instructions for wheel and
  UF2 release artifacts.
- Added a deferred remaining-features review covering firmware upgrade,
  signing, recovery, release, and optional status-output gaps.

### Install / Assets

- Install the host CLI from the GitHub Release wheel:
  `rp2350_relay_6ch-0.6.0-py3-none-any.whl`.
- Flash one of the matching firmware artifacts from the same release:
  `rp2350_relay_6ch-0.6.0-waveshare.uf2` or
  `rp2350_relay_6ch-0.6.0-pico2.uf2`.
- The source distribution `rp2350_relay_6ch-0.6.0.tar.gz` is available as an
  additional artifact.

### Verification

- Clean host package build completed with:
  `${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv/bin/python -m build`.
- Clean Waveshare firmware build completed with `scripts/build-firmware.sh`.
- Clean Pico 2 firmware build completed with:
  `TARGET=pico2 RELAY_OVERLAY=firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay scripts/build-firmware.sh`.
- Release-named artifacts were created under `dist/`.
- Build output included a Zephyr `hwinfo_rpi_pico.c` unused-variable warning and
  a setuptools deprecation warning from environment configuration.

### Known Limitations

- MCUboot-compatible firmware upload, test-image, confirm-image, and rollback
  workflows remain planned but not implemented.
- Pico 2 and Pico 2 W DIY builds require explicit relay overlays and external
  relay driver hardware.
- RS485 and wireless host communication are not v1 control paths.
- Application-layer authentication is not implemented; this release assumes
  trusted local USB access.

### Safety Notes

- Keep hazardous relay-side loads disconnected during bring-up.
- Confirm all relays are off after flashing, reset, smoke tests, and teardown.
- DIY Pico targets must use relay-driver circuitry compatible with the selected
  GPIO mapping and relay loads.

## 0.1.0 - 2026-05-17

First packaged operator release for the Waveshare RP2350-Relay-6CH controller.

- Tag: `v0.1.0`
- Commit: `447edb3`

### Highlights

- Added safe six-channel relay control for `CH1` through `CH6`.
- Added default-off relay behavior for boot, reset, firmware restart, and test
  setup/teardown.
- Added the custom Zephyr MCUmgr/SMP relay management protocol over USB CDC.
- Added the Python RPC library and installable `rp2350-relay` CLI.
- Added CLI commands for info, build-info, get, set, set-all, pulse, off-all,
  status, reboot, JSON output, and hardware smoke testing.
- Added the Waveshare RP2350-Relay-6CH Zephyr board target and build-info
  command for release traceability.
- Packaged the host CLI as a Python wheel for operator installation without a
  firmware checkout.

### Install / Assets

- Install the host CLI from the GitHub Release wheel:
  `rp2350_relay_6ch-0.1.0-py3-none-any.whl`.
- Use the matching firmware artifact from the same GitHub Release when flashing
  hardware.
- The release tag is available at:
  <https://github.com/NoNine/rp2350-relay-6ch/releases/tag/v0.1.0>.

### Verification

- Phase 6 verification recorded `PASS` for Waveshare RP2350-Relay-6CH hardware.
- Host tests passed with `25 passed`.
- CLI help printed successfully from the Zephyr workspace virtual environment.
- The CLI hardware smoke command was reported PASS by the hardware operator.
- The smoke command queried controller info/status, pulsed `CH1` through `CH6`,
  and attempted final `off-all` teardown.

### Known Limitations

- MCUboot-compatible firmware upload, test-image, confirm-image, and rollback
  workflows are not implemented in this release.
- RS485 and wireless host communication are not v1 control paths.
- Relay state is not persisted across reboot.
- Application-layer authentication is not implemented; this release assumes
  trusted local USB access.

### Safety Notes

- Keep hazardous relay-side loads disconnected during bring-up.
- Confirm all relays are off after flashing, reset, smoke tests, and teardown.
- Run `rp2350-relay --port <serial-port> off-all` before disconnecting relay
  hardware or ending a failed hardware test.
