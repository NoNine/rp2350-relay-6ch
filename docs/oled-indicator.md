# OLED Indicator Contract

Date: 2026-05-27

Status: Standalone implementation contract. This OLED add-on is not assigned
to any implementation phase. Do not update phase plans, the PRD, protocol
docs, host docs, or phase verification reports for this contract unless the
user explicitly requests that broader promotion.

## Summary

The OLED indicator is an optional local status display for the relay
controller. It mirrors firmware-owned controller and relay facts that already
drive local indicators, but it is not a relay control surface and it is not an
authoritative source for automation.

Host-visible command responses and status queries remain authoritative. OLED
hardware detection, POST state, and write failures remain firmware-internal
until device-originated asynchronous events are implemented and promoted.

The first supported hardware target is the Raspberry Pi Pico 2 or Pico 2 W
relay development fixture with a 3.3 V, 128x64, SSD1306-compatible I2C OLED at
address `0x3d`.

## Hardware Contract

For the Pico relay development fixture:

- Use `i2c1` on GP10 and GP11 for the OLED.
- Keep UART0 on GP0 and GP1.
- Keep relay development channels on GP2 through GP7.
- Keep the RGB LED fixture on GP8.
- Keep the buzzer fixture on GP9.
- Avoid Pico 2 W Wi-Fi-sensitive pins documented in `docs/pico-diy-targets.md`.

The OLED is optional even when display-capable firmware is built. A missing
OLED must not prevent firmware boot, relay initialization, RPC handling, pulse
teardown, `off-all`, or reboot handling.

## Display UI Contract

Use a fixed 128x64 layout with three stable bands:

- Top annunciator band: compact condition labels such as `USB`, `RDY`, `ACT`,
  `PLS`, and `ERR`.
- Main relay field: six stable relay cells, one per channel.
- Bottom status band: a short mode label and terse detail code.

Recommended mode labels:

- `BOOT`: firmware is initializing.
- `READY`: controller is ready and no relay is active.
- `ACTIVE`: one or more relays are commanded on or pulsing.
- `ATTN`: degraded state or recent rejected/busy command.
- `FAULT`: firmware or hardware fault.
- `REBOOT`: controlled reboot or update-related hold state.

Recommended detail labels:

- `OK`: no current attention detail.
- `P1` through `P6`: a single channel is pulsing.
- `P*`: multiple channels are pulsing.
- `E:ARG`: invalid argument or validation error.
- `E:BUSY`: pulse rejected because a channel is busy.
- `E:IO`: relay, display, or indicator I/O problem.
- `HOLD`: reboot/update pending; avoid removing power.

Relay cells show commanded firmware state only:

- Off: outlined cell with the channel number.
- On: filled or inverted cell with the channel number reversed.
- Pulsing: same as on, plus a small pulse mark in the cell.

Do not scroll. Do not draw explanatory sentences. Do not use animations except
bounded blink markers for pulse or attention. Rate-limit display refreshes so
relay control and pulse timing remain primary.

## Firmware Integration Contract

The OLED renderer belongs with the existing indicator subsystem. Relay control,
management RPC, and startup code continue publishing domain facts through the
typed indicator APIs:

- `indicator_set_ready(bool ready)`
- `indicator_set_relay_state(uint8_t state_mask, uint8_t pulse_mask)`
- `indicator_record_command(enum indicator_command_result result)`
- `indicator_set_degraded(bool degraded)`
- `indicator_set_fault(bool fault)`
- `indicator_set_reboot_pending(bool pending)`

The display is a local output backend next to the RGB LED and buzzer. Relay and
RPC paths must not perform OLED I/O directly.

Render priority follows the local indicator model:

1. Fault.
2. Reboot or update attention.
3. Degraded or command attention.
4. Relay active or pulse active.
5. Ready.
6. Booting.
7. Off, unsupported, or unavailable.

Add display support behind a firmware Kconfig option such as
`CONFIG_RP2350_RELAY_6CH_DISPLAY`. Enable Zephyr `DISPLAY`, `SSD1306`, and the
minimum framebuffer/text drawing support only when the display feature is
selected.

## Detection And POST Contract

Use these internal display backend states:

- `unsupported`: display feature disabled or no `zephyr,display` node exists.
- `not_detected`: display support is built, but no ready OLED is found at boot.
- `ready`: POST passed and later render writes have not failed.
- `failed`: POST or a later render write failed.

`not_detected` is normal for display-capable firmware without the optional
add-on. It must not set controller degraded or fault state.

POST success requires all of the following:

- Display feature is enabled.
- A `zephyr,display` device exists.
- `device_is_ready()` returns true for that device.
- Display capabilities match the expected 128x64 monochrome use.
- `display_blanking_off()` succeeds.
- `display_write()` successfully writes a page-aligned 8-pixel-high diagnostic
  buffer, such as an 8x8 or 16x8 fixed bit pattern at `(0, 0)`.

The POST diagnostic write is not a splash screen:

- Use a fixed simple bit pattern.
- Do not delay boot for operator visibility.
- Do not require operator confirmation.
- Let normal display rendering overwrite the diagnostic pixels on the next
  display update.
- Treat success as command/data write-path acknowledgement only.

POST cannot prove that OLED pixels are lit, the panel is visible, the panel
glass is intact, the relay contacts closed, load voltage exists, or current is
flowing.

## Failure Contract

Use fail-until-reboot behavior for display failures:

- If POST fails, record `failed`, log once or rate-limit logs, and skip future
  OLED writes until reboot.
- If a later render write fails, record `failed`, log once or rate-limit logs,
  and skip future OLED writes until reboot.
- Do not retry every render.
- Do not implement OLED hot-plug recovery in the first implementation.

OLED failure handling must never block relay control, `off-all`, pulse
teardown, reboot handling, or RPC responses. OLED health must not alter RGB LED
or buzzer state unless a later explicit indicator-health policy is adopted.

Do not add OLED health to `status`, `info`, host CLI output, host library APIs,
daemon status, or protocol docs in this contract. Host-visible display
diagnostics are intentionally deferred until device-originated asynchronous
events are implemented and promoted. At that point, represent OLED failures as
an event such as `indicator_fault` or a display-specific event payload rather
than adding polling-only status fields first.

## Safety Wording

The OLED shows commanded firmware state only. A filled channel cell means the
firmware commanded that relay output on, or a pulse is active for that channel.

It does not prove:

- Relay contact closure.
- Load voltage.
- Current flow.
- External equipment state.
- Isolated relay-side supply health.
- OLED pixel visibility.

Host-visible command responses, logs, and safe test procedures remain the
source of truth for scripts and troubleshooting.

## Test Contract

Future implementation tests should cover:

- Internal `unsupported`, `not_detected`, `ready`, and `failed` display states.
- Successful diagnostic POST write.
- Simulated bad geometry, blanking failure, and diagnostic write failure.
- Render write failure entering fail-until-reboot state.
- No repeated OLED writes after fail-until-reboot failure.
- Ready state rendering `READY` with no filled relay cells.
- Relay channel 1 filling only cell `1`.
- Multiple relay channels filling the matching cells.
- Pulse on channel 2 rendering `P2`.
- Multiple pulses rendering `P*`.
- Busy or invalid command attention rendering `ATTN` and an error detail.
- Fault overriding relay-active and ready display state.
- Reboot/update attention overriding relay-active but not fault.

Build checks should include:

- Display-enabled Pico 2 or Pico 2 W relay fixture build.
- Display-absent or display-disabled target build.
- Existing relay, relay-management, indicator, protocol, and host tests
  unchanged unless implementation later changes their behavior.

Do not create or update a phase verification report for OLED work unless the
user explicitly requests one.

## Related Docs

- `docs/discussions/oled-indicator-ui.md` records the design discussion that
  led to this contract.
- `docs/discussions/indicator-api-design.md` records why the indicator module
  uses typed product-domain APIs.
- `docs/discussions/device-originated-smp-events.md` records the future async
  event foundation that must exist before host-visible display diagnostics are
  promoted.
- `docs/status-indicators.md` remains the operator-facing source for RGB LED
  and buzzer behavior.
- `docs/pico-diy-targets.md` documents Pico 2 and Pico 2 W relay fixture
  wiring and build flow.
