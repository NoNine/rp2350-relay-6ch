# RP2350 Relay Controller PRD

## 1. Summary

This product is a Zephyr-based firmware and host tooling stack for the
Waveshare RP2350-Relay-6CH board. The device receives commands from a host
computer and controls six relay outputs accordingly. The host communicates with
the device through a packet-based RPC protocol, with a Python library providing
the protocol implementation and a CLI utility providing test, debug, and
operations workflows.

The project is expected to be implemented in multiple phases because it spans
embedded firmware, host-side protocol code, command-line tooling, firmware
upgrade, and bootloader integration.

Primary hardware details are maintained in
[hardware-info.md](hardware-info.md). The required target is the
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
- Firmware shall model relays as six active-high GPIO outputs on GPIO26 through
  GPIO31.
- Firmware shall initialize all relay outputs to off as early as practical
  during boot.
- Firmware shall avoid assigning relay GPIOs to any alternate function.
- Firmware shall support the board USB-C device connector as the primary host
  communication path.
- Firmware should expose the active-high buzzer on GPIO23 and WS2812 RGB LED on
  GPIO36 for status indication, if doing so does not interfere with the relay
  control and RPC requirements.
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
- Firmware may support an optional command/session timeout that turns all relays
  off after communication loss. The default v1 behavior is disabled unless
  explicitly configured.

### 5.3 Protocol

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

### 5.4 Error Handling

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

### Phase 2: USB CDC RPC And Relay Command Set

- Enable USB CDC transport.
- Enable MCUmgr/SMP and CBOR payload handling.
- Add the custom relay management group.
- Implement device info, get, set, set-all, pulse, off-all, status, and reboot
  commands.
- Define stable command IDs, payload fields, and error codes.

Acceptance criteria:

- A host can control all relays over USB CDC.
- Invalid commands and invalid arguments return structured errors.
- Packet sequence and timeout behavior can be tested.

### Phase 3: Python RPC Library

- Implement serial transport connection handling.
- Implement SMP packet encoding/decoding and CBOR payload handling.
- Implement relay command wrappers and typed exceptions.
- Add unit tests using simulated transports.

Acceptance criteria:

- Library tests cover encode/decode, success responses, error responses,
  timeouts, retries, and relay commands.
- Library API can control real hardware through USB CDC.

### Phase 4: CLI Utility

- Implement the CLI on top of the Python RPC library.
- Add human-readable and machine-readable output modes.
- Add commands for relay control, status, reboot, and update workflow.
- Add CLI-level tests for argument validation and error exit codes.

Acceptance criteria:

- CLI can run hardware smoke tests for all six relays.
- CLI is suitable for scripted debug and manufacturing checks.

### Phase 5: A/B Firmware Upgrade And Rollback

- Add MCUboot bootloader integration.
- Define flash partition layout for A/B slots.
- Enable Zephyr DFU/image management support.
- Implement host firmware upload workflow over USB CDC RPC.
- Implement test-image, reboot, health-check, confirm, and rollback behavior.

Acceptance criteria:

- Valid signed image uploads to inactive slot and can be test-booted.
- Healthy image confirms itself.
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
- Hardware smoke tests shall verify all six relays can be controlled
  independently and that reset returns relays off.
- Upgrade tests shall verify valid upload, invalid image rejection, pending/test
  boot, image confirmation, rollback after no confirmation, and interrupted
  upload recovery.

## 10. Documentation Requirements

- Document hardware pin mapping and safe relay assumptions.
- Document protocol version, group ID, command IDs, CBOR fields, and error
  codes.
- Document host library API and CLI usage.
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
