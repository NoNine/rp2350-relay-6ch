# RP2350 Relay Controller Implementation Plan

This plan turns the PRD in `docs/prd.md` into staged, testable work. The
first phases prioritize relay safety and relay control before host tooling and
firmware update flows across supported RP2350 relay targets.

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
- A phase is complete only after its acceptance tests pass, the relevant
  documentation is updated, and
  `docs/testing/phase-{phase-no}-verification.md` records the verification
  result.
- Do not create or update phase verification reports automatically during
  implementation. Write or update them only when explicitly requested, and
  include only verification commands and results that actually ran.
- Later phases may not start until dependencies from earlier phases are
  verified and their phase verification reports exist.
- Relay outputs must fail safe: all six channels default off on boot, reset,
  firmware restart, and test setup/teardown.
- Non-Waveshare RP2350 builds must provide explicit devicetree relay mappings
  for `ch1` through `ch6`; the protocol remains fixed at six channels.
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

- Historical Phase 0 builds used Raspberry Pi Pico 2 W
  (`rpi_pico2/rp2350a/m33/w`) as the temporary development target. Current
  relay hardware builds use `waveshare_rp2350_relay_6ch/rp2350b/m33`.

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

## Phase 7: Local Status Indicators

Purpose: add controller-level RGB LED and buzzer feedback as the next product
phase without changing relay safety semantics or host RPC authority.

Implementation scope:

- Use `docs/phase-7-plan.md` as the authoritative implementation plan for the
  indicator API, state model, hardware binding, event sources, and tests.
- Enable the Waveshare WS2812 RGB LED and passive PWM-driven buzzer only for
  targets that define the hardware.
- Keep buzzer feedback quiet by default or explicitly configurable, with no
  continuous alarm unless a timeout or silence policy is implemented.
- Ensure indicator errors are logged but never block relay control, `off-all`,
  pulse teardown, reboot handling, or RPC responses.
- Keep the status-indicator manual in `docs/status-indicators.md` aligned with
  implemented operator-visible behavior.

Tests/gates:

- Firmware tests cover RGB state priority and buzzer event mapping.
- Relay and relay-management tests still pass with indicators enabled.
- Hardware smoke testing verifies boot, ready, command-accepted, relay-active,
  rejected-command, update/reboot, and fault/attention indications where those
  states are available.

Dependencies:

- Phase 6 complete for host-visible command and status workflows.
- Indicator behavior documented before release.

Deliverables:

- Indicator firmware module and target configuration.
- Operator manual in `docs/status-indicators.md`.
- Indicator checks added to hardware smoke-test procedures.

## Phase 8a: Cross-Platform Session Mode

Purpose: add the cross-platform manual/direct host-control workflow before
firmware upgrade work, independent of any future firmware communication-loss
safety.

Implementation scope:

- Use `docs/phase-8a-plan.md` as the concise phase plan and
  `docs/host-session-mode.md` as the decision-complete implementation contract
  for session behavior, CLI shape, discovery, compatibility expectations, and
  tests.
- Add `rp2350-relay session` as a long-lived direct-serial session that owns
  one selected serial connection while connected and serializes relay commands
  through one direct `RelayClient` connection.
- Support explicit `--port`, exact `--serial`, and interactive USB discovery
  and selection for session startup.
- Reuse the existing direct CLI command surface, one-based channel arguments,
  host-side validation rules, output conventions, and typed exception mapping
  where practical.
- Keep one-shot `rp2350-relay --port COM7 <command>` behavior available for
  diagnostics and simple checks.
- Allow only the dummy firmware `heartbeat` command for session polling. Do not
  require communication-loss timeout commands or firmware safety behavior in
  this phase.

Tests/gates:

- Host tests cover session command parsing, command dispatch, typed error
  handling, USB discovery, one-connection session ownership, safe close
  behavior, reconnect behavior, and compatibility with existing one-shot direct
  commands.
- Existing direct host library and CLI tests still pass.
- Manual hardware smoke testing can run session mode from Windows and Linux and
  leave relays off after manual teardown.

Dependencies:

- Phase 6 complete for the direct host library and CLI command surface.
- Phase 7 complete before this phase starts under the current phase order.

Deliverables:

- Session command under the existing host CLI package.
- Cross-platform session usage documentation.
- Cross-platform session smoke-test procedure under `docs/testing/`.

## Phase 8b: Host Daemon Mode

Purpose: add the production Linux host-control architecture before firmware
upgrade work, independent of any future firmware communication-loss safety.

Implementation scope:

- Use `docs/phase-8b-plan.md` as the authoritative Phase 8b implementation plan
  for the daemon lifecycle, local IPC, client API, CLI behavior, reconnect
  handling, shutdown policy, and tests.
- Add `rp2350-relayd` as a long-running foreground-capable daemon that owns one
  selected relay controller, serializes relay commands, and handles reconnect
  after device reboot or USB reset.
- Add `rp2350-relayctl` as the short-lived daemon-client CLI while keeping
  `rp2350-relay` as the direct serial diagnostic CLI.
- Add a Python daemon client API alongside the existing direct `RelayClient`.
- Use newline-delimited JSON over an explicit same-user Unix domain socket.
- Ship a `systemd --user` unit for production operation.
- Do not require firmware heartbeat, communication-loss timeout commands, or
  new firmware protocol fields in this phase.

Tests/gates:

- Host tests cover daemon request parsing, response formatting, command
  serialization, client behavior, CLI behavior, startup state query, clean
  shutdown, no-client relay-state policy, and simulated reconnect.
- Existing direct host library and CLI tests still pass.
- Manual hardware smoke testing can run daemon mode from Linux without root and
  leave relays off after clean shutdown.

Dependencies:

- Phase 6 complete for the direct host library and CLI command surface.
- Phase 7 complete before this phase starts under the current phase order.

Deliverables:

- Daemon, daemon client, and daemon-client CLI under the host tooling package.
- User service/unit documentation and daemon usage documentation.
- Daemon smoke-test procedure under `docs/testing/`.

## Phase 9: Firmware Upgrade Foundation

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
- Phase 8a and Phase 8b complete for Windows and Linux production
  host-control workflows.
- Flash layout validated for RP2350 external flash.

Deliverables:

- Partition documentation in `docs/firmware-upgrade.md`.
- Signing/build script updates under `scripts/`.

## Phase 10: Host Firmware Upload And Rollback Workflow

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
- Phase 9 complete.
- Phase 5 and Phase 6 complete for direct host workflow.

Deliverables:

- End-to-end upgrade procedure in `docs/firmware-upgrade.md`.
- Recovery procedure in `docs/recovery.md`.
- Upgrade tests under `docs/testing/upgrade-tests.md`.

## Cross-Phase Documentation Checklist

- `docs/protocol/relay-management.md`: protocol version, group ID, command IDs,
  CBOR fields, response fields, and error codes.
- `docs/host-library.md`: Python API, exceptions, connection behavior, retry
  behavior, and examples.
- `docs/cli.md`: direct and daemon-client command usage, output modes, exit
  codes, and examples.
- Daemon documentation: user service operation, local socket path, lifecycle,
  reconnect behavior, and daemon-mode limitations.
- `docs/status-indicators.md`: RGB LED meanings, buzzer patterns, limitations,
  and troubleshooting.
- `docs/firmware-upgrade.md`: signing, upload, test boot, confirmation, and
  rollback.
- `docs/recovery.md`: failed update and manual recovery procedures.
- `docs/testing/`: relay, USB RPC, host, CLI, and upgrade test procedures.
