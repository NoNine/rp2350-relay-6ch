# Industrial control command model

Date: 2026-06-02

Status: Discussion. This note records open research and design reasoning for
command models used by industrial control devices. It does not change the
authoritative PRD, implementation plan, phase scope, protocol, firmware
configuration, host behavior, release artifacts, or verification status unless
those documents are updated explicitly.

This research is not anchored to the relay controller's current USB management
protocol. PLC-facing protocols are useful references, but PLC integration is
not the goal of this note. The goal is to understand how industrial products
separate routine control, observed state, diagnostics, configuration, identity,
ownership, safety behavior, and maintenance commands before choosing any
particular transport or encoding.

Related local references:

- [Relay management protocol](../protocol/relay-management.md) documents the
  current local USB/SMP command surface.
- [PLC integration protocol design](plc-integration-protocol-design.md)
  records narrower PLC-facing research.
- [PRD](../prd.md) remains the authoritative product requirements document.

## Summary

Leading industrial products rarely expose one flat command API. They tend to
separate fast process data from service commands, service commands from
configuration, configuration from diagnostics, and diagnostics from firmware
maintenance. The same device may expose different protocol surfaces for local
service, PLC control, supervisory systems, remote operations, and vendor
engineering tools.

For this relay controller, the strongest research direction is therefore a
protocol-independent command model, not a new wire protocol. The model should
state what can be commanded, what can be observed, what is merely inferred, who
owns control, how leases expire, how failures are reported, and what remains
outside routine output control. Once that model is clear, it can later be
mapped to USB/SMP, a host daemon, Modbus, OPC UA, MQTT Sparkplug, or a vendor
gateway without redefining relay behavior each time.

The model must stay honest about hardware. The current board can report
commanded relay state, pulse activity, uptime, firmware identity, health state,
communication-loss policy, and software counters. It cannot report relay
contact closure, load voltage, load current, power quality, downstream
equipment health, or mains-power behavior without additional measurement
hardware.

## Product patterns from leading vendors

The most useful product lesson is not which protocol is most common. It is that
industrial products define several roles for the same physical device:

- deterministic process data for control loops;
- explicit service commands for actions that should not be hidden in a status
  register;
- parameter and configuration channels for persistent behavior;
- diagnostics and identity for engineering tools and operators;
- maintenance channels for firmware update, reboot, backup, restore, and
  commissioning;
- supervisory or edge interfaces for broader plant systems.

### Siemens

Siemens automation products commonly center controller and I/O integration on
PROFINET, while also positioning OPC UA for standardized industrial
communication and higher-level interoperability:
<https://www.siemens.com/global/en/products/automation/industrial-communication/opc-ua.html>.

Relevant product lesson: a device command model must fit engineering workflow,
not only packet exchange. Operators expect identity, module layout, cyclic
data, diagnostics, and configuration to appear coherently in tooling. A generic
TCP command socket would not be equivalent to a productized Siemens-facing
device.

### Rockwell Automation

Rockwell Automation's CompactLogix 5380 controller family is built around
EtherNet/IP integration:
<https://www.rockwellautomation.com/en-us/products/details.5069-l306er.html>.

Relevant product lesson: Rockwell-oriented integration is an object and
assembly model, not only "commands over Ethernet." A device that claims this
ecosystem needs identity, cyclic I/O assemblies, diagnostics, and compatibility
artifacts that match the operator's control environment.

### Schneider Electric

Schneider Electric's Modicon controller families show a multi-protocol pattern
across Modbus, Ethernet-based automation networks, CANopen, and OPC UA,
depending on product and option set. The Modicon M262 family is a
representative current controller line:
<https://www.se.com/ww/en/product-range/65771-modicon-m262/>.

Relevant product lesson: Modbus remains useful even in modern product
families, but it is one integration face over a broader product model. Simple
registers are valuable for access, not sufficient for safety, ownership,
configuration discipline, or maintenance behavior.

### Beckhoff

Beckhoff's automation ecosystem strongly centers EtherCAT for field I/O and
TwinCAT for control, while also offering OPC UA connectivity through TwinCAT
functions:
<https://www.beckhoff.com/en-gb/products/automation/twincat/tfxxxx-twincat-3-functions/tf6xxx-connectivity/tf6100.html>.

Relevant product lesson: deterministic field I/O and supervisory information
models can coexist, but they serve different jobs. A small control device
should not blur low-latency output control with higher-level observation,
metadata, or administrative operations.

### WAGO

WAGO controllers such as the PFC200 family are positioned as industrial
controllers that bridge classic automation, fieldbus communication, and IT or
edge integration:
<https://www.wago.com/us/c/controllers-pfc200>.

Relevant product lesson: gateway and controller layers are normal places for
multi-protocol integration. A constrained end device does not need to implement
every industrial protocol natively if a controller or host layer can expose
the same command model safely.

### Phoenix Contact

Phoenix Contact's PLCnext Control products combine PLC runtime, industrial
networking, and higher-level software integration:
<https://www.phoenixcontact.com/en-us/products/controller-axc-f-2152-2404267>.

Relevant product lesson: modern industrial devices increasingly sit between OT
and IT expectations. That makes interface separation more important, not less:
control, configuration, diagnostics, and remote software functions need
different stability and security assumptions.

### Moxa

Moxa ioLogik remote I/O products expose distributed I/O through industrial and
network management protocols:
<https://www.moxa.com/en/products/industrial-edge-connectivity/controllers-and-ios/universal-controllers-and-i-os/iologik-e1200-series>.

Relevant product lesson: remote I/O products are good models for compact
command surfaces. Their value comes from exposing physical I/O, status,
network identity, and event/reporting behavior predictably, not from offering a
large arbitrary API.

### Advantech

Advantech ADAM Ethernet I/O modules are representative remote I/O products
that expose simple I/O points and diagnostics through Ethernet-oriented
industrial interfaces:
<https://www.advantech.com/en-us/products/ethernet-i-o-modules-adam-6000/sub_a67f7853-013a-4b50-9b20-01798c56b090>.

Relevant product lesson: a small relay controller can learn from remote I/O
devices: expose a stable, compact point model; separate output control from
device administration; and keep diagnostic claims bounded by the hardware.

## Standards as model references

Open industrial standards are best used here as design references. They show
common divisions of process data, service data, object models, diagnostics,
and integration artifacts. They are not automatic implementation choices.

### Modbus RTU/TCP

Useful command-model lesson: coils and registers make a small I/O model easy
to test and map.

Caution for this project: Modbus does not provide rich semantics, ownership,
auditability, or security by itself.

### EtherNet/IP and CIP

Useful command-model lesson: object identity, assemblies, and service requests
separate cyclic I/O from explicit services.

Caution for this project: a credible product needs more than packet support; it
needs profile, tooling, and compatibility work.

### PROFINET

Useful command-model lesson: engineering integration expects device
descriptions, cyclic data, diagnostics, and configuration.

Caution for this project: native support has high stack, certification, and
maintenance cost.

### OPC UA

Useful command-model lesson: OPC UA is a strong model for identity, metadata,
commands, diagnostics, and supervisory integration.

Caution for this project: it is usually a higher-level interface than the
smallest deterministic output-control path.

### EtherCAT

Useful command-model lesson: EtherCAT separates deterministic process data from
mailbox and configuration behavior.

Caution for this project: it is hardware- and ecosystem-sensitive, and not a
first fit for this board.

### CANopen

Useful command-model lesson: object dictionaries and device profiles are a
useful discipline for stable objects and parameters.

Caution for this project: it assumes CAN infrastructure or a gateway.

### IO-Link

Useful command-model lesson: IO-Link shows a compact sensor and actuator split
between process data, parameters, events, and device identity.

Caution for this project: it requires IO-Link-specific hardware and master
integration.

### MQTT Sparkplug

Useful command-model lesson: Sparkplug provides a publish/subscribe model for
edge state, birth/death certificates, metrics, and commands.

Caution for this project: it is better for supervisory or edge integration than
direct safety-critical local control.

References:

- Modbus Organization specifications:
  <https://www.modbus.org/modbus-specifications>
- ODVA EtherNet/IP and CIP overview:
  <https://www.odva.org/technology-standards/key-technologies/ethernet-ip/>
- PROFINET technology overview:
  <https://www.profibus.com/profinet-technology>
- OPC UA overview:
  <https://opcfoundation.org/about/opc-technologies/opc-ua/>
- EtherCAT Technology Group:
  <https://www.ethercat.org/en/technology.html>
- CAN in Automation CANopen overview:
  <https://www.can-cia.org/can-knowledge/canopen/>
- IO-Link Community:
  <https://io-link.com/>
- Eclipse Sparkplug specification:
  <https://sparkplug.eclipse.org/specification/>

## Command-model design questions

An industrial control command model should answer these questions before it
chooses CBOR, registers, JSON, objects, or topic names:

- Which values are commands, which are observed state, which are configuration,
  and which are diagnostics?
- Is an output state commanded, measured, inferred, or latched from a previous
  command?
- Which commands are idempotent state assignments, and which are single-shot
  actions?
- Which operations require exclusive ownership, a lease, or heartbeat renewal?
- What happens on boot, reset, watchdog recovery, firmware restart,
  communication loss, invalid writes, and internal faults?
- Can the caller distinguish accepted, rejected, busy, expired, cancelled,
  forced safe, and failed-after-acceptance outcomes?
- Which counters and fault reasons remain stable enough for scripts,
  dashboards, and support procedures?
- Which functions are routine control, and which are maintenance operations
  that should require a different surface or privilege?
- Which fields are stable public contract, and which are presentation details
  of one transport or gateway?

For relay control, the most important distinction is commanded output state
versus measured field state. Without feedback contacts or load measurement,
the model must say "commanded on" or "pulse active", not "contact closed",
"load powered", or "equipment running".

## Candidate command model for this project

This candidate model is a research direction, not a new product requirement.
It describes a protocol-independent shape that could sit below several future
interfaces.

### Control

Routine control should stay small:

- set one relay on or off;
- set all relay outputs from one six-bit state;
- turn all relays off;
- pulse one relay for a bounded duration;
- renew a control lease or heartbeat where the selected ownership policy uses
  one.

Steady-state commands and momentary commands should remain distinct. A pulse is
not the same as writing a relay on and later writing it off, because the device
owns pulse teardown and busy behavior.

### Status

Status should report device facts and command-model facts:

- commanded relay state mask;
- pulse-active mask;
- safe-state or forced-all-off indication;
- communication-loss policy and lease state when applicable;
- health state and compact health reasons;
- uptime and reset or boot identity when available;
- accepted, rejected, decode-error, invalid-argument, busy, communication-loss,
  forced-all-off, reboot, and mapping-error counters.

Status should avoid pretending to be telemetry. Contact state, load current,
load voltage, power quality, and downstream device health should be absent
unless future hardware measures them.

### Identity and capabilities

Identity and capability fields should let clients decide what model they are
speaking to before issuing commands:

- hardware name and relay count;
- firmware version and build identity;
- command-model version;
- transport or gateway identity;
- supported operations;
- pulse duration bounds;
- communication-loss and safe-state capabilities.

Identity should be readable through every integration surface that can issue
control commands.

### Configuration

Configuration should not share the same mental model as routine relay writes.
Writable configuration, if added later, should be narrow, explicit, and
auditable at the host or gateway layer.

For v1-style behavior, avoid persistent relay-on state. Boot, reset, firmware
restart, watchdog recovery, and test setup or teardown should preserve
default-off behavior.

### Maintenance

Firmware update, reboot, image confirmation, factory reset, and support-bundle
collection are maintenance operations. They may use the same physical transport
as routine control, but they should remain distinct in the command model and in
operator tooling.

Maintenance commands should not be hidden behind generic output registers or
ordinary relay-control endpoints.

### Ownership and failure semantics

Industrial command models need explicit control ownership:

- define whether control is local-only, host-owned, gateway-owned, or
  multi-client;
- define what renews ownership and what expires it;
- define the safe state on ownership loss;
- make invalid or unauthorized writes fail predictably without surprising
  relay state changes;
- report command rejection reasons separately from device health faults;
- report accepted commands that later fail during deferred work, such as
  controlled reboot or timed output teardown.

Ownership policy can be stricter at a gateway than in local firmware, but the
safe-state behavior must remain deterministic if the gateway or transport
fails.

## Mapping direction

The command model should be the stable center. Protocols and products should
map to it:

- USB/SMP remains the current trusted local control and maintenance surface.
- A host daemon can expose the model to local applications while retaining
  access control, logging, and multi-device coordination.
- Modbus can map the compact I/O and diagnostic subset for simple industrial
  access.
- OPC UA can map identity, capabilities, diagnostics, and service commands for
  supervisory systems.
- MQTT Sparkplug can map edge state and command metrics where publish/subscribe
  integration is useful.
- PROFINET, EtherNet/IP, EtherCAT, CANopen, and IO-Link should remain future
  product or gateway decisions unless explicitly promoted into product scope.

This approach keeps relay behavior consistent even if several integration
surfaces are added later.

## Recommendation

Use this research to define a protocol-independent industrial command model
before designing any additional protocol. The model should be compact, honest
about hardware, and strict about safety:

- keep routine relay control narrow;
- separate status from measured telemetry;
- separate configuration and maintenance from output control;
- define ownership and lease behavior explicitly;
- expose stable identity, capability, health, and counter fields;
- put network-facing integration and richer security policy in host or gateway
  layers before adding native firmware networking.

PLC-facing protocols remain valuable examples, but they are references. The
main product direction is a clear industrial command model that can later be
mapped to whichever integration surfaces are justified.
