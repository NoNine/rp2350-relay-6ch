# Phase 7 Plan: Local Status Indicators

## Summary

Phase 7 implements local controller status indication using the Waveshare
RP2350-Relay-6CH passive buzzer and WS2812 RGB LED. The indicators provide
operator feedback for controller state, command results, relay-active state,
degraded conditions, update/reboot activity, and faults.

Relay control and host-visible RPC responses remain authoritative. Indicator
behavior must never block relay operations, `off-all`, pulse teardown, reboot
handling, or RPC responses.

Firmware-update-specific indicator states may be reserved in Phase 7, but
Phase 7 must not implement MCUboot, image upload, test-image, confirmation,
rollback, or firmware-update protocol behavior. Firmware upgrade foundation is
planned for Phase 8, and host upload/rollback workflows are planned for
Phase 9.

## Implementation

Add a small firmware indicator module under the application firmware tree. The
module owns indicator state, timing, and hardware I/O; relay and management
paths only publish state or event changes.

Public firmware API:

- `indicator_init()`
- `indicator_set_ready(bool ready)`
- `indicator_set_relay_state(uint8_t state_mask, uint8_t pulse_mask)`
- `indicator_record_command(enum indicator_command_result result)`
- `indicator_set_degraded(bool degraded)`
- `indicator_set_fault(bool fault)`
- `indicator_set_reboot_pending(bool pending)`

The API is intentionally typed and domain-specific instead of a generic
condition or LED-group assertion interface. Callers publish the domain facts
they own: `main` publishes readiness and faults, `relay.c` publishes relay and
pulse state, and `relay_mgmt.c` publishes command outcomes and reboot-pending
state. The indicator module maps those facts to logical indicator conditions,
resolves priority, handles transient timing, and performs hardware I/O. This
keeps persistent conditions separate from one-shot command events, limits API
misuse, and borrows OpenBMC's logical-group priority model without adding a
generic group registry, string IDs, or runtime indicator configuration.

State model:

- Maintain persistent state bits for booting, ready, relay-active, degraded,
  reserved update/reboot, and fault.
- Maintain transient command events separately from persistent state. Accepted
  commands produce a brief green blink. Rejected, invalid, and busy commands
  produce bounded attention feedback.
- Drive RGB and buzzer output from one delayed-work renderer. Callers must not
  perform LED or buzzer I/O directly, and indicator work must not run while
  holding relay locks or generating MCUmgr responses.
- Log indicator device errors with rate limiting or first-failure suppression,
  then continue normal relay and RPC behavior.

RGB priority, highest first:

1. Fault: red blink.
2. Reserved update/reboot: purple or blue animation.
3. Degraded or attention: yellow pulse.
4. Accepted command: brief green blink.
5. Relay-active: solid cyan or blue.
6. Ready idle: solid green.
7. Booting: dim white.
8. Disabled or unavailable: off.

Buzzer policy:

- Keep buzzer feedback disabled by default behind a build-time Kconfig option.
- When explicitly enabled, use one short beep for accepted operations, two short
  beeps for rejected or busy commands, and three short beeps for reserved
  reboot/update transitions.
- Any repeating attention chirp must be time-limited. Do not add a continuous
  alarm without a silence policy.

Hardware and integration:

- Enable indicators only for supported targets that define the devices.
  Waveshare uses the buzzer on GPIO23 and WS2812 RGB LED on GPIO36; the Pico 2
  W development fixture uses buzzer PWM on GP9 and WS2812 RGB LED on GP8.
- Model the buzzer as a passive PWM-driven device. Silent means PWM output
  disabled or zero duty; audible feedback uses bounded tone windows.
- Use Zephyr `led_strip_update_rgb()` for the WS2812 RGB LED.
- Use Zephyr's RP2350-compatible `worldsemi,ws2812-rpi_pico-pio` binding for
  the RGB LED, with an enabled PIO node and a pinctrl group for `PIO*_P36`.
  Do not use `worldsemi,ws2812-gpio` on RP2350; Zephyr's GPIO bit-bang driver
  is not the RP2350 path.
- Keep the target-specific RGB LED and buzzer GPIOs dedicated to indicators. Do
  not assign relay GPIO26 through GPIO31 to alternate functions.
- Add the required LED strip and PIO configuration only for targets that support
  the hardware.

Event sources:

- `main` initializes the indicator module, shows booting while firmware starts,
  shows ready after relay initialization succeeds, and shows fault if relay
  initialization fails.
- `relay.c` publishes relay-active state after successful relay state changes,
  `off-all`, pulse start, and pulse expiry.
- `relay_mgmt.c` records accepted, invalid, busy, and reboot-pending command
  events after the command outcome is known.
- Do not change the relay protocol wire format, host command responses, or host
  library behavior in Phase 7.

Documentation and smoke tests:

- Treat relay-active indication as commanded relay state only, not measured
  contact closure, load voltage, load current, or equipment health.
- Keep `docs/status-indicators.md` aligned with implemented operator-visible
  behavior.
- Add optional indicator checks to hardware smoke procedures without creating
  or updating a phase verification report automatically.

## Design Notes

Open-source implementations suggest keeping status indication as a derived,
nonblocking presentation layer:

- PX4's LED controller keeps per-priority LED state and renders output from a
  periodic controller instead of doing hardware work at each event source.
- ArduPilot's notification library separates persistent flags from one-shot
  events, then fans updates out to LED and buzzer backends.
- ESPHome's status LED derives hardware output from compact application status
  bits and updates it from a scheduled loop.
- Meshtastic's status LED and external-notification modules reinforce
  feature-gated, time-bounded indicator behavior rendered from a scheduled
  worker or thread instead of direct hardware writes at each event source. Its
  buzzer mode checks support Phase 7's default of quiet output unless feedback
  is explicitly enabled.
- OpenBMC's LED manager reinforces treating indicator conditions as asserted
  logical groups resolved by priority before physical LED I/O. Its retry and
  error paths support logging or rate-limiting physical LED write failures
  without changing the authoritative controller state.
- Zephyr's LED and PWM APIs provide the hardware boundary for this project:
  `led_strip_update_rgb()` for the WS2812 RGB LED and `pwm_set_dt()` for the
  passive buzzer tone output.

## Pico 2 W Development Fixture

Pico 2 W is the first Phase 7 hardware verification fixture before testing on
Waveshare RP2350-Relay-6CH hardware. The repository already provides the
fixture devicetree overlay at
`firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay`.

Fixture pin assignments must follow these constraints:

- Do not use Pico 2 W Wi-Fi pins GP23, GP24, GP25, or GP29.
- Do not collide with the current example relay overlay pins GP2 through GP7.
- Keep UART0 GP0 and GP1 available for debug console use.
- Keep RGB LED data and buzzer GPIOs separate from relay outputs.
- Use the existing fixture wiring:
  - RGB LED data: `GP8` via PIO1.
  - Buzzer PWM output: `GP9` via PWM slice 4B.

## Acceptance Checks

Automated firmware checks:

```sh
west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay
build/firmware-tests/relay/zephyr/zephyr.exe
west build -s firmware/tests/relay_mgmt -b native_sim -d build/firmware-tests/relay-mgmt
build/firmware-tests/relay-mgmt/zephyr/zephyr.exe
west build -s firmware/tests/indicator -b native_sim -d build/firmware-tests/indicator
build/firmware-tests/indicator/zephyr/zephyr.exe
```

Expected results:

- Existing relay and relay-management tests still pass.
- Indicator tests cover RGB state priority and buzzer event mapping, including
  booting to ready, relay-active, accepted-command blink, degraded, reserved
  reboot/update, and fault priority.
- Indicator tests cover quiet buzzer default behavior and explicit buzzer
  feedback for accepted, rejected, busy, and reboot-pending events.
- Indicator device failures do not affect relay state transitions, pulse
  teardown, `off-all`, reboot scheduling, or RPC response generation.

Automated host checks:

```sh
scripts/test-host.sh
```

Expected results:

- Host library and CLI behavior remains compatible with the existing relay
  protocol.
- No host tests require local RGB LED or buzzer hardware.

Manual hardware smoke check:

```sh
rp2350-relay --port <serial-port> smoke
```

Expected hardware results:

- Boot and RPC ready indications match `docs/status-indicators.md`.
- Valid relay commands produce only bounded command feedback.
- Relay-active indication appears only while one or more relays are commanded
  on or pulsing.
- Invalid command feedback is bounded and the device remains responsive.
- Firmware-update-specific indications are not required for Phase 7 hardware
  acceptance because firmware update is planned for Phase 8 and Phase 9.
- Final `off-all` clears relay-active indication and leaves all relays off.

## Dependencies

- Phase 6 complete and verified.
- Waveshare target devicetree definitions for the passive PWM-driven buzzer
  and WS2812 RGB LED.
- Hardware available for final local indicator smoke testing.

## Deliverables

- Indicator firmware module and board configuration updates.
- Firmware tests for indicator priority and failure isolation.
- Updated operator manual in `docs/status-indicators.md`.
- Updated smoke-test procedure notes for optional local indicator checks.
