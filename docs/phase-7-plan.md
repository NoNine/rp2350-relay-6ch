# Phase 7 Plan: Local Status Indicators

## Summary

Phase 7 implements local controller status indication using the Waveshare
RP2350-Relay-6CH active-high buzzer and WS2812 RGB LED. The indicators provide
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

- Enable the buzzer on GPIO23 and WS2812 RGB LED on GPIO36 only for supported
  Waveshare targets that define those devices.
- Add a small firmware indicator module with:
  - RGB state-priority handling for boot, ready, command accepted,
    relay-active, degraded, reserved update/reboot, and fault states
  - bounded buzzer feedback for accepted operation, rejected command,
    reserved update/reboot transition, and local attention states
  - quiet default behavior unless buzzer feedback is explicitly enabled
- Feed indicator state from existing relay and management paths without
  changing the relay protocol wire format.
- Treat relay-active indication as commanded relay state only, not measured
  contact closure, load voltage, load current, or equipment health.
- Log indicator device errors but continue normal relay and RPC behavior.
- Keep `docs/status-indicators.md` aligned with implemented behavior.
- Add optional indicator checks to hardware smoke procedures without creating
  or updating a phase verification report automatically.

## Pico 2 W Development Fixture

Pico 2 W may be used as the Phase 7 development fixture before testing on
Waveshare RP2350-Relay-6CH hardware. This is a documentation note only; it does
not add firmware overlays, devicetree nodes, build configuration, or runtime
indicator behavior.

Fixture pin assignments must follow these constraints:

- Do not use Pico 2 W Wi-Fi pins GP23, GP24, GP25, or GP29.
- Do not collide with the current example relay overlay pins GP2 through GP7.
- Keep UART0 GP0 and GP1 available for debug console use.
- Keep RGB LED data and buzzer GPIOs separate from relay outputs.
- Record concrete fixture wiring as:
  - RGB LED data: `TBD by development fixture wiring`
  - Buzzer active-high output: `TBD by development fixture wiring`

## Acceptance Checks

Automated firmware checks:

```sh
west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay
build/firmware-tests/relay/zephyr/zephyr.exe
west build -s firmware/tests/relay_mgmt -b native_sim -d build/firmware-tests/relay-mgmt
build/firmware-tests/relay-mgmt/zephyr/zephyr.exe
```

Expected results:

- Existing relay and relay-management tests still pass.
- Indicator tests cover RGB state priority and buzzer event mapping.
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
- Waveshare target devicetree definitions for the active-high buzzer and WS2812
  RGB LED.
- Hardware available for final local indicator smoke testing.

## Deliverables

- Indicator firmware module and board configuration updates.
- Firmware tests for indicator priority and failure isolation.
- Updated operator manual in `docs/status-indicators.md`.
- Updated smoke-test procedure notes for optional local indicator checks.
