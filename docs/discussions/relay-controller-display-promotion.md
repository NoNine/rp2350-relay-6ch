# Relay controller display promotion

Date: 2026-05-30

Status: Discussion. This note records presentation guidance for showing the
relay controller's local indicator subsystem in README, release, or product
context. It does not change the authoritative PRD, implementation plan,
firmware UI behavior, OLED contract, phase scope, or verification status unless
those documents are updated explicitly.

## Summary

Present the OLED as part of the physical relay controller's local indicator
subsystem, not as a detached UI matrix. The product-level message is that RGB,
buzzer, and OLED outputs provide at-a-glance bench or operator confidence while
host RPC remains authoritative.

The best references are products that sell a small display as an integrated
capability, not as a detached UI specification. M5Stack Core-style product
pages are a good product-presentation reference: they lead with the physical
display-bearing device and make the display feel like part of the product.
Omron is a good readability reference: its temperature-controller presentation
emphasizes high-contrast, glanceable process and status display. Eaton is a
good integrated-controller reference: its smart-relay presentation treats
built-in display and status visualization as local confidence surfaces within a
broader controller system. ifm IO-Link displays are a good local
process/status visualization reference: they show sensor or process values near
the equipment while the controller context remains authoritative.

## Promotion pattern

- Lead with the physical relay controller or a product-style generated preview
  with the OLED visibly installed.
- Frame the display as local operator confidence: a nearby human can glance at
  the controller and see whether it is ready, active, pulsing, or in an
  attention or fault state.
- Show one strong operational state for README or release attention, such as
  `PULSE CH3`, `CH2 CH5 ON`, or `FAULT/ATTN`.
- Pair the image and benefit with concrete constraints: optional `128x64`
  SSD1306, RGB/buzzer/OLED outputs, local commanded-state indication, and host
  RPC remaining authoritative.
- Keep full state matrices, pixel-level behavior, and renderer priorities in
  technical OLED documentation.
- Label generated or simulated previews clearly so they do not imply a
  photographed installed display or completed hardware verification.

## Reference use

Use M5Stack-style lessons when writing about product capability: lead with the
physical product or display-bearing device, not a detached state matrix.

Use Omron-style lessons when writing about readability: high contrast,
glanceable process status, restrained text, and easy local interpretation.

Use Eaton-style lessons when writing about controller integration: the display
is one local visualization path inside a larger control system, not the source
of authority for relay commands.

Use ifm IO-Link-style lessons when writing about local indication: show current
state or process-like values near the equipment while preserving the host or
controller path as the authority.

## References

- M5Stack Core2 product page:
  <https://shop.m5stack.com/products/m5stack-core2-esp32-iot-development-kit-v1-3>
  for product-page framing around a physical display-bearing device.
- Omron E5CC specification page:
  <https://www.ia.omron.com/products/family/3101/specification.html>
  for high-contrast, readable local process display presentation.
- Eaton easyE4 visualization page:
  <https://www.eaton.com/gb/en-gb/products/controls-drives-automation-sensors/AutomationControlandVisualization/easye4-programmable-relay-visualization.html>
  for integrated controller visualization across local and remote surfaces.
- ifm IO-Link displays page:
  <https://www.ifm.com/us/en/us/learn-more/io-link/io-link-signal-conditioners/io-link-displays>
  for local process/status values shown near equipment while the controller
  context remains authoritative.
