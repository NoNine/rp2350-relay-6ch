# POST And Boot Logo Research For Small OLED/PLED Displays

Date: 2026-06-03

Status: Discussion. This note records research on POST diagnostics and boot
logo or splash behavior for small monochrome dot-matrix displays. It does not
change the authoritative OLED contract, PRD, implementation plan, phase scope,
firmware behavior, or verification status unless those documents are updated
explicitly.

## Summary

For this project, "small PLED dot display" means the same practical display
class as the current OLED work: 128x64 SSD1306 or SH1106-style monochrome
dot-matrix displays, including compatible OLED or PLED modules.

The main research finding is that POST and boot logos are separate product
concepts:

- POST is an engineering diagnostic. It proves only that firmware can detect
  the display path and write a known pattern.
- A boot logo or splash screen is a user-visible product feature. It consumes
  boot time, image storage, renderer complexity, and sometimes bootloader or
  asset tooling.
- The normal status UI is the operational surface. For relay control, it must
  become visible as soon as useful firmware state is available.

The current relay-controller OLED contract already follows the safer embedded
pattern: use a fixed POST write-path check, do not delay boot for visibility,
and let normal status rendering overwrite the diagnostic pixels.

## Local Project Baseline

The existing OLED contract keeps the display optional and local-only. Missing
or failed OLED hardware must not block firmware boot, relay initialization,
RPC, `off-all`, pulse teardown, or reboot handling.

Relevant current rules:

- POST success is based on display readiness, expected geometry, accepted
  orientation, successful unblanking, and one successful diagnostic write.
- The diagnostic write is explicitly not a splash screen.
- The display mirrors commanded firmware state only; host responses and logs
  remain authoritative for automation and troubleshooting.
- OLED failures remain firmware-internal until a later explicit async-event
  policy promotes them.

This baseline should remain unchanged unless a future product decision
explicitly promotes a boot logo into scope.

## Surveyed Open-Source Patterns

| Project | Display class | Boot/logo behavior | Useful lesson |
| --- | --- | --- | --- |
| Marlin | 128x64 mono LCD and character LCD | Supports a custom bootscreen before the Marlin splash screen, with image size, offset, inversion, compression, animation, and timeout options. | Boot logos are explicit product features with asset conversion and display-time policy, not incidental POST checks. |
| QMK | SSD1306, SH1106, and SH1107 OLED modules, including 128x64 | Focuses on an OLED task that renders keyboard state, layer names, and user-defined graphics during normal operation. | Small OLED support is strongest when the display is treated as a compact live status view with known display geometry and dirty rendering. |
| IronOS | Small monochrome soldering-iron displays, including Pinecil and Miniware devices | Supports user startup logos or animations stored in a fixed flash area outside normal firmware updates for many models. | Persistent boot art creates storage, model, conversion, and flashing rules that are larger than the renderer itself. |
| Meshtastic firmware | Device OLED screens on LoRa nodes | Existing discussions distinguish a boot splash from standard, carousel, enhanced, and standalone screen modes for 128x64 devices. | Boot screens are one state in a broader local-device UI model; ongoing status screens carry the operational value. |
| Meshtastic device-ui | TFT-first UI library with planned OLED support | Tracks boot screen, customizable boot screen, home screen, and status bar features separately. | Richer boot UI is usually paired with a broader UI framework and should not be copied into a constrained status-only firmware path casually. |
| U-Boot | Bootloader video/framebuffer displays | Supports splash screens at SPL or U-Boot proper through Kconfig, device tree, image loading, and framebuffer handoff. | Bootloader splash is a different layer from application POST and brings stage-specific build and handoff complexity. |

## Reference Lessons

Marlin is the clearest small-display boot-logo example. It treats the
bootscreen as configurable artwork with a default display duration, optional
animation, explicit position, dimensions, inversion, and compressed bitmap
support. That is useful when the device is a standalone appliance with an
operator-facing display, but it is much broader than a POST probe.

QMK is a better model for the relay controller's normal display surface. It
documents SSD1306/SH1106/SH1107 support, 128x64 presets, buffer sizing, display
rotation, scrolling controls, and user rendering callbacks. The important
pattern is not keyboard-specific; it is that display code renders current
product-domain state in small, predictable updates.

IronOS shows the hidden cost of a polished startup logo feature. Logos can be
static or animated, converted to a model-specific 1-bit format, stored in flash
locations that survive firmware updates, and flashed with tooling that differs
by device. That is appropriate for a personal handheld tool, but excessive for
a relay controller whose display is optional and diagnostic.

Meshtastic is relevant because it has many small screen devices and ongoing
discussion around 128x64 UI modes. Its boot/splash ideas sit alongside
standard status screens, carousel information screens, and richer standalone UI
modes. That reinforces that splash behavior should be designed as part of a
complete local UI model, not added as a one-off renderer side effect.

U-Boot is useful mainly as a boundary example. Splash screens can be enabled
in early boot stages, loaded from boot media, and kept alive across bootloader
and OS transitions. That is a boot-chain feature. The relay controller's
application-level OLED POST should not inherit that complexity.

## Implications For This Relay Controller

Keep POST boring and deterministic:

- Use the existing fixed diagnostic pattern only to prove command/data write
  path availability.
- Do not add sleeps, logo dwell time, animation, or operator confirmation.
- Do not treat a visible logo as proof that the OLED glass, pixels, relay
  contacts, load voltage, or current flow are healthy.

Prefer status over branding:

- The first useful user-visible frame should be the normal compact status UI:
  `BOOT`, `READY`, `ACTIVE`, `ATTN`, `FAULT`, or `REBOOT`.
- If boot-progress display is desired later, express it as operational state,
  not as a decorative splash screen.
- Avoid long strings, scrolling, or explanatory text on 128x64 displays.

If a boot logo is promoted later, require a separate product decision:

- Add an explicit Kconfig option or product profile, not a default behavior.
- Specify asset dimensions, storage, conversion, timeout, and update behavior.
- Keep relay default-off initialization and RPC readiness ahead of logo
  visibility.
- Treat the logo as cosmetic and always replace it with status once firmware
  state is available.
- Keep display absence and display failure non-fatal.

## Reference List

Existing related discussions and contracts:

- `docs/oled-indicator.md`
- `docs/discussions/oled-indicator-ui.md`
- `docs/discussions/relay-controller-product-promotion.md`
- `docs/status-indicators.md`
- `docs/health-model.md`
- `docs/discussions/indicator-api-design.md`

Existing external references already used by related discussions:

- IEC 60073 coding principles:
  <https://webstore.iec.ch/en/publication/587>
- ISA-18 standards:
  <https://www.isa.org/standards-and-publications/isa-standards/isa-18-series-of-standards>
- M5Stack Core2 product page:
  <https://shop.m5stack.com/products/m5stack-core2-esp32-iot-development-kit-v1-3>
- Kode Dot product page and display docs:
  <https://www.kode.diy/> and <https://docs.kode.diy/en/kode-dot/display>
- Omron E5CC specification page:
  <https://www.ia.omron.com/products/family/3101/specification.html>
- Eaton easyE4 visualization page:
  <https://www.eaton.com/gb/en-gb/products/controls-drives-automation-sensors/AutomationControlandVisualization/easye4-programmable-relay-visualization.html>
- ifm IO-Link displays page:
  <https://www.ifm.com/us/en/us/learn-more/io-link/io-link-signal-conditioners/io-link-displays>

New open-source references surveyed:

- Marlin boot and status screens:
  <https://marlinfw.org/docs/configuration/boot_status_screen.html>
- Marlin bitmap converter:
  <https://marlinfw.org/tools/u8glib/converter.html>
- QMK OLED driver:
  <https://docs.qmk.fm/features/oled_driver>
- IronOS startup logo:
  <https://ralim.github.io/IronOS/Logo/>
- Meshtastic firmware:
  <https://github.com/meshtastic/firmware>
- Meshtastic device-ui:
  <https://github.com/meshtastic/device-ui>
- Meshtastic device screen UI discussion:
  <https://github.com/meshtastic/firmware/issues/2590>
- U-Boot splash screen documentation:
  <https://software-dl.ti.com/processor-sdk-linux/esd/AM62X/10_00_07_04/exports/docs/linux/Foundational_Components/U-Boot/UG-Splash-Screen.html>
- coreboot documentation:
  <https://doc.coreboot.org/>
