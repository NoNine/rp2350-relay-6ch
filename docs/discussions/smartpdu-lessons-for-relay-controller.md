# SmartPDU lessons for the relay controller

SmartPDUs and this relay controller are not the same class of product. A
SmartPDU distributes mains power inside a rack and is designed, certified, and
marketed around outlet-level power control and metering. This project controls
six relay outputs on an RP2350 board and should not imply PDU safety,
certification, or metering capabilities that the hardware does not provide.

Still, the operational role overlaps. In a remote server room, both devices can
be part of the recovery path when equipment is unreachable and no operator is
on site. This discussion asks what the relay-controller project can learn from
leading SmartPDU products without trying to become a rack PDU.

## Representative products reviewed

As of 2026-05-22, the common SmartPDU feature set is visible across several
current vendor families:

- APC NetShelter switched and metered-by-outlet PDUs show common enterprise
  PDU features such as individual outlet remote power on/off control, power-on
  sequencing and time delays, outlet grouping, role-based outlet access, and
  field-replaceable network management modules:
  <https://www.apc.com/shop/tradeups/us/en/products/APC-NetShelter-Rack-PDU-Switched-1U-1-4kW-120V-15A-8-NEMA-5-15P-outlets/P-AP7900B>
  and
  <https://iportal.se.com/Contents/docs/UPS-JLAE-7DZLEC_R6_EN.PDF>
- Raritan PX and PX4 PDUs combine power-quality metering, environmental sensor
  options, alerting, hot-swappable network cards, and integration through SNMP,
  RESTful API, Redfish API, JSON-RPC, Lua scripting, and SDKs:
  <https://www.raritan.com/products/power-distribution/intelligent-rack-pdus>
  and
  <https://www.raritan.com/product-selector/pdu-detail/PX4-534AJU-E7>
- Vertiv Geist switched rack PDUs provide outlet-level switching, monitoring,
  environmental sensor support, alarms, secure web access, SNMP, and JSON API
  integration:
  <https://www.vertiv.com/en-us/products-catalog/critical-power/power-distribution/vertiv-geist-switched-rack-pdu/>
  and
  <https://www.geistglobal.com/switched-outlet-level-monitoring>
- Eaton Rack PDU G4 highlights secure boot, cybersecurity certifications,
  dual Ethernet, cascading, hot-swappable network modules, latching relays, and
  outlet-level monitoring/switching on managed models:
  <https://www.eaton.com/us/en-us/products/backup-power-ups-surge-it-power-distribution/power-distribution-for-it-equipment/eaton-rack-pdu-g4.html>
  and
  <https://www.eaton.com/ae/en-gb/company/news-insights/news-releases/2024/eaton-launches-its-new-rack-pdu-g4.html>
- CyberPower switched metered-by-outlet PDUs show the mainstream baseline:
  per-outlet remote switching, remote reboot, load shedding, power sequencing,
  local/remote monitoring, and email/SMS/SNMP event notifications:
  <https://www.cyberpowersystems.com/products/pdu/switched-mbo/>
  and
  <https://www.cyberpowersystems.com/product/pdus/switched-mbo/pdu83105/>
- Server Technology PRO4X PDUs add examples of hot-swappable controllers,
  outlet LEDs, redundant controller power between linked PDUs, environmental
  sensors, and detailed power/status visibility:
  <https://www.servertech.com/products/pro4x-rack-pdu-tech-specs/>

This list is not a product ranking. It is a sample of mature SmartPDU patterns
worth comparing against the relay controller's remote-operations goals.

## Lesson 1: model outputs as named assets

Leading SmartPDUs do not ask operators to remember only "outlet 17." They add
labels, groups, LEDs, sequencing, and metadata so an operator can map a control
action to real equipment.

For this project:

- Keep `CH1` through `CH6` as the stable hardware labels.
- Consider host-side aliases such as `router`, `modem`, `oob-switch`, or
  `camera-psu`, stored outside firmware for v1.
- Add a future command or host config format that can print both the relay
  number and the operator alias.
- Make every log, JSON response, and smoke-test message include enough context
  to avoid switching the wrong relay.

The important lesson is not the outlet count. It is that remote switching must
be operationally legible.

## Lesson 2: sequencing is a first-class feature

SmartPDUs commonly support delayed power-on, delayed power-off, reboot actions,
and outlet power sequencing. This avoids inrush problems, gives upstream
equipment time to stabilize, and lets dependent devices restart in order.

For this relay controller, useful sequencing ideas include:

- Add a host-side "sequence" workflow before adding complex firmware state.
- Keep sequence steps explicit: relay, action, delay, timeout, and expected
  final state.
- Make sequence failure behavior conservative and visible.
- Prefer idempotent recovery recipes such as "off, wait, on, wait, verify" over
  ambiguous toggles.
- Keep the existing bounded pulse command for simple cases, but do not force all
  recovery workflows through pulse semantics.

A good v1 direction is a CLI-driven sequence file that expands into existing
`set`, `pulse`, `off_all`, and `status` requests.

## Lesson 3: state feedback matters as much as commands

SmartPDU products spend significant design effort on status. Examples include
outlet LEDs, per-outlet monitoring, local displays, alarm states, environmental
sensor values, and warnings when reported outlet state does not match expected
state.

The relay controller currently reports logical relay state and protocol
counters. It does not electrically verify load power or contact closure.

Possible lessons:

- Be precise in naming: report "commanded relay state" unless hardware feedback
  is actually measured.
- If future hardware adds feedback, separate commanded state from observed
  state.
- Consider a host-side warning when a command succeeds but follow-up `status`
  does not match the expected state.
- Use the buzzer or RGB LED only for concise local state indication, not as a
  substitute for host-visible status.
- Add counters for reboot count, last reboot reason, last relay command, and
  last error when firmware support is available.

SmartPDUs teach that remote control without trustworthy feedback is only half a
management interface.

## Lesson 4: telemetry changes the product category

Metered and managed PDUs expose volts, amps, watts, energy, power factor,
branch current, breaker state, and sometimes power-quality events. That data is
useful for capacity planning, overload prevention, billing, and diagnosing
failed equipment.

This relay controller should not claim equivalent telemetry on current
hardware. However, the management pattern still matters:

- Keep the protocol extensible enough to add optional measurements later.
- Treat measurements as capabilities, not required fields.
- Include units, scale, sampling age, and validity for any future sensor value.
- Preserve deterministic behavior when a sensor is absent or not ready.
- Avoid using inferred relay state as a proxy for actual load health.

If a future board adds current sensing or dry-contact feedback, it should be
reported as observed telemetry alongside the existing command state.

## Lesson 5: alerts and event logs are core operations tools

SmartPDUs commonly integrate with SNMP traps, email, syslog, dashboards, and
DCIM systems. Their value is not just the protocol list; it is that operators
learn about threshold crossings, failed actions, and environmental risks
without polling a device manually.

For this project:

- Keep device firmware small and deterministic; start alerts in host tooling.
- Add a host-side monitor mode that polls `status` and emits JSON lines or
  process exit codes suitable for supervisors.
- Consider syslog, Prometheus textfile output, or a simple webhook bridge in
  host tooling before adding networking to firmware.
- Record timestamped events for relay operations, failed commands, firmware
  identity changes, and update workflow steps.
- Keep thresholds explicit and local to the operator environment.

This follows the SmartPDU pattern while avoiding premature network exposure on
the relay board.

## Lesson 6: access control becomes mandatory when control leaves the bench

Enterprise SmartPDUs expose security features because remote power control is a
high-impact administrative function. Common patterns include HTTPS, SSH,
SNMPv3, user roles, directory integration, RADIUS, TACACS+, secure boot,
signed firmware, and restricted remote APIs.

The current relay controller v1 assumes trusted local USB access and signed
firmware images later. That is reasonable for the current scope, but the
SmartPDU lesson is clear:

- Do not add Wi-Fi, RS485 multi-drop, or IP control without an explicit access
  model.
- Prefer signed firmware and rollback before remote network control.
- Treat "reboot a server" or "switch a relay" as a privileged operation.
- Keep auditability in mind: operator, command, target relay, time, result, and
  firmware identity.
- Make unsafe defaults hard to deploy accidentally.

Security is not an add-on feature for a remotely reachable power-control
device.

## Lesson 7: management-plane serviceability is part of availability

Several SmartPDU designs distinguish the management controller from the power
path. Product families advertise hot-swappable network modules, controller
reboots that do not interrupt load power, linked controller power, and local
displays for setup.

The relay controller has a simpler architecture, but the lesson applies:

- Firmware reboot behavior must be explicit: all relays default off today.
- If future deployments need management restart without output changes, that
  would be a different product decision and should be documented as such.
- Host tooling should make firmware identity and protocol compatibility easy to
  check before running recovery commands.
- Recovery procedures should cover "USB serial changed," "device unreachable,"
  "firmware update failed," and "relay state unexpected."
- Firmware update should preserve a known-good image until the new image proves
  itself healthy.

Availability is not only uptime. It is also the ability to recover the control
plane predictably.

## Lesson 8: physical installation details reduce operator mistakes

SmartPDU products use colored chassis, locking outlets, cable retention,
orientation-aware displays, outlet LEDs, and local labels to reduce mistakes in
crowded racks.

For this project:

- Encourage clear relay-channel labeling at the enclosure and terminal block.
- Keep documentation examples aligned with board labels: `CH1` through `CH6`.
- Consider printable or machine-readable mapping labels for operator installs.
- Do not rely on software aliases alone when physical wiring can be changed.
- Make first-flash and smoke-test procedures assume hazardous loads are
  disconnected.

For remote equipment, a wrong label can be as damaging as a software bug.

## What not to copy blindly

The relay controller should not copy every SmartPDU behavior:

- Do not imply outlet-level power metering without measurement hardware.
- Do not persist relay-on state across reboot just because some PDUs can retain
  outlet state.
- Do not add embedded web UI, cloud management, or multiple network protocols
  before authentication, update safety, and operational ownership are clear.
- Do not make automatic watchdog-triggered power cycling a default behavior;
  it may be useful, but it can also create destructive restart loops.
- Do not treat safety certification, mains isolation, or load ratings as
  documentation-only concerns.

The project should adopt SmartPDU operational discipline, not SmartPDU claims.

## Possible roadmap influence

Near-term ideas that fit the current project:

- Host-side relay aliases.
- CLI sequence files for ordered recovery workflows.
- A monitor mode that emits machine-readable health and state events.
- A support-bundle command that collects `info`, `build_info`, `status`, host
  version, and serial-port details.
- Clearer status naming around commanded state versus observed state.

Medium-term ideas:

- Firmware reboot reason and boot-count fields.
- Last-command and last-error event records.
- Optional sensor fields for temperature, dry-contact inputs, or load feedback
  if hardware support is added.
- Firmware update confirmation tied to explicit health checks.
- Signed release workflow that makes firmware and host tooling compatibility
  obvious.

Longer-term ideas:

- RS485 or network transport with authentication and auditability.
- Redundant management paths with one relay arbiter.
- DCIM or monitoring integration through host tooling.
- Hardware variants with feedback inputs or current sensing.
- A separate, properly certified power-control product if the project ever
  moves from relay-controller firmware to SmartPDU-like mains distribution.

## Open questions

1. Should relay aliases live only in host configuration, or should firmware
   eventually expose a small label store?
2. What is the safest default when a multi-step sequence fails halfway through?
3. Which event fields are necessary for a useful operator audit trail?
4. Should future feedback inputs be modeled per relay, per load, or per
   external circuit?
5. Which monitoring integration should come first: JSON lines, syslog,
   Prometheus textfile, SNMP, or a webhook bridge?
6. Would RS485 add enough operational value before Wi-Fi or Ethernet, given the
   project's isolated relay/RS485-side hardware notes?
7. What behaviors from commercial SmartPDUs would be actively unsafe for this
   relay board's current hardware?

## Suggested direction

The strongest SmartPDU lesson is that remote power control is an operations
product, not just a switching API. For this relay controller, the practical path
is:

- Keep relay behavior simple, safe, and explicit.
- Add host-side operator ergonomics before adding firmware complexity.
- Expand status, sequencing, monitoring, and support-bundle workflows in
  scriptable formats.
- Keep security and update rollback ahead of any remote network transport.
- Be honest about hardware limits, especially around power metering,
  electrical feedback, and mains-rated product claims.
