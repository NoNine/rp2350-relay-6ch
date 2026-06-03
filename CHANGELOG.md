# Changelog

Release history for `rp2350-relay-6ch`.

This project keeps concise release history here and uses GitHub Releases for
release assets. The section shape is inspired by Adafruit CircuitPython release
notes: highlights, install/assets, verification, known limitations, and safety
notes.

## 0.8.9 - 2026-06-03

Communication-loss, health, product-build, and daemon reliability release for
the current relay firmware and host tooling line.

- Tag: `v0.8.9`
- Commit: the `v0.8.9` tag target.

### Highlights

- Added communication-loss safety handling with owner timeout indication,
  recovery behavior, and local operator feedback.
- Added compact health reporting, reboot failure health status, and a runtime
  watchdog supervisor path.
- Refined the relay management protocol roles, split relay get handling, and
  decoded relay capabilities in host output.
- Promoted product composition as the top-level build workflow with YAML
  product configs, scoped release configs, and scoped wheel staging.
- Improved reboot recovery, startup recovery, retained display cleanup, and
  daemon reconnect probing.
- Added host smoke command coverage, documented host control surfaces, and
  documented relay daemon autostart setup.

### Install / Assets

- Install the host CLI from the GitHub Release wheel:
  `rp2350_relay_6ch-0.8.9-py3-none-any.whl`.
- Flash one of the matching firmware artifacts from the same release:
  `rp2350_relay_6ch-0.8.9-waveshare.uf2` or
  `rp2350_relay_6ch-0.8.9-pico2.uf2`.
- Optional platform executable artifacts may be attached when useful:
  `rp2350_relay_6ch-0.8.9-linux-x64` or
  `rp2350_relay_6ch-0.8.9-windows-x64.exe`.
- The source distribution `rp2350_relay_6ch-0.8.9.tar.gz` may be available as
  an additional artifact.

### Verification

- Host tests passed with
  `${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/python -m pytest host/tests`.
- Release metadata and formatting passed `git diff --check`.
- Release artifact build passed with `scripts/build.sh release 0.8.9`.

### Known Limitations

- MCUboot-compatible firmware upload, test-image, confirm-image, and rollback
  workflows remain planned but not implemented.
- OLED status is firmware-local and is not exposed through host-visible status,
  protocol, CLI, or daemon APIs.
- Pico 2 and Pico 2 W DIY builds require explicit relay overlays and external
  relay driver hardware.
- RS485 and wireless host communication are not v1 control paths.
- Application-layer authentication is not implemented; this release assumes
  trusted local USB or local Unix socket access.

### Safety Notes

- Keep hazardous relay-side loads disconnected during bring-up.
- Confirm all relays are off after flashing, reset, smoke tests, daemon
  reconnect checks, service restart checks, and teardown.
- Communication-loss safety is firmware policy for relay outputs; it does not
  measure contact closure, load voltage, load current, or external equipment
  health.
- The OLED indicator is informational only; safe relay state remains defined by
  relay GPIO state, firmware behavior, and host control checks.

## 0.8.8 - 2026-05-28

OLED indicator and Linux service-management release for the current relay
firmware and host tooling line.

- Tag: `v0.8.8`
- Commit: the `v0.8.8` tag target.

### Highlights

- Added Linux user systemd instance support for `rp2350-relayd`, including
  generated service files, named instance configuration, and doctor helpers
  under `rp2350-relayctl`.
- Added optional firmware-local SSD1306 OLED indicator support with POST,
  display failure handling, relay state rendering, pulse state rendering, and
  native framebuffer coverage.
- Added a fixed 128x64 OLED floorplan with annunciators, relay cells, status
  band, preview-aligned divider rules, and pulse countdown bars.
- Enabled the optional OLED setup for Waveshare firmware builds on I2C1 at
  address `0x3d`, using GP10 for SDA and GP11 for SCL.
- Documented OLED behavior, Waveshare wiring, daemon systemd installation,
  daemon troubleshooting, and updated CLI and README release guidance.

### Install / Assets

- Install the host CLI from the GitHub Release wheel:
  `rp2350_relay_6ch-0.8.8-py3-none-any.whl`.
- Flash one of the matching firmware artifacts from the same release:
  `rp2350_relay_6ch-0.8.8-waveshare.uf2` or
  `rp2350_relay_6ch-0.8.8-pico2.uf2`.
- Optional platform executable artifacts may be attached when useful:
  `rp2350_relay_6ch-0.8.8-linux-x64` or
  `rp2350_relay_6ch-0.8.8-windows-x64.exe`.
- The source distribution `rp2350_relay_6ch-0.8.8.tar.gz` may be available as
  an additional artifact.

### Verification

- Host tests passed with `scripts/test-host.sh`: `144 passed`.
- Release metadata and formatting passed `git diff --check`.
- Earlier implementation checks in this line built default Waveshare firmware,
  Pico 2 firmware, OLED-enabled Pico 2/Pico 2 W firmware, and native indicator
  and relay test suites.

### Known Limitations

- MCUboot-compatible firmware upload, test-image, confirm-image, and rollback
  workflows remain planned but not implemented.
- Communication-loss firmware safety actions remain planned but not
  implemented.
- OLED status is firmware-local and is not exposed through host-visible status,
  protocol, CLI, or daemon APIs.
- Pico 2 and Pico 2 W DIY builds require explicit relay overlays and external
  relay driver hardware.
- RS485 and wireless host communication are not v1 control paths.
- Application-layer authentication is not implemented; this release assumes
  trusted local USB or local Unix socket access.

### Safety Notes

- Keep hazardous relay-side loads disconnected during bring-up.
- Confirm all relays are off after flashing, reset, smoke tests, daemon
  reconnect checks, service restart checks, and teardown.
- The OLED indicator is informational only; safe relay state remains defined by
  relay GPIO state, firmware behavior, and host control checks.
- Daemon health checks are host-side checks through the firmware heartbeat
  command; they do not measure contact closure, load voltage, load current, or
  external equipment health.

## 0.8.5 - 2026-05-26

Linux daemon and CLI reliability release for the current relay host tooling
line.

- Tag: `v0.8.5`
- Commit: the `v0.8.5` tag target.

### Highlights

- Added Linux `rp2350-relayd` daemon mode for one local daemon to own one relay
  controller through a Unix socket.
- Added `rp2350-relayctl` daemon-client commands for short-lived relay control
  through the daemon.
- Added `daemon-status` for daemon connection state, with human-readable output
  by default and JSON available on request.
- Added daemon heartbeat polling while idle, with reconnect recovery after
  heartbeat transport failures.
- Fixed a session-mode reboot reconnect race.
- Aligned human key/value CLI output for more consistent operator-readable
  one-shot command output.
- Recorded Phase 8b daemon-mode verification.

### Install / Assets

- Install the host CLI from the GitHub Release wheel:
  `rp2350_relay_6ch-0.8.5-py3-none-any.whl`.
- Flash one of the matching firmware artifacts from the same release:
  `rp2350_relay_6ch-0.8.5-waveshare.uf2` or
  `rp2350_relay_6ch-0.8.5-pico2.uf2`.
- Optional platform executable artifacts may be attached when useful:
  `rp2350_relay_6ch-0.8.5-linux-x64` or
  `rp2350_relay_6ch-0.8.5-windows-x64.exe`.
- The source distribution `rp2350_relay_6ch-0.8.5.tar.gz` may be available as
  an additional artifact.

### Verification

- Phase 8b verification recorded PASS for the automated host gate and
  operator-reported daemon hardware smoke test.
- Host tests passed with `scripts/test-host.sh`: `126 passed`.
- Built the host wheel with `python -m build --wheel`.
- Verified the rebuilt wheel contains daemon entry points, heartbeat polling,
  and daemon-status human formatting.

### Known Limitations

- MCUboot-compatible firmware upload, test-image, confirm-image, and rollback
  workflows remain planned but not implemented.
- Communication-loss firmware safety actions remain planned but not
  implemented.
- Pico 2 and Pico 2 W DIY builds require explicit relay overlays and external
  relay driver hardware.
- RS485 and wireless host communication are not v1 control paths.
- Application-layer authentication is not implemented; this release assumes
  trusted local USB or local Unix socket access.

### Safety Notes

- Keep hazardous relay-side loads disconnected during bring-up.
- Confirm all relays are off after flashing, reset, smoke tests, daemon
  reconnect checks, and teardown.
- Daemon health checks are host-side checks through the firmware heartbeat
  command; they do not measure contact closure, load voltage, load current, or
  external equipment health.

## 0.8.0 - 2026-05-25

Cross-platform session mode release for the current relay CLI and firmware
line.

- Tag: `v0.8.0`
- Commit: the `v0.8.0` tag target.

### Highlights

- Added cross-platform `rp2350-relay session` mode for long-lived manual relay
  operation on Windows and Linux.
- Added interactive USB relay discovery, explicit serial-port selection, and
  USB serial-number selection for session startup and reconnect workflows.
- Added prompt history, completion, safe disconnect/exit checks, and one-client
  command serialization for session mode.
- Added host executable build tooling for bundled operator CLI artifacts.
- Reduced background heartbeat noise to concise session status lines.
- Added a one-time `heartbeat: restored` notice after heartbeat polling
  recovers from a failure streak.
- Recorded Phase 8a session-mode verification.
- Added repository implementation-discipline guidance for minimal changes,
  existing component reuse, and avoiding unnecessary abstractions.

### Install / Assets

- Install the host CLI from the GitHub Release wheel:
  `rp2350_relay_6ch-0.8.0-py3-none-any.whl`.
- Flash one of the matching firmware artifacts from the same release:
  `rp2350_relay_6ch-0.8.0-waveshare.uf2` or
  `rp2350_relay_6ch-0.8.0-pico2.uf2`.
- Optional platform executable artifacts may be attached when useful:
  `rp2350_relay_6ch-0.8.0-linux-x64` or
  `rp2350_relay_6ch-0.8.0-windows-x64.exe`.
- The source distribution `rp2350_relay_6ch-0.8.0.tar.gz` may be available as
  an additional artifact.

### Verification

- Phase 8a verification recorded PASS for the automated host gate and
  operator-reported hardware session smoke test.
- Host tests passed with `scripts/test-host.sh`: `87 passed`.
- Built the host wheel with `python -m build --wheel`.
- Verified the rebuilt wheel contains `heartbeat: restored` and the concise
  heartbeat transport status text.
- Wheel build output included the existing setuptools deprecation warning from
  environment configuration.

### Known Limitations

- Linux production daemon mode remains planned but not implemented.
- MCUboot-compatible firmware upload, test-image, confirm-image, and rollback
  workflows remain planned but not implemented.
- Communication-loss firmware safety actions remain planned but not
  implemented.
- Pico 2 and Pico 2 W DIY builds require explicit relay overlays and external
  relay driver hardware.
- RS485 and wireless host communication are not v1 control paths.
- Application-layer authentication is not implemented; this release assumes
  trusted local USB access.

### Safety Notes

- Keep hazardous relay-side loads disconnected during bring-up.
- Confirm all relays are off after flashing, reset, smoke tests, and teardown.
- Session mode safe exit checks are host-side checks of reported relay state;
  they do not measure contact closure, load voltage, load current, or external
  equipment health.

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
