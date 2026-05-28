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

### Pixel Floorplan And Attributes

Render the watch-style UI on a `128x64` 1-bit SSD1306 framebuffer. Coordinates
below are logical OLED pixels, with `(0, 0)` at the top-left corner. Use
1 px strokes and fixed bitmap glyphs around `5x7`, rendered without
antialiasing.

```text
x=0                                                        x=127
y=0  +--------------------------------------------------------+
     |                                                        |
y=3  |   USB RDY ACT PLS ERR                                 |
y=13 |   --------------------------------------------------   |
y=18 |  [1]  [2]  [3]  [4]  [5]  [6]                         |
y=44 |                                                        |
y=50 |   --------------------------------------------------   |
y=54 |   BOOT/READY/ACTIVE/ATTN/FAULT/REBOOT    OK/P2/E:IO   |
y=63 +--------------------------------------------------------+
```

| Element | Attributes |
| --- | --- |
| Canvas | `128x64` px, monochrome 1-bit framebuffer |
| Coordinate origin | `(0, 0)` at top-left |
| Top annunciators | `x=3..124`, `y=3..9`, short uppercase labels such as `USB RDY ACT PLS ERR` |
| Top divider | `x=3..124`, `y=13`, 1 px high |
| Relay field | `y=18..44`, six fixed cells |
| Bottom divider | `x=3..124`, `y=50`, 1 px high |
| Bottom status text | `x=3..124`, `y=54..60`, mode label left-aligned and detail code right-aligned |
| Font | fixed bitmap glyphs around `5x7`, no antialiasing |
| Strokes and borders | 1 px |
| Relay cell size | `17x27` px |
| Relay cell border | 1 px |
| Relay cell origins | `(3,18)`, `(24,18)`, `(45,18)`, `(66,18)`, `(87,18)`, `(108,18)` |
| Relay cell pitch | 21 px |
| Relay digit | centered within the cell using the same fixed bitmap font |
| Off relay | outline cell with normal on-pixel digit |
| Active relay | filled cell with reversed digit |
| Pulsing relay | active relay rendering plus two small `2x2` reversed blocks near the top-right inside the cell |
| Pulse countdown | when pulse timing is available, a `15x2` px center-drain bar in `x=cell_x+1..cell_x+15`, `y=46..47` under each pulsing cell |

Relay cell `x` placement is:

```c
x = 3 + channel_index * 21; /* channel_index is 0..5 */
```

Use these renderer constants unless implementation constraints require an
equivalent layout:

```c
#define OLED_W 128
#define OLED_H 64

#define UI_TOP_TEXT_Y 3
#define UI_TOP_RULE_Y 13
#define UI_CELL_X0 3
#define UI_CELL_Y 18
#define UI_CELL_W 17
#define UI_CELL_H 27
#define UI_CELL_PITCH 21
#define UI_PULSE_BAR_Y 46
#define UI_PULSE_BAR_H 2
#define UI_PULSE_BAR_W 15
#define UI_BOTTOM_RULE_Y 50
#define UI_STATUS_Y 54

#define UI_RULE_X0 3
#define UI_RULE_X1 124
#define UI_TEXT_X0 3
#define UI_TEXT_X1 124
#define UI_LINE_W 1
```

Keep `y=45`, `y=48`, and `y=49` clear around the pulse countdown bar so the
bar does not merge visually with relay cell borders or the bottom divider.

The preview assets use dim and bright OLED tones for readability. On a 1-bit
SSD1306 framebuffer, render both as normal on pixels unless a later renderer
explicitly adopts dithering.

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
- Pulsing: same as on, plus two small `2x2` reversed blocks near the
  top-right inside the cell.

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
