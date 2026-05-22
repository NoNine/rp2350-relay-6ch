# RGB LED and buzzer use scenarios

Date: 2026-05-22

Status: Discussion. This note records product investigation and a recommended
direction for the optional RGB LED and buzzer. It does not change the current
authoritative PRD, implementation plan, phase scope, or verification status
unless those documents are updated explicitly.

## Summary

Use the WS2812 RGB LED and buzzer as local diagnostics for real operator
problems: "Is it alive?", "Is it safe?", "Is it connected?", "Did my command
work?", and "Does this need attention?" They should not replace host-visible
status, claim measured relay or load feedback, or add incidental firmware
complexity.

## Research findings

- Industrial color convention: follow IEC-style meanings: green normal, yellow
  abnormal or attention, red emergency or fault, blue action required, and
  white or neutral when unsure. Source:
  <https://us.idec.com/RD/safety/law/iso-iec/iec60204>
- I/O modules commonly show output state separately from diagnostics; Rockwell
  documents steady yellow for output on and flashing red for field or load
  faults. Source:
  <https://literature.rockwellautomation.com/idc/groups/literature/documents/um/5069-um004_-en-p.pdf>
- Managed PDUs warn that outlet LEDs may mean relay coil energized, not actual
  output voltage. This directly matches this project: report commanded relay
  state, not verified load power. Source:
  <https://www.apc.com/us/en/faqs/FA234961/>
- Similar relay and I/O products use LEDs for online/offline, encrypted
  communications, upgrade, and overcurrent or undervoltage faults. Source:
  <https://help.axis.com/en-us/axis-a9910>
- Industrial signal towers pair colors with audible alerts for local attention,
  not routine noise. Sources:
  <https://www.bannerengineering.com/us/en/products/lighting-and-indicators/tower-lights/tl15-in-line-series.html>,
  <https://www.phoenixcontact.com/en-pc/products/optical-element-psd-s-oe-led-mc-2702090>,
  and <https://www.werma.com/us/products/signal-towers-stack-lights/esign/>
- Annunciators commonly provide horn silence, lamp test, configurable labels,
  and local horn output. Source:
  <https://fr.comap-control.com/products/accessories/i-o-modules/em2iglrabaa/>

## Recommended behavior

- RGB LED owns controller-level state only:
  - Off: no firmware-controlled indication yet.
  - Dim white: boot or initializing, relays held off.
  - Solid green: ready, RPC available, no active relay outputs.
  - Green with brief blink: valid host command accepted.
  - Solid cyan or blue: one or more relays commanded on, or pulse active.
  - Yellow pulse: degraded or attention state such as RPC not ready, busy pulse
    rejection, invalid command burst, or update pending.
  - Purple or blue animation: firmware upload, test image, or update in
    progress.
  - Red blink: firmware or hardware fault; relays should be off unless the
    fault occurs after an explicit command state.
- Buzzer is sparse and operator-facing:
  - One short beep: local command or operation accepted, only when explicitly
    enabled.
  - Two short beeps: command rejected or validation error.
  - Three short beeps: firmware update stage transition or reboot scheduled.
  - Repeating chirp, time-limited: fault needing local attention.
  - No beep for normal heartbeat, idle ready state, every relay toggle by
    default, or continuous alarm without a timeout or silence policy.
- Add a host/API configuration later for quiet mode, buzzer enable, brightness,
  and lamp/buzzer test. Default should be quiet enough for lab and server-room
  use.

## Repo integration

- Current PRD already allows the active-high buzzer on GPIO23 and WS2812 RGB
  LED on GPIO36 if relay control and RPC are unaffected.
- Hardware notes confirm the active-high buzzer and RGB LED hardware.
- Devicetree currently defines both outputs but disables them.
- First implementation step should be a short design doc or PRD/implementation
  plan update assigning ownership, because the remaining-features review
  explicitly marks this undecided.

## Test plan

- Firmware unit tests for state-priority mapping: fault beats update, update
  beats degraded, degraded beats relay-active, ready is lowest.
- Tests must verify indicator failures never affect relay control, pulse
  timing, off-all, reboot, or RPC responses.
- Hardware smoke test: boot LED, ready LED, command blink, relay-active color,
  rejected command beep, update/reboot indication, and final all-off.
- Documentation must state that LED relay-active means commanded state only,
  not verified contact closure or load power.

## Assumptions

- V1 keeps this local-only: no network alerts, no telemetry claims, and no
  persistent relay-on state.
- The RGB LED is a single device, so it cannot truthfully show six independent
  relay states at once.
- The buzzer defaults to disabled or minimal feedback to avoid nuisance alarms
  in industrial and server environments.
