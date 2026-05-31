# Relay controller product promotion

Date: 2026-05-30

Status: Discussion. This note records product-presentation guidance for the
relay controller, especially its local indicator subsystem, in README, release,
or product context. It does not change the authoritative PRD, implementation
plan, firmware UI behavior, OLED contract, phase scope, or verification status
unless those documents are updated explicitly.

## Summary

Present the physical relay controller first, then show the local indicator
subsystem as an integrated part of the product. The product-level message is
that RGB, buzzer, and OLED outputs provide at-a-glance bench or operator
confidence while host RPC remains authoritative.

The best references sell local display or visualization as an integrated
product capability, not as a detached UI specification. M5Stack Core2 and Kode
Dot are useful for physical-device promotion: the local display helps the
product feel complete, inspectable, and ready to use. Omron E5CC is useful for
readability and operator confidence. Eaton easyE4 is useful for presenting
visualization as one surface inside a controller system. ifm IO-Link displays
are useful for showing status near equipment while upstream controller context
remains authoritative.

## Product promotion pattern

- Lead with the physical relay controller or a product-style generated preview,
  with the local indicator subsystem visibly installed where possible.
- Frame the local indicators as operator confidence: a nearby human can glance
  at the controller and see whether it is ready, active, pulsing, or in an
  attention or fault state.
- Show one strong operational state for README or release attention, such as
  `PULSE CH3`, `CH2 CH5 ON`, or `FAULT/ATTN`.
- Pair the image and benefit with concrete constraints: optional `128x64`
  SSD1306, RGB/buzzer/OLED outputs, local commanded-state indication, and host
  RPC remaining authoritative.
- Keep full state matrices, pixel-level behavior, renderer priorities, and
  firmware behavior in technical local-indicator documentation.
- Label generated or simulated previews clearly so they do not imply a
  photographed installed display or completed hardware verification.

## M5Stack Core2 promotion pattern

M5Stack Core2's strongest pattern is physical product first, integrated feature
set second. The page presents a compact, display-bearing controller as the
object of interest, then uses the screen, touch zones, speaker, vibration
motor, battery, sensors, storage, and expansion interface to make it feel like
a complete IoT terminal rather than a loose development board.

That pattern is useful because the display is not isolated from the product.
It sits inside the enclosure alongside feedback, controls, power, and expansion
so the device reads as something a developer can hold, power, program, and use
directly. For relay-controller promotion, lead with the assembled controller
and show relay terminals, board or enclosure context, OLED, RGB LED, and buzzer
as one integrated product instead of showing a state table or pixel mockup
first.

Core2 also moves from integrated capability to applications and then to specs.
It names use cases such as IoT terminals, HMI devices, DIY projects,
smart-home devices, and embedded multi-function development before the dense
parameter list. The relay-controller equivalent is bench control, operator
confidence, relay state visibility, and then the concrete constraints: optional
SSD1306 OLED, RGB/buzzer/OLED outputs, GPIO relay hardware, Zephyr firmware,
local commanded-state indication, and host RPC as command authority.

Do not borrow Core2's touchscreen, multimedia, battery-powered terminal, or
general HMI expectations. Borrow the product-page structure: complete physical
device, integrated local feedback, practical use cases, then constrained specs.

## Kode Dot promotion pattern

Kode Dot's strongest pattern is complete maker device first, display as proof
of capability second. It promotes a small physical controller as
self-contained, programmable, and ready for inspection or prototyping, then
uses the onboard display to make that promise visible. The screen matters
because it completes the product story, not because the page treats it as a
detached display module.

The first message is the physical object and what it lets a maker do with less
bench friction: carry it, power it, run projects, inspect state, and interact
without assembling a loose board stack. The display makes the controller feel
alive and locally legible. For relay-controller promotion, use the same
self-contained-device framing: the local indicators are visible proof that the
product is powered, understandable, and ready for bench work.

Kode Dot leads with audience and workflow before detailed specs. It frames the
product for makers, hackers, education, prototyping, and project demos, then
uses the display as the surface where those workflows become visible. The relay
controller should not borrow the app experience; it should borrow the immediate
inspection value. A promoted local-indicator state should answer a nearby
operator's first question: ready, active channels, pulse in progress, or
attention needed.

The product also reduces perceived risk with familiar tooling and ecosystem
cues such as common embedded development paths, open examples, and display
libraries. The relay-controller equivalent is to keep any preview close to
credible integration facts: optional `128x64` SSD1306, RGB/buzzer/OLED local
outputs, local commanded-state indication, and host RPC authority.

Do not borrow Kode Dot's touch interaction, launcher metaphors, high-resolution
color UI, or rich project screens. Borrow only the integrated-device framing:
OLED, RGB LED, and buzzer as one local confidence layer around host-authoritative
relay control.

## Omron E5CC promotion pattern

Omron E5CC's strongest pattern is readability first. The product presentation
puts the compact front-panel display at the center of the value story: a small
industrial controller must make current state easy to read, easy to set up, and
easy to operate inside a control panel.

The display is treated as an operational surface, not a decorative screen. The
front face communicates value, setpoint context, and device status with
restrained industrial typography and high contrast. For relay-controller
promotion, this argues for short, high-contrast, operational local-indicator
states. A product image should favor clear OLED strings such as `READY`,
`PULSE CH3`, `CH2 CH5 ON`, or `FAULT/ATTN`, supported by RGB/buzzer context,
over dense menus or explanatory copy.

Omron's pattern ties readability to trust. The device appears credible because
the operator can interpret its state quickly while standing at the panel. The
relay controller needs the same nearby confidence: a human at the bench should
be able to glance at the product and know whether relay activity is normal,
active, pulsing, or in an attention state.

Do not borrow E5CC's process-control semantics, numeric PV/SV behavior,
certified panel-controller behavior, or persistent on-device control authority.
Borrow the readability pattern: restrained, high-contrast, operator-readable
local state, with `128x64` SSD1306 treated as one constraint behind the broader
OLED/RGB/buzzer confidence message.

## Eaton easyE4 promotion pattern

Eaton easyE4's strongest pattern is controller-system visualization. The page
presents display and visualization as surfaces within a programmable relay
system, alongside remote touch displays, browser-based visualization, and
mobile access through the controller's integrated web capability.

That pattern is useful because one screen does not become the whole product.
Local display, mirrored relay display, custom touch display, and web/mobile
visualization are framed as options for different installation needs. For the
relay controller, OLED, RGB LED, and buzzer should be described as local
confidence outputs within a larger control path, while host RPC remains the
authoritative command interface.

Eaton's pattern also separates visibility from authority. A visualization
surface can show status, allow adjustment, mirror controls, or provide remote
access, but it remains part of the controller system rather than becoming the
system's identity. Relay-controller promotion should show confirmation of
commanded state, attention states, and fault visibility near the device without
implying that the local indicator subsystem makes relay decisions.

Do not borrow easyE4's remote visualization products, web tooling, network
dashboards, remote HMIs, operator roles, alarm modules, or SmartPDU-like
control surfaces. Borrow the system framing: local indicators as confidence
surfaces inside the relay controller product.

## ifm IO-Link displays promotion pattern

ifm IO-Link displays' strongest pattern is local process and diagnostic
visibility near the equipment. The product page presents displays as devices
that show process values, text, messages, and status information assigned by a
controller or received from sensors in an IO-Link environment.

The display is promoted as an in-place interpretation layer. It lets operators
see values and messages where the machine or sensor context exists, instead of
forcing every check through a remote workstation. The relay-controller parallel
is local confirmation at the hardware: OLED, RGB LED, and buzzer make the host
commanded relay state visible to a bench operator without changing where
authority lives.

ifm's pattern is especially useful because it separates data source from local
presentation. The controller or sensor remains the source of the process data,
while the display makes that data visible locally. For the relay controller,
host RPC remains the source of commands and the local indicator subsystem
becomes a nearby status view, not a command source, telemetry system, or audit
log.

Do not borrow IO-Link ecosystem capabilities such as controller-assigned
process values, sensor-fed values, keys, QR codes, color screens, or runtime
configuration unless they are explicitly promoted into project scope. Borrow
only the local-visibility pattern: process-like state displayed near the
equipment while upstream command authority remains elsewhere.

## Reference use summary

- Use M5Stack Core2 for complete physical-product framing with integrated local
  feedback.
- Use Kode Dot for self-contained maker-device framing where local indicators
  make the product feel alive and inspectable.
- Use Omron E5CC for local-indicator readability: restrained, high-contrast,
  glanceable operating status.
- Use Eaton easyE4 for controller-system framing: local visualization is one
  confidence surface, not the command authority.
- Use ifm IO-Link displays for near-equipment status framing while preserving
  upstream controller authority.

## References

- M5Stack Core2 product page:
  <https://shop.m5stack.com/products/m5stack-core2-esp32-iot-development-kit-v1-3>
  for product-page framing around a physical display-bearing device.
- Kode Dot product page and display docs:
  <https://www.kode.diy/> and
  <https://docs.kode.diy/en/kode-dot/display>
  for integrated maker-device framing where the onboard display makes a small
  controller feel self-contained and inspectable.
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
