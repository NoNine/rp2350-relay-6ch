# RP2350 Relay Controller Implementation Plan

This plan turns the PRD in `docs/prd.md` into staged, testable work. The
first phases prioritize relay safety and relay control before host tooling and
firmware update flows.

## Scaffolded Structure

```text
firmware/
  boards/                         # Target board overlays, board metadata, partition files
  include/rp2350_relay_6ch/        # Firmware public/internal headers
  src/                            # Zephyr application sources
  tests/                          # Firmware unit/integration tests
host/
  rp2350_relay_6ch/               # Python RPC library package
  tests/                          # Host library and CLI tests
tools/                            # CLI entry points and operational helpers
scripts/                          # Build, flash, signing, and smoke-test helpers
docs/
  protocol/                       # SMP group, commands, CBOR fields, errors
  testing/                        # Hardware and upgrade test procedures
```

## Phase Rules

- Each phase must leave the repository in a buildable/testable state.
- Each phase must have its detailed phase plan saved as
  `docs/phase-{phase-no}-plan.md` before implementation starts.
- A phase is complete only after its acceptance tests pass and the relevant
  documentation is updated.
- Later phases may not start until dependencies from earlier phases are
  verified.
- Relay outputs must fail safe: all six channels default off on boot, reset,
  firmware restart, and test setup/teardown.
- Temporary debug paths are allowed only inside the current phase and must be
  removed or replaced before the phase is marked complete.

## Phase 0: Project Baseline And Build Harness

Purpose: establish the minimal Zephyr and Python project shell needed for
repeatable implementation.

Implementation scope:

- Add top-level build metadata for the Zephyr application.
- Add initial `firmware/CMakeLists.txt`, `firmware/prj.conf`, and app entry
  point.
- Add Python packaging/test metadata for host tooling.
- Add scripts for build, unit test, and hardware smoke-test entry points.
- Document the expected Zephyr workspace and board target name once confirmed.

Tests/gates:

- `west build` reaches configuration or a documented board-port blocker.
- Python test runner starts and reports zero or placeholder tests cleanly.
- CI/local script entry points exist and fail with actionable messages when
  prerequisites are missing.

Dependencies:

- `docs/hardware-info.md`
- Zephyr workspace available under the developer environment.

### Assumptions

- Use Raspberry Pi Pico 2 W (`rpi_pico2/rp2350a/m33/w`) as the temporary
  development board target until a custom RP2350-Relay-6CH board definition or
  overlay is available.

## Phase 1: Board Bring-Up And Relay GPIO Control

Purpose: implement safe direct control for the six relay outputs before adding
USB or RPC complexity.

Implementation scope:

- Create or adapt board/device-tree configuration for the RP2350-Relay-6CH.
- Model `CH1` through `CH6` as active-high GPIO outputs on GPIO26 through
  GPIO31.
- Initialize all relay GPIOs to off as early as practical during boot.
- Implement a relay module with:
  - channel count and index validation
  - get one/get all state
  - set one relay
  - set all relays as one logical operation
  - off-all operation
  - bounded pulse state machine or timer foundation
- Add firmware unit tests for relay state transitions and invalid indexes.
- Add a temporary Zephyr shell/debug command or test hook for manual relay
  switching during bring-up.
- Add simple logging for relay initialization and control failures.

Tests/gates:

- Firmware builds for the selected RP2350 target.
- Unit tests cover default-off state, set one, set all, off all, and invalid
  channel rejection.
- Hardware smoke test confirms each relay can switch on/off independently.
- Reset/power-cycle smoke test confirms all relays return off.

Dependencies:

- Phase 0 complete.
- Confirmed board target or local board overlay.

Deliverables:

- Relay driver/module implementation.
- Relay test procedure in `docs/testing/relay-smoke-test.md`.
- Updated hardware notes if measured relay polarity differs from active high.

## Phase 2: Pulse Handling And Relay Safety Semantics

Purpose: finish relay behavior that affects safety and concurrency before
exposing commands to hosts.

Implementation scope:

- Define supported pulse duration range.
- Implement pulse command internals:
  - validates relay index
  - validates duration
  - turns relay on immediately
  - returns relay off when the duration expires
  - rejects overlapping pulse requests for a busy relay
- Ensure relay state remains queryable during and after a pulse.
- Add optional compile-time disabled session timeout foundation if it does not
  complicate v1 relay behavior.
- Add teardown-safe test utilities that force all relays off.

Tests/gates:

- Unit tests cover valid pulse, duration bounds, invalid channel, busy relay,
  and final off state.
- Hardware smoke test pulses each relay with a short duration and verifies
  final off state.
- No test leaves a relay on after failure.

Dependencies:

- Phase 1 complete.

Deliverables:

- Documented pulse duration bounds in `docs/protocol/relay-management.md`.
- Updated relay smoke tests.

## Phase 3: Custom SMP Relay Management Group

Purpose: expose the relay control surface through deterministic MCUmgr/SMP
commands over an internal or test transport first.

Implementation scope:

- Enable MCUmgr/SMP and CBOR payload support.
- Define application management group ID, protocol version, command IDs, CBOR
  request/response fields, and stable error codes.
- Implement commands:
  - device info
  - get relay state
  - set relay
  - set all relays
  - pulse relay
  - off all relays
  - get status
  - reboot
- Map firmware validation failures to structured host-visible errors.
- Track basic counters for received commands, successful commands, decode
  errors, invalid arguments, and busy responses.

Tests/gates:

- Protocol unit tests cover all command handlers with valid CBOR payloads.
- Tests cover malformed CBOR, missing fields, unknown command IDs, invalid
  indexes, invalid durations, and busy pulse behavior.
- Device info reports firmware version, protocol version, hardware name,
  capabilities, and boot/update fields that are available at this phase.

Dependencies:

- Phase 2 complete.

Deliverables:

- `docs/protocol/relay-management.md` with group ID, command IDs, CBOR schema,
  response schema, and error code table.

## Phase 4: USB CDC SMP Transport

Purpose: make relay RPC available to a host computer over the board USB-C
device connector.

Implementation scope:

- Enable USB device support and CDC ACM transport.
- Bind SMP transport to CDC ACM.
- Ensure startup order keeps relays off while USB/RPC initializes.
- Add host-visible status for transport initialization and counters.
- Add manual smoke-test notes for connecting to the CDC serial port.

Tests/gates:

- Firmware builds with USB CDC and SMP enabled.
- Host can request device info over USB CDC.
- Host can get, set, set-all, pulse, and off-all relays over USB CDC.
- Invalid RPC requests return structured errors without crashing firmware.

Dependencies:

- Phase 3 complete.
- USB device support verified for the RP2350 Zephyr target.

Deliverables:

- USB/RPC bring-up notes in `docs/testing/usb-rpc-smoke-test.md`.

## Phase 5: Python RPC Library

Purpose: provide the host API that hides packet framing and exposes typed relay
operations.

Implementation scope:

- Implement serial-port connection and optional explicit port selection.
- Implement SMP framing/parsing for the CDC serial transport.
- Implement CBOR encoding/decoding for relay commands.
- Implement sequence tracking, request timeouts, bounded retries, and response
  matching.
- Implement methods:
  - `get_info()`
  - `get_relays()`
  - `set_relay()`
  - `set_all_relays()`
  - `pulse_relay()`
  - `off_all()`
  - `get_status()`
  - `reboot()`
- Add typed exceptions for transport, timeout, protocol, validation, and
  device errors.
- Add simulated transports for unit tests without hardware.

Tests/gates:

- Unit tests cover packet encode/decode, CBOR payloads, response matching,
  timeouts, retries, structured device errors, and relay wrappers.
- Hardware smoke test controls all six relays through the Python API.

Dependencies:

- Phase 4 complete for hardware validation.
- Phase 3 protocol documentation stable enough for host implementation.

Deliverables:

- Host library package under `host/rp2350_relay_6ch/`.
- Library API documentation in `docs/host-library.md`.

## Phase 6: CLI Utility

Purpose: provide manual, scripted, and manufacturing-friendly relay workflows.

Implementation scope:

- Build CLI commands on top of the Python RPC library:
  - `info`
  - `get`
  - `set`
  - `set-all`
  - `pulse`
  - `off-all`
  - `status`
  - `reboot`
- Add serial port selection, timeout, retry, and output mode options.
- Support human-readable and machine-readable output.
- Return non-zero exit codes for argument, transport, timeout, protocol, and
  device failures.
- Add a hardware smoke-test command or script that cycles each relay safely.

Tests/gates:

- CLI tests cover argument validation, output modes, success paths with a fake
  transport, and failure exit codes.
- Hardware smoke test can switch all six relays and leave them off.
- CLI `status` exposes useful counters and last error data.

Dependencies:

- Phase 5 complete.

Deliverables:

- CLI entry point under `tools/`.
- Usage documentation in `docs/cli.md`.

## Phase 7: Firmware Upgrade Foundation

Purpose: add A/B update primitives without yet relying on automatic rollback
for product safety.

Implementation scope:

- Add MCUboot-compatible build configuration.
- Define flash partitions for bootloader, primary slot, secondary slot, and
  scratch/status storage as required by the chosen swap mode.
- Enable Zephyr flash map, DFU, and standard MCUmgr image management support.
- Ensure firmware images are signed as part of the documented build flow.
- Extend device info/status with boot slot and image state.
- Add minimum boot health checks:
  - relay GPIO subsystem initialized
  - all relays are in safe off state
  - RPC service initialized
  - boot/update state readable

Tests/gates:

- MCUboot image builds and signs successfully.
- Device boots signed application image.
- Image state can be queried over RPC.
- Relay defaults remain off with MCUboot enabled.

Dependencies:

- Phase 4 complete.
- Flash layout validated for RP2350 external flash.

Deliverables:

- Partition documentation in `docs/firmware-upgrade.md`.
- Signing/build script updates under `scripts/`.

## Phase 8: Host Firmware Upload And Rollback Workflow

Purpose: complete the PRD update workflow through the Python API and CLI.

Implementation scope:

- Add Python wrappers for standard MCUmgr image upload and image state:
  - `upload_firmware()`
  - `test_image()`
  - `confirm_image()`
  - `reboot()`
- Add CLI commands:
  - `upload`
  - `test-image`
  - `confirm-image`
  - `reboot`
- Implement automatic confirmation only after minimum health checks pass.
- Document and test interrupted upload, invalid image rejection, test boot,
  confirm, and rollback.

Tests/gates:

- Valid signed image uploads to inactive slot.
- Uploaded image can be marked pending/test and booted.
- Healthy image confirms itself after checks pass.
- Unconfirmed or unhealthy image rolls back to previous confirmed image.
- Interrupted upload does not replace or damage the running confirmed image.
- Invalid or unsigned image is rejected before test boot.

Dependencies:

- Phase 7 complete.
- Phase 5 and Phase 6 complete for host workflow.

Deliverables:

- End-to-end upgrade procedure in `docs/firmware-upgrade.md`.
- Recovery procedure in `docs/recovery.md`.
- Upgrade tests under `docs/testing/upgrade-tests.md`.

## Cross-Phase Documentation Checklist

- `docs/protocol/relay-management.md`: protocol version, group ID, command IDs,
  CBOR fields, response fields, and error codes.
- `docs/host-library.md`: Python API, exceptions, connection behavior, retry
  behavior, and examples.
- `docs/cli.md`: command usage, output modes, exit codes, and examples.
- `docs/firmware-upgrade.md`: signing, upload, test boot, confirmation, and
  rollback.
- `docs/recovery.md`: failed update and manual recovery procedures.
- `docs/testing/`: relay, USB RPC, host, CLI, and upgrade test procedures.
