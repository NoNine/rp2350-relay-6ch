# Health models in embedded control systems

Date: 2026-06-01

Status: Discussion. This note records research and design reasoning for health
models in MCU-based embedded control systems. It does not change the
authoritative PRD, implementation plan, phase scope, protocol, firmware
configuration, release artifacts, or verification status unless those documents
are updated explicitly.

## Summary

A health model is the structured answer to "what is wrong, how fresh is that
knowledge, what is allowed now, and what recovery action is justified?" It is
not the same thing as a watchdog. A watchdog is one recovery mechanism. The
health model decides whether the system is healthy enough to feed that watchdog,
confirm a firmware image, accept an operation, show a warning, or force a safe
state.

Good embedded health models separate observation from action:

- observation records signals, deadlines, counters, and reasons;
- classification turns those observations into normal, warning, degraded,
  fault, or recovery-pending state;
- gating decides which operations are allowed;
- action performs deterministic recovery, such as all-off, reboot, rollback, or
  refusal to arm;
- reporting exposes machine-readable reasons to host tools and concise local
  indications to operators.

For the RP2350 relay controller, the useful model is smaller than a flight
controller or automotive ECU. It should not import navigation, sensor fusion,
or safety-certification complexity. The transferable idea is to keep one
coherent health state that communication-loss safety, watchdog supervision,
firmware update confirmation, status output, and local indicators can consume.

## Health model concepts

### Domains

A health domain is a bounded area whose state can be observed and reasoned
about independently. Domains keep checks from turning into one vague
"unhealthy" boolean.

Useful embedded domains include:

- boot and update state;
- scheduler and task liveness;
- communication freshness;
- command parser and RPC readiness;
- actuator or GPIO safe state;
- storage and configuration;
- local indicators;
- measured power, current, voltage, temperature, or sensors when hardware
  exists.

For this relay controller, domains that are not measured must not be claimed.
The board can know commanded relay state and GPIO setup status. It cannot know
relay contact closure, load current, load voltage, or downstream equipment
health without added measurement hardware.

### Signals

Signals are the raw facts behind health state. A signal should have a source, a
freshness rule, and a failure interpretation.

Examples:

- a worker heartbeat counter advanced within a deadline;
- relay GPIO initialization succeeded;
- all relay outputs are commanded off during boot;
- RPC service registered and can answer status;
- host heartbeat arrived before the communication-loss deadline;
- bootloader image state is readable;
- reset reason indicates watchdog, power-on, software reboot, or brownout;
- repeated invalid commands crossed an attention threshold.

Signals should be explicit enough that firmware can report a reason and tests
can force the condition.

### State and severity

An embedded health model usually needs more than healthy/unhealthy:

- `normal`: required domains are fresh and no action is needed.
- `warning`: something needs attention, but the current operation remains
  allowed.
- `degraded`: the controller can keep serving a reduced or cautious operating
  mode.
- `fault`: a required condition failed and one or more operations are blocked.
- `recovery_pending`: a deterministic recovery action is scheduled or in
  progress, such as reboot, rollback, or forced all-off.

The exact names can differ, but the distinction matters. Warning is not fault.
Fault is not always reboot. Degraded operation should be deliberate, not a
silent side effect of missing data.

### Reasons, counters, and events

Operators and automation need reasons, not only state colors. A compact model
typically exposes:

- active reason flags;
- highest-priority reason for tiny local displays or buzzers;
- counters for timeouts, rejected commands, reboot requests, and fault
  transitions;
- last event or last fault reason;
- timestamp or uptime age for freshness-sensitive domains.

Counters and events serve different jobs. Counters are stable automation data.
Events explain transitions and can be missed on reset or reconnect.

### Freshness

Stale data is a health condition. A relay controller can report "last heartbeat
age" or "deadline expired" even when the serial transport is not actively
throwing errors.

Freshness rules should be domain-specific:

- heartbeat freshness can be a strict deadline;
- worker liveness can be a missed-period count;
- boot health can be a one-time startup gate;
- configuration health can change only after read/write operations;
- local indicator health may be advisory and should not block relay control
  unless a later product requirement says otherwise.

## Embedded and control-system patterns

### PX4

PX4 health and arming checks separate component health from mode gating. PX4
runs checks repeatedly, keeps current failed checks, emits events when the list
changes, and exposes health reports as structured fields such as component
presence, warning flags, error flags, arming-check flags, and per-mode
arm/run capability.

Relevant lessons:

- Health state can be continuously evaluated, not only checked at command time.
- Different modes may require different health domains.
- Structured flags and events are both useful: flags describe current state,
  events explain changes.
- A ground station should be able to show exact failure reasons, while local
  LEDs and tones remain secondary.

References:

- <https://docs.px4.io/main/en/advanced_config/prearm_arm_disarm>
- <https://docs.px4.io/main/en/msg_docs/HealthReport>
- <https://docs.px4.io/main/en/msg_docs/ArmingCheckReply>
- <https://docs.px4.io/main/en/flying/pre_flight_checks>

### ArduPilot

ArduPilot pre-arm checks block arming when configuration, calibration, sensor,
power, logging, mission, or internal checks fail. It reports concrete failure
messages to the ground station, repeats failure messages at a controlled rate,
and warns that disabling checks should be exceptional.

Relevant lessons:

- Blocking an operation is more useful when the reason is clear.
- Operator-facing reasons should map to corrective actions.
- Check bypasses are dangerous unless they are explicit, visible, and reserved
  for bench or exceptional use.
- Some checks are startup gates; others remain relevant during operation.

Reference:

- <https://ardupilot.org/copter/docs/common-prearm-safety-checks.html>

### Betaflight

Betaflight uses compact arming-disable flags. Those flags can be shown through
CLI, OSD, app UI, and beeper patterns. When local output is constrained, the
system reports the most important active reason.

Relevant lessons:

- A bitmask of active reasons is practical on constrained firmware.
- Reason priority matters when only one LED, buzzer, or short text field is
  available.
- Local indications should derive from the same machine state used by host
  tools.
- Compact status still needs version-aware documentation because flag meanings
  are part of the operator interface.

Reference:

- <https://betaflight.com/docs/wiki/guides/current/Arming-Sequence-And-Safety>

### AUTOSAR Watchdog Manager

AUTOSAR's Watchdog Manager is heavier than this project needs, but its
supervision vocabulary is valuable. It separates software units into supervised
entities and checkpoints, then applies three supervision styles:

- alive supervision: a periodic entity runs not too rarely and not too often;
- deadline supervision: a transition between checkpoints completes within
  configured time bounds;
- logical supervision: checkpoints occur in an expected order.

It also distinguishes local supervision status for each supervised entity from
global supervision status for the MCU.

Relevant lessons:

- Liveness alone is incomplete; timing and sequence correctness matter.
- Coarse checkpoints are simple but detect fewer failures.
- Fine-grained checkpoints detect more but add configuration and test burden.
- Local component health and global controller health should be separate
  concepts.

Reference:

- <https://www.autosar.org/fileadmin/standards/R24-11/CP/AUTOSAR_CP_SWS_WatchdogManager.pdf>

### RTOS monitor-task pattern

Across RTOSes, a common product pattern is a monitor task that receives
heartbeats, counters, or checkpoint reports from critical workers. The monitor
computes health state and optionally feeds a hardware watchdog.

Relevant lessons:

- Workers should report progress; they should not feed the hardware watchdog
  directly.
- Each worker needs a deadline that matches its real duty cycle.
- Long operations need explicit "busy but healthy" handling.
- The monitor should report which check failed before allowing reset when that
  is feasible.

### Boot and update health

Boot health is different from runtime health. A firmware image can be healthy
enough to boot but not healthy enough to confirm itself for future boots.

Useful boot/update checks:

- relay outputs reached the configured safe state;
- required services initialized;
- firmware can read boot/update state;
- reset reason and boot phase are available;
- no required worker has failed during the confirmation window.

The important rule is that confirmation must depend on health, not merely on
surviving for a fixed delay.

## Health model shape for this relay controller

This section is exploratory. It is not an implementation approval.

### Health domains

The smallest useful future model for this project would track these domains:

- `relay_gpio`: relay GPIO subsystem initialized and commanded safe state is
  known.
- `rpc`: management service initialized and able to answer commands.
- `comm_owner`: host ownership or heartbeat freshness, only for profiles that
  enable communication-loss supervision.
- `boot_update`: boot slot, image state, and confirmation readiness.
- `watchdog_supervisor`: firmware workers or checkpoints are fresh enough to
  feed a hardware watchdog if watchdog support is enabled.
- `indicator`: optional local LED/buzzer/display health, advisory only unless a
  later requirement says otherwise.

Domains should be added only when the firmware can measure them. Do not add
domains for load power, relay contact feedback, mains behavior, network state,
or environmental state without hardware and requirements.

### Overall state

A compact state model could be:

- `booting`: initialization is still in progress.
- `normal`: relay controller is ready and no attention state is active.
- `relay_active`: one or more relays are commanded on or pulsing.
- `degraded`: controller is usable but needs attention.
- `fault`: required health failed; affected operations are blocked or safe
  action is latched.
- `recovery_pending`: reboot, rollback, or communication-loss recovery is
  scheduled or in progress.

This aligns with existing local indicator vocabulary while keeping host-visible
status authoritative.

### MVC-style ownership

Model/View/Controller is only an analogy here, but it helps keep ownership
clear in firmware:

| MVC role | Firmware role | Owns |
| --- | --- | --- |
| Model | `health` module | Stable controller truth: health state, active reasons, relay masks, readiness, failures, communication-owner freshness, recovery pending, and transition count. |
| Controller | `main.c`, `relay.c`, `relay_mgmt.c` | Boot progress, relay GPIO results, host commands, heartbeat renewals, timeouts, and reboot requests; publishes facts to health. |
| View | Host `status`, RGB LED, buzzer, OLED | Reads `health_snapshot` and formats or renders it for automation or operators. |
| View-local state | `indicator.c` presentation internals | Command flash windows, buzzer timing, OLED POST/write status, pulse display timing, hardware availability, and last rendered output. |

Data flow:

```text
host command / boot / relay event / timeout
        |
        v
controller code: main.c / relay.c / relay_mgmt.c
        |
        v
model: health facts -> derived health_snapshot
        |
        +--> status response
        |
        v
view: indicator renders stable summary + local transients
```

The view boundary is important. A local indicator can and should keep local
presentation state: accepted/rejected/busy flash windows, buzzer queue and beep
timing, OLED POST and write-path status, display pulse progress timing,
hardware availability, and last rendered output. None of that should become a
second source of stable controller-health truth.

For example, health can say "state is `degraded`, primary reason is
`comm_owner_timeout`, relay mask is `0x00`." The indicator view can decide to
show a yellow pulse, `OWNER` detail text, and a bounded beep pattern. The status
view can expose the same facts as structured fields. Both views are consistent
because neither derives its own independent controller state.

This also explains why command feedback is not a health reason by default.
Accepted, rejected, and busy command indications are useful local UX, but they
are transient presentation events unless a later contract defines thresholds
that promote repeated outcomes into health reasons.

### Reason flags

Potential future reason flags:

- `relay_gpio_init_failed`
- `relay_safe_state_failed`
- `rpc_not_ready`
- `comm_owner_timeout`
- `comm_reboot_pending`
- `watchdog_supervisor_failed`
- `boot_state_unreadable`
- `image_unconfirmed`
- `update_confirm_blocked`
- `indicator_degraded`
- `invalid_command_threshold`
- `busy_pulse_threshold`

Reason flags should be stable once exposed. Before exposing them through a
protocol or CLI, the project should decide naming, versioning, and compatibility
rules.

### Gating and actions

The health model should drive gates, not hide actions in unrelated modules.

Example gates:

- Firmware image confirmation requires relay safe state, RPC readiness, and
  boot/update state readability.
- Hardware watchdog feeding requires required domains to be fresh.
- Communication-loss timeout may force all relays off and optionally schedule
  reboot, but it should be reported as owner health, not generic firmware
  death.
- Relay control commands require relay GPIO health and valid command input.
- Local indicators derive from health state and must not block relay teardown,
  `off-all`, reboot scheduling, or RPC responses.

Example actions:

- warning: increment counter, set reason flag, update local indication.
- degraded: expose host-visible reason and keep safe operations available.
- fault: block affected operations and force a deterministic safe action where
  needed.
- recovery pending: expose pending action and deadline, then perform the
  scheduled reboot or rollback if recovery conditions are not met.

### Reporting

If promoted later, host-visible status should prefer structured fields over log
text:

- overall health state;
- active reason flags;
- highest-priority reason;
- domain freshness or deadline for communication-owner/watchdog domains;
- counters for owner timeouts, watchdog resets, rejected commands, busy pulses,
  and recovery actions;
- reset reason and last boot phase where supported;
- firmware identity and boot/update state.

Local indicators should remain compressed summaries. A single RGB LED or buzzer
cannot be the authoritative health interface.

## What this means for watchdogs

The watchdog research note says a watchdog is a recovery mechanism, not a
health model. This note gives the missing model:

- supervised domains report progress and reasons;
- the health supervisor computes state;
- watchdog feeding is one consumer of that state;
- firmware update confirmation is another consumer;
- communication-loss safety is another consumer;
- operator status and local indicators are reporting consumers.

That shape avoids both common watchdog mistakes:

- feeding the watchdog from a path that proves only that a timer or idle thread
  still runs;
- resetting the device without preserving enough reason data to troubleshoot
  why it reset.

## Zephyr support assessment

Zephyr provides good building blocks for this model, but not a complete
product-level health model. The application still needs to own health domains,
reason flags, gates, recovery policy, and host-visible status shape.

Useful Zephyr building blocks:

- Hardware watchdog drivers provide the final reset mechanism.
- Task watchdog can supervise multiple threads and can use a hardware watchdog
  fallback where supported.
- MCUmgr/SMP can expose status, image state, statistics, and custom management
  groups over the existing host transport.
- Zephyr statistics can support counters for timeouts, rejected commands,
  watchdog resets, and recovery actions.
- Logging is useful for diagnostics, but logs should not be the health API.
- zbus can help if health state later has multiple producers and consumers,
  but it is optional and may be too much for the current firmware shape.
- Devicetree and Kconfig are good fits for selecting watchdog, indicator, and
  product-profile behavior at build time.
- MCUboot integration is the right foundation for boot/update health gates and
  image confirmation.

What Zephyr does not provide:

- A standard `normal` / `degraded` / `fault` / `recovery_pending` controller
  health state.
- Product-specific reason flags or priority rules.
- Policy for which domains are required to confirm an image, feed a watchdog,
  accept relay commands, or show local indication.
- Truth about relay contacts, load current, load voltage, or external equipment
  state without measurement hardware.

For this project, the likely best fit is an application-owned `health` module
that consumes relay, RPC, communication-owner, boot/update, watchdog-supervisor,
and indicator signals. Zephyr watchdog, task watchdog, MCUmgr, stats, logging,
Kconfig, and MCUboot should support that module rather than replace it.

## Research takeaways

- Health is structured state plus reasons, not a boolean.
- Observation, classification, gating, action, and reporting should be separate
  steps.
- A compact reason bitmask plus counters is a good fit for constrained
  firmware.
- Different operations can require different health domains.
- Recovery should require stable conditions, not one good packet or one good
  loop iteration.
- Local LED/buzzer output should derive from host-visible health state, not
  replace it.
- Do not claim health for domains the hardware cannot observe.
- For this relay controller, the first useful health model should stay small:
  relay GPIO, RPC readiness, communication owner freshness, boot/update
  confirmation, watchdog supervisor, and optional indicator health.

## References

- PX4 pre-arm and arming configuration:
  <https://docs.px4.io/main/en/advanced_config/prearm_arm_disarm>
- PX4 health report:
  <https://docs.px4.io/main/en/msg_docs/HealthReport>
- PX4 arming check reply:
  <https://docs.px4.io/main/en/msg_docs/ArmingCheckReply>
- PX4 flight-readiness status:
  <https://docs.px4.io/main/en/flying/pre_flight_checks>
- ArduPilot pre-arm safety checks:
  <https://ardupilot.org/copter/docs/common-prearm-safety-checks.html>
- Betaflight arming sequence and safety:
  <https://betaflight.com/docs/wiki/guides/current/Arming-Sequence-And-Safety>
- AUTOSAR Watchdog Manager:
  <https://www.autosar.org/fileadmin/standards/R24-11/CP/AUTOSAR_CP_SWS_WatchdogManager.pdf>
- Hardware watchdog research note:
  <hardware-watchdogs-mcu-systems.md>
- Existing health detection discussion:
  <health-detection-methodology.md>
