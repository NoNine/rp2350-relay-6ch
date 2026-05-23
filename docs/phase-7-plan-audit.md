# Phase 7 Plan Audit

This document records Phase 7 planning snapshots and implementation notes for
traceability. Entries are historical planning snapshots, not implementation
approval.

Authoritative scope remains in the PRD, `docs/implementation-plan.md`, and active
phase plans. Do not treat an entry here as promoted scope unless it is later
accepted into one of those documents or implemented in code.

## 2026-05-23 - Pico 2 W DIY Indicator GPIO Assignment

- Status: implemented
- Request/context: Check whether the Pico 2 W DIY board has proper GPIO
  assignments for the planned buzzer and RGB LED, then plan the fixture
  assignment.
- Proposed plan: Keep the DIY relay overlay's six relay channels on GP2 through
  GP7. Add optional indicator fixture definitions using GP8 for WS2812 RGB LED
  data via PIO1 and GP9 for the passive buzzer PWM output via PWM slice 4B.
  Define the `led-strip` alias, `zephyr,user` PWM entry, required pinctrl
  states, `&pwm` configuration, and a `worldsemi,ws2812-rpi_pico-pio` node.
- Assumptions/defaults: GP8 and GP9 are available on the development fixture and
  are not connected to relay drivers. Pico 2 W Wi-Fi pins GP23, GP24, GP25, and
  GP29 remain avoided. The proposal documents a development-fixture convention,
  not a required DIY wiring standard.
- Follow-up: Implemented in `pico2w-relay-dev.overlay` and
  `docs/pico-diy-targets.md`.

## 2026-05-23 - Phase 7 Local Status Indicators

- Status: implemented
- Request/context: Implement Phase 7 local status indicators with Pico 2 W as
  the first verification target hardware, without reinventing new components or
  abstractions unless necessary.
- Assumptions/defaults: Pico 2 W verification uses the existing fixture overlay
  with RGB LED data on GP8 and buzzer PWM on GP9. Buzzer feedback remains
  disabled unless explicitly enabled at build time. No protocol, host library,
  or CLI command changes are part of Phase 7.
- Follow-up: Implemented in the Phase 7 firmware indicator module, firmware
  hooks, tests, and status-indicator documentation.

### Proposed Plan

#### Phase 7 Local Status Indicators

##### Summary

Implement one small firmware indicator module using the existing Phase 7
design: RGB LED via Zephyr `led_strip_update_rgb()` and passive buzzer via
Zephyr PWM. Keep relay/RPC behavior authoritative and unchanged. First hardware
verification target is Pico 2 W using the existing `pico2w-relay-dev.overlay`
fixture: WS2812 on `GP8`, buzzer PWM on `GP9`.

##### Key Changes

- Add app Kconfig options:
  - `CONFIG_RP2350_RELAY_6CH_INDICATORS=y` by default when LED strip/PWM
    support exists.
  - `CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK=n` by default, enabled only for
    explicit buzzer feedback builds/tests.
- Add `firmware/include/rp2350_relay_6ch/indicator.h` and
  `firmware/src/indicator.c` with the typed API already planned:
  - `indicator_init()`
  - `indicator_set_ready(bool ready)`
  - `indicator_set_relay_state(uint8_t state_mask, uint8_t pulse_mask)`
  - `indicator_record_command(enum indicator_command_result result)`
  - `indicator_set_degraded(bool degraded)`
  - `indicator_set_fault(bool fault)`
  - `indicator_set_reboot_pending(bool pending)`
- Implement the renderer as scheduled delayed work owned by the indicator
  module. It resolves priority as: fault, reboot pending, degraded/attention,
  accepted-command transient, relay-active, ready, booting, off.
- Use existing device bindings only:
  - RGB LED: `DEVICE_DT_GET(DT_ALIAS(led_strip))`
  - Buzzer: `PWM_DT_SPEC_GET(DT_PATH(zephyr_user))`
  - If a device is missing or not ready, log once and keep relay/RPC paths
    working.
- Hook existing code narrowly:
  - `main.c`: initialize indicators, show booting, set ready after relay init,
    set fault on relay init failure.
  - `relay.c`: after successful state/pulse changes and pulse expiry, publish
    current relay and pulse masks.
  - `relay_mgmt.c`: record accepted, invalid, busy, and reboot-pending command
    outcomes after the outcome is known.
- Do not change the MCUmgr command IDs, CBOR fields, protocol version, host
  library, or CLI command behavior.
- Update docs to match implementation:
  - `docs/status-indicators.md`: remove "planned/disabled" language and
    document quiet-by-default buzzer feedback.
  - `docs/phase-7-plan.md`: align Pico 2 W fixture pins with the existing
    overlay (`GP8`, `GP9`) instead of TBD.

##### Tests

- Add focused indicator firmware tests on `native_sim` for:
  - booting to ready
  - ready idle vs relay-active
  - accepted-command transient priority
  - invalid/busy attention priority
  - reboot-pending priority
  - fault highest priority
  - buzzer silent by default
  - explicit buzzer feedback mapping for accepted, rejected/busy, and reboot
    pending
- Keep existing relay and relay-management tests passing, with added assertions
  that indicator hooks do not change relay state, pulse expiry, `off-all`, or
  RPC responses.
- Build and run:
  - `west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay`
  - `build/firmware-tests/relay/zephyr/zephyr.exe`
  - `west build -s firmware/tests/relay_mgmt -b native_sim -d build/firmware-tests/relay-mgmt`
  - `build/firmware-tests/relay-mgmt/zephyr/zephyr.exe`
  - `scripts/test-host.sh`
- Build first hardware target:
  - `TARGET=pico2w RELAY_OVERLAY=firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay scripts/build-firmware.sh`
- Hardware smoke first on Pico 2 W:
  - verify boot/ready RGB indication
  - verify accepted command blink is bounded
  - verify relay-active color only while commanded on/pulsing
  - verify invalid/busy feedback is bounded and RPC remains responsive
  - run final `off-all` and confirm relay-active indication clears
- Also build Waveshare firmware after Pico 2 W passes to catch board
  integration issues:
  - `scripts/build-firmware.sh`

##### Assumptions

- Use the existing Pico 2 W fixture overlay as the first verification hardware
  definition.
- Keep buzzer feedback disabled in normal builds unless explicitly enabled by
  Kconfig.
- Do not add generic indicator registries, runtime configuration, host-visible
  indicator state, new protocol fields, or new abstractions beyond the single
  indicator module.
- Do not create or update a Phase 7 verification report unless explicitly
  requested after verification commands have actually run.

### Implemented Report

Implemented Phase 7 local status indicators and recorded the plan in
[docs/phase-7-plan-audit.md](/home/ubuntu/zephyrproject/apps/rp2350-relay-6ch/docs/phase-7-plan-audit.md).

#### What Changed

- Added a small firmware indicator module with RGB priority handling and
  quiet-by-default buzzer feedback.
- Hooked `main`, relay state changes, management command results, and reboot
  scheduling into the indicator API.
- Added app Kconfig options for indicators and explicit buzzer feedback.
- Added native-sim indicator tests, including buzzer-enabled mapping coverage.
- Updated Phase 7/status/CLI docs, including Pico 2 W fixture pins `GP8` and
  `GP9`.

#### Verification Passed

- `firmware/tests/indicator`: 8 passed, 1 skipped in quiet mode.
- `firmware/tests/indicator` with buzzer feedback enabled: 9 passed.
- `firmware/tests/relay`: 13 passed.
- `firmware/tests/relay_mgmt`: 13 passed.
- `scripts/test-host.sh`: 34 passed.
- Pico 2 W firmware build passed with `pico2w-relay-dev.overlay`.
- Waveshare firmware build passed.

The target builds still show the existing Zephyr `hwinfo_rpi_pico.c`
unused-variable warning. I did not run physical hardware smoke tests or create a
Phase 7 verification report.
