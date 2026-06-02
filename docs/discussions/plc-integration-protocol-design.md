# PLC integration protocol design

Date: 2026-06-02

Status: Discussion. This note records research and design reasoning for
PLC-facing protocol direction for an MCU-based relay and control device. It
does not change the authoritative PRD, implementation plan, phase scope,
protocol, firmware configuration, host behavior, release artifacts, or
verification status unless those documents are updated explicitly.

This discussion is intentionally not anchored to the relay controller's current
USB management protocol. The goal is to ask what a small industrial control
device should expose to PLCs and supervisory tools if the protocol were being
designed from industrial practice first.

## Summary

For PLC integration, the strongest near-term direction is a Modbus-style
industrial I/O model, most likely exposed through host or gateway tooling before
adding a native fieldbus stack to firmware. Modbus is not the richest standard,
but it is simple, widely implemented, easy to test, and well matched to a small
set of relay outputs, status bits, counters, and fault flags.

PROFINET and EtherNet/IP/CIP are important in Siemens and Rockwell automation
environments, respectively, but they carry higher stack, certification, device
profile, and product-maintenance cost. They are better treated as future
product integrations or gateway targets than as incidental firmware follow-up
work. OPC UA is valuable for supervisory integration and semantic data, but it
is usually a higher-level interface than the direct cyclic I/O view a PLC needs.

The recommended first design step is therefore not to pick a transport. It is
to define the industrial I/O model clearly:

- discrete commanded outputs;
- observed or reported output state, named honestly;
- safe-state behavior on communication loss, boot, reset, and watchdog action;
- command acceptance, rejection, and fault reasons;
- counters and identity fields that let PLC logic and operators diagnose what
  happened.

Once that model is clear, Modbus RTU/TCP can carry it directly, and richer
protocols can map to the same model later.

## Design questions

An industrial command protocol should answer PLC and operator questions before
it answers serialization questions:

- What does the PLC see: coils, discrete inputs, registers, named objects, or a
  service-oriented API?
- Which values are commands, which values are status, and which values are
  configuration?
- Is a reported relay state the commanded GPIO state, measured contact state,
  measured load power, or only the last accepted command?
- What happens when the controller boots, resets, loses communication, receives
  invalid writes, or detects an internal fault?
- Which actions are single-shot commands, and which are held ownership leases?
- Can a PLC distinguish "command accepted", "command rejected", "command
  expired", and "device forced safe state"?
- Which parts of the model must be stable across firmware versions?

For the current RP2350 relay hardware, the device can honestly report commanded
relay state, pulse activity, communication-loss policy, uptime, firmware
identity, and software counters. It cannot report contact closure, load
voltage, load current, downstream equipment health, or mains-power quality
without added measurement hardware.

## Open industrial standards

| Standard | Typical role | PLC fit | Embedded cost | Fit for this hardware |
| --- | --- | --- | --- | --- |
| Modbus RTU/TCP | Simple register and coil access over serial or TCP | Very broad | Low | Strong near-term fit for six relay outputs and status registers |
| EtherNet/IP/CIP | Rockwell-centered industrial Ethernet object model and I/O | Strong in Rockwell plants | Medium to high | Good future ecosystem target, but not a small incidental firmware feature |
| PROFINET | Siemens-centered industrial Ethernet with device descriptions and cyclic I/O | Strong in Siemens plants | High | Good future ecosystem target, especially for Ethernet hardware, certification, and product support |
| OPC UA | Secure information model and service interface for supervisory systems | Good for SCADA/MES, less direct for tiny relay I/O | Medium to high | Better as host/gateway integration than native RP2350 relay firmware |
| EtherCAT | Deterministic fieldbus for distributed I/O and motion | Strong where EtherCAT is the machine bus | High and hardware-sensitive | Not a first target for this board |
| CANopen | Object dictionary over CAN for embedded automation | Good where CAN is already present | Medium | Useful design reference, but requires CAN hardware or gateway |
| IO-Link | Point-to-point smart sensor/actuator interface | Good below PLC I/O masters | Medium and hardware-specific | Not a first target for this board |

### Modbus

Modbus is the most practical baseline for a small relay controller. The Modbus
Organization publishes the protocol specifications, including the application
protocol and serial line guidance:
<https://www.modbus.org/modbus-specifications>.

Relevant lessons:

- The coil/register model is simple enough for PLC ladder logic and test tools.
- A six-relay device can expose outputs as coils and diagnostics as input or
  holding registers.
- The protocol does not solve identity, authorization, auditability, or rich
  semantics by itself.
- Modbus TCP is easy for gateways and host tools; Modbus RTU is useful for
  serial industrial environments.

For this project, Modbus should be treated as a mapping target for a clean
industrial I/O model, not as permission to add an unsafe write-any-coil control
surface.

### EtherNet/IP and CIP

ODVA defines EtherNet/IP as an industrial Ethernet network that uses the Common
Industrial Protocol:
<https://www.odva.org/technology-standards/key-technologies/ethernet-ip/>.

Relevant lessons:

- EtherNet/IP is a natural fit for Rockwell-heavy plants.
- It supports richer identity, assemblies, cyclic I/O, and object modeling than
  raw Modbus.
- A useful implementation is not just a packet format; it needs a product
  profile, electronic data description, testing, and long-term compatibility.

For the current relay board, EtherNet/IP is better considered a gateway or
future Ethernet product path than native firmware scope.

### PROFINET

PI describes PROFINET as an industrial Ethernet standard for automation:
<https://www.profibus.com/profinet-technology>.

Relevant lessons:

- PROFINET is a natural fit for Siemens-centered PLC environments.
- Engineering workflows expect device descriptions, cyclic I/O behavior,
  diagnostics, and predictable integration into PLC tooling.
- The implementation and certification burden is far above a simple serial
  command protocol.

For this project, PROFINET should influence the I/O and diagnostic model, but
not become a near-term firmware feature without a deliberate product decision.

### OPC UA

The OPC Foundation publishes OPC UA as the IEC 62541 interoperability standard:
<https://opcfoundation.org/factory/>.

Relevant lessons:

- OPC UA is good for structured identity, capabilities, diagnostics, and
  supervisory integration.
- It can represent a relay controller as named objects with commands, status,
  metadata, and security policy.
- It is less natural than coils/registers for the smallest PLC-control loop.

For current hardware, OPC UA is most credible as a host daemon, gateway, or
edge service that sits above the deterministic device command layer.

### EtherCAT, CANopen, and IO-Link

EtherCAT, CANopen, and IO-Link are useful references because they show how
mature industrial protocols separate device identity, process data, parameters,
and diagnostics.

References:

- EtherCAT Technology Group: <https://www.ethercat.org/>
- CAN in Automation CANopen: <https://www.can-cia.org/can-knowledge/canopen/>
- IO-Link Community: <https://io-link.com/technology>

These standards are not recommended as first protocol targets for the current
RP2350 relay hardware. They require bus-specific assumptions and integration
work that should follow a clear product direction.

## Product patterns from leading vendors

Leading automation vendors rarely rely on one universal protocol. Mature
controllers and I/O products usually expose several integration paths so the
same device family can fit different plants.

### Siemens

Siemens automation products center PLC integration around PROFINET and also
position OPC UA for standardized industrial communication:
<https://www.siemens.com/global/en/products/automation/industrial-communication/opc-ua.html>.

Relevant product lesson: in Siemens environments, first-class integration is
not just "speaks Ethernet." It means the PLC engineering workflow can discover,
configure, diagnose, and cyclically exchange I/O with the device.

### Schneider Electric

Schneider Electric's Modicon controller families commonly show multi-protocol
industrial integration across Modbus, Ethernet-based automation networks, and
OPC UA depending on model and option set. The Modicon M262 product family is a
representative current controller line:
<https://www.se.com/ww/en/product-range/65771-modicon-m262/>.

Relevant product lesson: Modbus remains a mainstream integration baseline even
in modern product families, especially when simple I/O and broad tool support
matter.

### Rockwell Automation

Rockwell Automation's CompactLogix 5380 controllers are built around
EtherNet/IP integration:
<https://www.rockwellautomation.com/en-us/products/details.5069-l306er.html>.

Relevant product lesson: Rockwell-heavy sites will expect EtherNet/IP and CIP
behavior, not a generic TCP API. A gateway can be a practical bridge, but a
native product claim needs the object model and integration artifacts operators
expect.

### Beckhoff

Beckhoff's automation ecosystem strongly centers EtherCAT for field I/O and
TwinCAT for control, while also offering OPC UA connectivity through TwinCAT
functions:
<https://www.beckhoff.com/en-gb/products/automation/twincat/tfxxxx-twincat-3-functions/tf6xxx-connectivity/tf6100.html>.

Relevant product lesson: deterministic field I/O and supervisory information
models can coexist, but they serve different jobs. A small relay controller
should not blur direct output control with higher-level monitoring semantics.

## Recommended industrial I/O model

The protocol-independent model should be small and explicit:

- Six stable output channels, named by hardware label and optionally by
  host-side alias.
- Output command bits for on/off control.
- Output status bits for commanded relay state.
- Pulse or momentary-operation command path, represented separately from
  steady-state writes.
- Safe-state status that tells the PLC when firmware forced all relays off.
- Communication-loss lease or heartbeat status, including timeout policy.
- Fault and warning flags with compact reason codes.
- Counters for accepted commands, rejected commands, communication loss, forced
  all-off, reboot requests, and decode or mapping errors.
- Identity registers or objects for firmware version, hardware name, protocol
  model version, relay count, and capability bits.

Two separations matter:

- Commanded state is not measured state. Do not imply contact feedback or load
  telemetry unless hardware measures it.
- Control and configuration are different. Routine PLC output writes should not
  share the same surface as firmware update, security, or persistent
  configuration changes.

## Modbus mapping direction

A future Modbus discussion or specification should keep a conservative map:

- coils for relay output commands;
- discrete inputs for commanded output state, pulse-active state, safe-state
  active, and fault flags;
- input registers for counters, uptime, model version, and compact diagnostic
  values;
- holding registers only for narrowly justified writable configuration, if any;
- a dedicated command register or write pattern for pulse operations, so a
  momentary pulse is not confused with a steady on/off coil.

The map should reserve ranges for future diagnostics, but it should not create
pretend telemetry. Missing hardware measurements should be absent, not filled
with inferred values.

## Security and safety posture

Industrial protocols do not remove the need for a safety model. For this relay
controller:

- local USB can remain a trusted bench or local-operator path;
- any network, RS485 multi-drop, or PLC-facing bridge must define ownership,
  timeout, and safe-state behavior;
- remote relay switching should be treated as a privileged control action;
- gateways should provide access control and logging before firmware grows a
  network-facing control surface;
- invalid writes should fail predictably and leave relay state safe;
- boot, reset, firmware restart, watchdog recovery, and test teardown must
  preserve default-off behavior.

Security-rich standards such as OPC UA can help at the supervisory layer, but
the embedded device still needs deterministic local behavior when the transport
or gateway fails.

## Recommendation

Start with an industrial I/O model and a Modbus-oriented discussion for PLC
integration. Keep it independent from the existing USB management protocol, so
both the current host tooling and future industrial mappings can share the same
truth about outputs, status, faults, counters, and safe-state behavior.

Near-term:

- Document a Modbus-style I/O map as a discussion or draft spec.
- Prefer a host or gateway implementation before native firmware fieldbus
  support.
- Keep command semantics narrow: relay on/off, bounded pulse, all-off,
  heartbeat or ownership lease, status, identity, and diagnostics.

Defer:

- Native PROFINET, EtherNet/IP, EtherCAT, CANopen, or IO-Link firmware support.
- Network-facing firmware control without an explicit access model.
- Claims of measured power, current, contact state, or downstream equipment
  health on hardware that does not measure those values.

This path gives PLC users a familiar control surface while preserving the
project's current scope discipline: safe local relay control first, richer
industrial integration only when deliberately promoted into product scope.
