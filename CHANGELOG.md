# Changelog

Release history for `rp2350-relay-6ch`.

This project keeps concise release history here and uses GitHub Releases for
release assets. The section shape is inspired by Adafruit CircuitPython release
notes: highlights, install/assets, verification, known limitations, and safety
notes.

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
