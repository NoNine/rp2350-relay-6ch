# RP2350 Relay Controller PRD

## 1. Summary

This product is a Zephyr-based firmware and host tooling stack for six-channel
RP2350 relay controllers. The device receives commands from a host computer
and controls six relay outputs accordingly. The host communicates with the
device through a packet-based RPC protocol, with a Python library providing the
protocol implementation and a CLI utility providing test, debug, and
operations workflows.

The project is expected to be implemented in multiple phases because it spans
embedded firmware, host-side protocol code, command-line tooling, firmware
upgrade, and bootloader integration.

Primary Waveshare hardware details are maintained in
[hardware-info.md](hardware-info.md). The required Waveshare target is the
RP2350B-based RP2350-Relay-6CH board with six active-high relay GPIOs:

| Relay | GPIO | Default state |
| --- | ---: | --- |
| CH1 | GPIO26 | Off |
| CH2 | GPIO27 | Off |
| CH3 | GPIO28 | Off |
| CH4 | GPIO29 | Off |
| CH5 | GPIO30 | Off |
| CH6 | GPIO31 | Off |

## 2. Goals

- Provide reliable host-controlled relay switching for all six relay channels.
- Support board-specific relay mappings through devicetree so the firmware and
  host tooling can be reused across supported RP2350 relay targets.
- Reuse Zephyr OS subsystems where practical instead of implementing custom
  infrastructure from scratch.
- Use a packet-based RPC protocol suitable for command, response, error, and
  firmware-update workflows.
- Provide a Python host-side RPC library that encapsulates packet framing,
  encoding, command handling, timeouts, retries, and errors.
- Provide a host-side CLI utility that uses the Python RPC library for manual
  control, automated tests, debug, and firmware upgrade.
- Support A/B firmware upgrade with rollback so an interrupted or faulty
  upgrade does not brick the device.

## 3. Non-Goals For V1

- RS485 host communication is not required in v1.
- Raspberry Pi Radio Module 2 wireless networking is not required in v1.
- Persisting relay state across reboot is not required in v1.
- Application-layer authentication is not required in v1; v1 assumes trusted
  local USB access and signed firmware images.
- A graphical host application is not required in v1.

## 4. Target Hardware Requirements

- Firmware shall target the Waveshare RP2350-Relay-6CH board variant described
  in `docs/hardware-info.md`.
- Firmware shall support documented board-specific devicetree relay mappings
  for reusable RP2350 relay builds.
- Firmware shall model relays as six GPIO outputs defined by devicetree. On
  Waveshare hardware these shall be active-high GPIO26 through GPIO31.
- Firmware shall initialize all relay outputs to off as early as practical
  during boot.
- Firmware shall avoid assigning relay GPIOs to any alternate function.
- Firmware shall support the board USB device connector as the primary host
  communication path.
- Firmware should expose the passive PWM-driven buzzer on GPIO23 and WS2812 RGB
  LED on GPIO36 for local status indication, if doing so does not interfere
  with the relay control and RPC requirements.
- Firmware shall not assume MCU-side `GND` and isolated RS485/relay-side `SGND`
  are the same domain.

## 5. Firmware Requirements

### 5.1 Zephyr OS Usage

The Zephyr application shall use Zephyr-provided features wherever they cover
the needed behavior:

- GPIO drivers for relay, buzzer, and status LED control.
- USB device support with CDC ACM as the v1 host transport.
- MCUmgr/SMP for packet-based request/response management.
- CBOR encoding for protocol payloads.
- Zephyr logging for firmware diagnostics.
- Flash map and DFU support for image upload and image-state management.
- MCUboot integration for bootloader-compatible image format and A/B boot.
- Watchdog and health checks for safe upgrade confirmation where supported.

### 5.2 Relay Behavior

- All relays shall default to off on boot, reset, firmware crash, and firmware
  restart.
- Firmware shall expose commands to read and control each relay independently.
- Firmware shall expose a command to set all relays in a single atomic logical
  operation.
- Firmware shall expose a pulse command that turns a relay on for a requested
  duration and then returns it off.
- Firmware shall validate relay indexes and reject out-of-range channels.
- Firmware shall validate pulse durations and reject values outside the
  supported range.
- Firmware shall make relay state queryable after every control operation.
- Firmware shall return a structured error when a command cannot be executed.
- A communication-loss safety mechanism, such as a command/session timeout that
  turns all relays off, is not a current project requirement.

### 5.3 Local Status Indicators

- The RGB LED shall indicate controller-level state only, such as booting,
  ready, command accepted, relay-active, degraded, update in progress, or fault.
- The RGB LED shall not imply measured relay contact closure, load voltage,
  load current, or external equipment state.
- A relay-active RGB LED indication shall mean one or more relays are commanded
  on or pulsing.
- The buzzer shall be reserved for local attention and explicit user operation
  feedback. Routine relay toggles shall not generate audible feedback by
  default.
- Buzzer alerts shall be bounded by a timeout or silence policy. Firmware shall
  not generate an indefinite alarm without an operator-controlled silence path.
- Indicator failures shall not block relay control, `off-all`, pulse teardown,
  reboot handling, or RPC responses.
- Host-visible status and command responses remain authoritative for automation
  and troubleshooting.

### 5.4 Protocol

The device protocol shall be based on Zephyr MCUmgr/SMP:

- SMP shall provide the packet envelope, sequence number, operation, group ID,
  command ID, and response matching behavior.
- Payloads shall use CBOR maps.
- Relay control shall be implemented as an application-specific SMP management
  group. Zephyr documents application-specific groups as starting at group ID
  64.
- Responses shall use standard SMP response behavior and shall include
  structured status or error data.
- The protocol shall be versioned so future host libraries can detect command
  compatibility.
- The protocol shall be deterministic enough for automated host-side testing.

Required relay management commands:

| Command | Purpose |
| --- | --- |
| Device info | Return firmware version, protocol version, hardware name, boot slot, and capabilities. |
| Get relay state | Return current state for one relay or all relays. |
| Set relay | Set one relay on or off. |
| Set all relays | Set all six relays using one request. |
| Pulse relay | Turn one relay on for a bounded duration, then off. |
| Off all relays | Turn all relays off. |
| Get status | Return last error, uptime, boot/update state, and communication counters where available. |
| Reboot | Request controlled firmware reboot for update and debug workflows. |

Required firmware-update commands may use Zephyr's standard MCUmgr image
management group where available instead of duplicating image upload semantics
inside the custom relay group.

### 5.5 Error Handling

- Invalid CBOR payloads shall return decode errors.
- Unknown command IDs shall return command-not-supported errors.
- Invalid relay indexes shall return invalid-argument errors.
- Invalid pulse durations shall return invalid-argument errors.
- Busy relays, overlapping pulse requests, or unavailable resources shall return
  busy errors.
- Firmware update failures shall identify whether the failure happened during
  upload, validation, image test selection, boot, or confirmation.
- Host-visible errors shall be stable and documented for scripting use.

## 6. Host-Side Requirements

### 6.1 Python RPC Library

The host-side Python library shall implement the complete host protocol stack:

- Serial-port discovery or explicit serial-port connection for USB CDC.
- SMP packet framing and parsing.
- CBOR request and response encoding.
- Request sequence management.
- Timeouts and bounded retry behavior.
- Relay command wrappers.
- Firmware upload and image-state wrappers.
- Typed exceptions for protocol, timeout, transport, validation, and device
  errors.
- Test doubles or simulated transports for unit testing without hardware.

The library shall hide packet details from callers. A caller should be able to
call methods such as `get_info()`, `get_relays()`, `set_relay()`,
`set_all_relays()`, `pulse_relay()`, `off_all()`, `upload_firmware()`,
`test_image()`, `confirm_image()`, and `reboot()`.

### 6.2 CLI Utility

The CLI shall use the Python RPC library and provide commands suitable for
manual testing, manufacturing checks, and debug:

- `info`: print device, firmware, protocol, and boot/update status.
- `get`: print one relay or all relay states.
- `set`: set one relay on or off.
- `set-all`: set all relay states.
- `pulse`: pulse one relay for a requested duration.
- `off-all`: turn all relays off.
- `upload`: upload a signed firmware image to the inactive slot.
- `test-image`: mark an uploaded image for test boot.
- `confirm-image`: confirm the currently running image.
- `reboot`: reboot the device.
- `status`: print health, last error, uptime, counters, and update state.

The CLI shall return non-zero exit codes on command failure and shall support a
machine-readable output mode for automated testing.

## 7. Firmware Upgrade And Boot Requirements

- The product shall support A/B firmware upgrade.
- The bootloader shall support A/B booting and rollback.
- MCUboot shall be the required bootloader unless a later design explicitly
  replaces it with a bootloader that provides equivalent signed-image,
  test-boot, confirm, and rollback behavior.
- Zephyr application builds shall enable MCUboot-compatible image generation.
- Flash partitions shall include a bootloader region, primary slot, secondary
  slot, and scratch/status storage as required by the selected MCUboot swap
  mode.
- Firmware images shall be signed before upload.
- Host-side firmware upload shall transfer the image over the same USB CDC
  packet/RPC path used for normal control.
- Firmware upload shall write only to the inactive slot.
- The host shall be able to mark the uploaded image as pending/test.
- After reboot, the new firmware shall only confirm itself after health checks
  pass.
- If the new firmware does not confirm itself, the bootloader shall roll back to
  the previous confirmed image.
- Interrupted uploads shall not affect the currently running confirmed image.
- Invalid or unsigned images shall be rejected before test boot.

Minimum health checks before confirmation:

- Relay GPIO subsystem initialized successfully.
- All six relays are in the configured safe state.
- RPC service initialized successfully.
- Firmware can read its own boot/update state.

## 8. Phased Implementation Plan

This PRD defines the product scope and high-level delivery order. The detailed
phase gates, dependencies, and deliverables are maintained in
[implementation-plan.md](implementation-plan.md), which is the authoritative
implementation phase breakdown.

### Phase 1: Board Bring-Up And Relay Control

- Create the Zephyr application structure and board configuration.
- Configure relay GPIOs from hardware documentation.
- Initialize relays to off.
- Implement basic relay state model and direct firmware tests where possible.
- Add logging and simple status indication.

Acceptance criteria:

- Firmware builds for the target board.
- Each relay can be switched on and off from internal test code or a temporary
  shell/debug path.
- Relays default off after reset.

### Phase 2: Pulse Handling And Relay Safety Semantics

- Define supported pulse duration bounds.
- Implement pulse command internals before exposing host RPC.
- Reject invalid channels, invalid durations, and overlapping pulse requests.
- Keep relay state queryable during and after pulses.
- Add teardown-safe test helpers that force all relays off.

Acceptance criteria:

- Unit tests cover valid pulses, duration bounds, invalid channels, busy
  relays, and final off state.
- Hardware smoke tests verify short pulses leave every relay off.
- No test leaves a relay on after failure.

### Phase 3: Custom SMP Relay Management Group

- Enable MCUmgr/SMP and CBOR payload handling.
- Add the custom relay management group.
- Implement device info, get, set, set-all, pulse, off-all, status, and reboot
  commands.
- Define stable command IDs, payload fields, and error codes.

Acceptance criteria:

- Invalid commands and invalid arguments return structured errors.
- Protocol tests cover valid payloads, malformed CBOR, unknown commands,
  invalid relay indexes, invalid durations, and busy pulse behavior.

### Phase 4: USB CDC SMP Transport

- Enable USB device support and CDC ACM transport.
- Bind SMP transport to CDC ACM.
- Keep relay startup safe while USB and RPC initialize.
- Add manual smoke-test notes for the CDC serial connection.

Acceptance criteria:

- A host can control all relays over USB CDC.
- Invalid RPC requests return structured errors without crashing firmware.

### Phase 5: Python RPC Library

- Implement serial transport connection handling.
- Implement SMP packet encoding/decoding and CBOR payload handling.
- Implement relay command wrappers and typed exceptions.
- Add unit tests using simulated transports.

Acceptance criteria:

- Library tests cover encode/decode, success responses, error responses,
  timeouts, retries, and relay commands.
- Library API can control real hardware through USB CDC.

### Phase 6: CLI Utility

- Implement the CLI on top of the Python RPC library.
- Add human-readable and machine-readable output modes.
- Add commands for relay control, status, and reboot.
- Add CLI-level tests for argument validation and error exit codes.

Acceptance criteria:

- CLI can run hardware smoke tests for all six relays.
- CLI is suitable for scripted debug and manufacturing checks.

### Phase 7: Local Status Indicators

- Enable the RGB LED and buzzer where supported by the target hardware.
- Implement controller-level RGB LED states for boot, ready, command accepted,
  relay-active, degraded, update, and fault conditions.
- Keep buzzer feedback bounded and build-time configurable. Development builds
  enable command feedback by default for hardware verification.
- Ensure indicator failures do not affect relay control, `off-all`, pulse
  teardown, reboot handling, or RPC responses.

Acceptance criteria:

- Firmware tests cover RGB LED state priority and buzzer feedback mapping.
- Existing relay, relay-management, host, and CLI tests still pass.
- Hardware smoke checks verify local indicators without leaving relays on.

### Phase 8a: Cross-Platform Session Mode

- Add `rp2350-relay session` as a cross-platform manual/direct operator
  workflow.
- Keep the session as a long-lived direct-serial owner for one selected serial
  connection while it is connected.
- Support explicit `--port`, exact `--serial`, and interactive USB discovery
  and selection for session startup.
- Reuse the existing host client, validation rules, typed exceptions, and
  one-based CLI channel arguments.
- Keep one-shot direct serial commands available for diagnostics and simple
  checks.
- Do not require firmware communication-loss safety beyond the dummy heartbeat
  command used for session polling.

Acceptance criteria:

- Session tests pass without hardware.
- Existing direct host library and CLI tests still pass.
- Windows and Linux hardware smoke checks can control relays through one
  long-lived session and leave all relays off after manual teardown.

### Phase 8b: Host Daemon Mode

- Add Linux daemon mode as the production host-control workflow.
- Add `rp2350-relayd` to own the USB CDC serial port, serialize commands, and
  handle reconnects.
- Add `rp2350-relayctl` and a Python daemon client API for short-lived local
  clients.
- Use newline-delimited JSON over a same-user Unix domain socket.
- Ship a `systemd --user` unit for production operation.
- Keep direct serial control available through the existing CLI for diagnostics.
- Do not require firmware communication-loss safety or heartbeat support.

Acceptance criteria:

- Daemon and daemon-client tests pass without hardware.
- Existing direct host library and CLI tests still pass.
- Linux hardware smoke checks can control relays through the daemon without
  root and leave all relays off after clean shutdown.

### Phase 9: Firmware Upgrade Foundation

- Add MCUboot bootloader integration.
- Define flash partition layout for A/B slots.
- Enable Zephyr DFU/image management support.
- Extend device info/status with boot slot and image state.
- Add minimum boot health checks.

Acceptance criteria:

- MCUboot image builds and signs successfully.
- Device boots a signed application image.
- Relay defaults remain off with MCUboot enabled.

### Phase 10: Host Firmware Upload And Rollback Workflow

- Add Python and CLI wrappers for standard MCUmgr image upload and image state.
- Implement test-image, reboot, health-check, confirm, and rollback behavior.
- Document interrupted upload, invalid image rejection, test boot,
  confirmation, and rollback.

Acceptance criteria:

- Valid signed image uploads to the inactive slot and can be test-booted.
- Healthy image confirms itself after checks pass.
- Unhealthy or unconfirmed image rolls back to the previous confirmed image.
- Interrupted upload does not brick or replace the running image.

## 9. Testing Requirements

- Unit tests shall cover relay state transitions, invalid inputs, pulse timing,
  and default-off initialization logic.
- Protocol tests shall cover valid commands, malformed CBOR, unknown command
  IDs, invalid relay indexes, invalid durations, busy states, sequence matching,
  and timeout behavior.
- Host library tests shall cover packet encode/decode, response matching,
  retries, timeout handling, typed errors, and simulated device responses.
- CLI tests shall cover argument parsing, output modes, non-zero failure exit
  codes, and representative relay/update commands.
- Session tests shall cover cross-platform session command parsing, USB
  discovery, command dispatch, one-connection session ownership, typed error
  handling, safe close behavior, reconnect behavior, and compatibility with
  existing one-shot direct commands.
- Daemon tests shall cover local protocol parsing, command serialization,
  client command behavior, reconnect handling, startup state query, no-client
  relay-state policy, and clean shutdown.
- Hardware smoke tests shall verify all six relays can be controlled
  independently and that reset returns relays off.
- Local-indicator tests shall verify RGB LED state-priority behavior, buzzer
  feedback patterns, and that indicator failures do not affect relay safety or
  host RPC behavior when the feature is enabled.
- Upgrade tests shall verify valid upload, invalid image rejection, pending/test
  boot, image confirmation, rollback after no confirmation, and interrupted
  upload recovery.

## 10. Documentation Requirements

- Document hardware pin mapping and safe relay assumptions.
- Document protocol version, group ID, command IDs, CBOR fields, and error
  codes.
- Document host library API and CLI usage.
- Document cross-platform session mode, daemon mode, `systemd --user`
  operation, local socket path, direct diagnostic mode, and host-control smoke
  testing.
- Document RGB LED and buzzer status meanings before enabling local indicator
  support in released firmware.
- Document firmware signing and upgrade workflow.
- Document recovery procedure for failed update attempts.
- Document known limitations for v1, including lack of RS485 and wireless
  transport support.

## 11. References

- Local hardware source: [hardware-info.md](hardware-info.md)
- Zephyr SMP protocol:
  <https://docs.zephyrproject.org/latest/services/device_mgmt/smp_protocol.html>
- Zephyr SMP transport:
  <https://docs.zephyrproject.org/latest/services/device_mgmt/smp_transport.html>
- Zephyr Device Firmware Upgrade:
  <https://docs.zephyrproject.org/latest/services/device_mgmt/dfu.html>
- Zephyr Device Management:
  <https://docs.zephyrproject.org/latest/services/device_mgmt/index.html>
