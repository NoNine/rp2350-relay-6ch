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

### Audible buzzer pattern research

Standards give useful constraints for audible indications, but they do not
define a universal "accepted command", "warning", or "fault" buzzer catalog for
small relay controllers. The consistent lesson is that audible indications must
be recognizable, bounded, appropriate to the site, and not confused with
emergency evacuation signals.

- ISO 7731 covers auditory danger signals for public and work areas. It focuses
  on audibility, recognition, and test methods for danger signals rather than
  prescribing a fixed equipment-feedback pattern table. Source:
  <https://www.iso.org/standard/33590.html>
- IEC/EN 60073 defines coding principles for indicators and actuators,
  including visual, acoustic, and tactile signals. It supports using
  characteristics such as intermittency, sound, and duration as meaningful
  codes, but still expects the equipment designer to assign meanings
  consistently. Source:
  <https://standards.iteh.ai/catalog/standards/clc/74016ce4-0b01-49c8-b265-33812d04ea68/en-60073-2002>
- IEC 60204-1 treats audible and visual warning before hazardous machine start
  as a risk-assessment topic. That fits pre-start or motion-warning use better
  than routine relay command feedback. Source:
  <https://preview.sist.si/sist-preview/103808/4a7e421d8a614886a504a6e95bf138fb/IEC-60204-1-2016-AMD1-2021.pdf>
- ISO 8201 defines an audible emergency evacuation signal. Its evacuation
  pattern should remain reserved for evacuation or emergency systems, not reused
  for normal controller warnings or command feedback. Source:
  <https://www.iso.org/standard/67046.html>
- OSHA 29 CFR 1910.165 requires employee alarm systems to provide recognizable
  warning for emergency action or safe escape and reaction time. This reinforces
  keeping emergency alarms distinct from local equipment chirps. Source:
  <https://www.osha.gov/etools/evacuation-plans-procedures/emergency-standards/employee-alarms/>
- ISA-18.1 and ISA-18.2 are useful alarm-system references for consistency,
  acknowledgement, silencing, operator response, and avoiding nuisance alarms.
  They are most relevant if this project later grows an annunciator or alarm
  philosophy, not for turning every relay event into a buzzer event. Source:
  <https://www.isa.org/standards-and-publications/isa-standards/isa-18-series-of-standards>

Well-known industrial signaling products converge on a small vocabulary:
continuous tone, simple pulse, faster pulse, chirp, double-pulse, siren, and
acknowledged/silenced states. The relay controller should use only the quiet
subset needed for local feedback.

- PATLITE product documentation includes examples such as 250 ms on/off,
  500 ms on/off, double-pulse timing, and continuous on. Source:
  <https://www.manualsdir.com/manuals/373122/patlite-nhl.html?page=22>
- Siemens SIRIUS 8WD4 audible elements support continuous or pulsating tones.
  Source:
  <https://support.industry.siemens.com/cs/attachments/109758131/manual_SIRIUS_signal_columns_en-US.pdf>
- Rockwell Automation / Allen-Bradley 855T audible modules support continuous
  and pulse-tone operation with selectable sound levels. Source:
  <https://www.rockwellautomation.com/en-us/products/details.855T-B24TA2.html>
- Banner TL70 audible modules document pulse, chirp, siren, and continuous
  alarm modes, with intensity options. Source:
  <https://info.bannerengineering.com/cs/groups/public/documents/literature/182214.pdf>
- Eaton M22 audible indicators are offered as distinct continuous-tone and
  pulsed-tone buzzer devices; one pulsed model documents 2300 Hz and 83 dB.
  Source:
  <https://www.eaton.com/us/en-us/skuPage.229028.html>

Practical mapping for this project:

| Condition | Buzzer pattern | Reason |
| --- | --- | --- |
| Command accepted | 50-100 ms chirp or one short bounded beep | Confirms deliberate operator action without creating alarm noise. |
| Noncritical local attention | 500 ms on / 500 ms off, time-limited | Matches common signal-tower pulse behavior and remains recognizable. |
| Fault requiring operator action | 250 ms on / 250 ms off or about 1.5 Hz pulse, time-limited | More urgent than attention, but still bounded and distinct from evacuation. |
| Critical local hazard | Distinct double-pulse sequence, time-limited | Easier to distinguish from simple warning or validation rejection. |
| Pre-start or motion warning | Risk-assessed pulsed warning before the hazardous action | Applies only when the controlled equipment creates a local hazard. |
| Emergency evacuation | Use only an applicable evacuation or site alarm pattern | Do not reuse evacuation-style temporal patterns for normal relay states. |
| Acknowledged or silenced alarm | Audible off; visual and host-visible state remain active | Supports operator silence without hiding the underlying condition. |

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

- Current PRD already allows the passive PWM-driven buzzer on GPIO23 and
  WS2812 RGB LED on GPIO36 if relay control and RPC are unaffected.
- Hardware notes confirm the passive buzzer and RGB LED hardware.
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
