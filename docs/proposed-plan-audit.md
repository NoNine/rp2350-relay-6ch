# Proposed Plan Audit Log

This document records proposed implementation plans for traceability. Entries are
historical planning snapshots, not implementation approval.

Authoritative scope remains in the PRD, `docs/implementation-plan.md`, and active
phase plans. Do not treat an entry here as promoted scope unless it is later
accepted into one of those documents or implemented in code.

## Status Values

- `proposed`: Plan was suggested but not yet accepted or implemented.
- `revised`: Plan was replaced by a later version.
- `accepted`: Plan was approved for implementation.
- `superseded`: Plan was made obsolete by a different decision.
- `implemented`: Plan was implemented.

## Entry Template

```md
## YYYY-MM-DD - Short Title

- Status: proposed
- Request/context:
- Proposed plan:
- Assumptions/defaults:
- Follow-up:
```

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
