# Health detection methodology

Date: 2026-05-23

Status: Discussion. This note records research and design reasoning for
health detection methodology, including communication-loss safety and
host-control architecture. It does not change the authoritative PRD,
implementation plan, phase scope, protocol, or verification status unless those
documents are updated explicitly.

## Summary

Health detection is being explored for this relay controller because the board
may be used in unattended recovery workflows. Communication loss is one
important health-detection case: a lost host connection while a relay is
energized can leave the controlled equipment in an ambiguous state.
Flight-control software has mature patterns for health checks, arming gates,
link-loss failsafe behavior, and operator-visible fault reasons.

The useful lesson is not to copy aircraft behavior. UAV stacks solve navigation
and propulsion problems that this relay controller should not import. The
transferable pattern is to model communication health explicitly, expose stable
machine-readable state, and keep the safety action deterministic.

## Flight-control software reviewed

### PX4

PX4 centralizes health and arming checks through modular check classes and a
reporter. It publishes structured health and failsafe state, then uses that
state to decide whether specific operating modes can arm or run.

Relevant lessons:

- Separate health observation from operational gating.
- Prefer structured bitfields and status reports over ad hoc log text.
- Report only when state changes or when reporting is explicitly forced.
- Keep mode/action gating derived from health state instead of duplicating
  checks across callers.

References:

- <https://github.com/PX4/PX4-Autopilot/tree/main/src/modules/commander/HealthAndArmingChecks>
- <https://github.com/PX4/PX4-Autopilot/blob/main/msg/HealthReport.msg>

### ArduPilot

ArduPilot runs pre-arm checks periodically, rate-limits repeated user-facing
messages, and reports concrete reasons when arming is blocked. It also supports
check masks, but its documentation warns that disabling checks is mostly a
bench-test escape hatch.

Relevant lessons:

- Operator-facing reasons matter as much as the boolean pass/fail result.
- Repeated failure messages should be rate-limited.
- Check bypasses should be explicit and treated as exceptional.
- The same underlying health state can serve both automation and human
  troubleshooting.

References:

- <https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_Arming/>
- <https://ardupilot.org/copter/docs/common-prearm-safety-checks.html>

### Betaflight

Betaflight keeps compact arming-disable flags ordered by criticality. Those
flags drive user-visible feedback such as beeper warnings and OSD messages.

Relevant lessons:

- A small ordered fault mask works well on constrained firmware.
- The most important active reason should be easy to identify.
- Local indication can derive from the same machine state used by host tools.
- Compact status is useful when the device has limited UI surface.

References:

- <https://github.com/betaflight/betaflight/blob/master/src/main/fc/runtime_config.h>
- <https://betaflight.com/docs/wiki/guides/current/Arming-Sequence-And-Safety>

### INAV and Cleanflight

INAV and Cleanflight model receiver or link loss as staged failsafe state
machines. They distinguish link-loss detection, failsafe activation, landing or
drop behavior, landed/disarmed state, and recovery monitoring. INAV also
documents that re-arming after a failsafe can require a stable recovered signal
and a safe arm-switch position.

Relevant lessons:

- Link loss should be stateful, not just an expired timer.
- Detection delay and recovery delay are separate policy choices.
- Recovery should require stable conditions, not a single good packet.
- When a failsafe disarms outputs, re-enabling them should be explicit.

References:

- <https://github.com/iNavFlight/inav/blob/master/docs/Failsafe.md>
- <https://github.com/iNavFlight/inav/blob/master/src/main/flight/failsafe.c>
- <https://github.com/cleanflight/cleanflight/blob/master/src/main/flight/failsafe.c>

### Paparazzi UAV

Paparazzi exposes a simple datalink age counter in its datalink module and lets
airframe or mission logic decide what to do when the link has been quiet for
too long.

Relevant lessons:

- A heartbeat-age or datalink-age counter is easy to reason about.
- Link-loss action can be separated from link-loss measurement.
- Mission-specific behavior should not be hidden inside transport parsing.

References:

- <https://github.com/paparazzi/paparazzi/blob/master/sw/airborne/modules/datalink/datalink.c>
- <https://github.com/paparazzi/paparazzi>

### LibrePilot and OpenPilot

LibrePilot/OpenPilot use receiver status, system alarms, and arming gates.
Invalid or timed-out receiver inputs set receiver alarms and feed configured
failsafe channel values. Critical receiver or guidance alarms can force disarm
or block arming.

Relevant lessons:

- Health alarms and output commands should be separate objects.
- Link quality can degrade into warning state before becoming critical.
- A central arming/disarming path is easier to audit than scattered safety
  checks.
- Failsafe inputs can be substituted explicitly, but that pattern does not map
  directly to relay control because relays should fail to a clear off state.

References:

- <https://github.com/librepilot/LibrePilot/tree/next/flight/modules/Receiver>
- <https://github.com/librepilot/LibrePilot/tree/next/flight/modules/ManualControl>

## Lessons for this relay controller

The relay controller should borrow the state and reporting patterns, not the
aircraft-specific behavior. There is no equivalent of return-to-home, landing,
stick motion, sensor fusion, or arming switch recovery in this product.

Useful principles:

- Treat communication loss as explicit state: healthy, timed out, and reboot
  pending.
- Use machine-readable status fields and counters.
- Keep the safety action deterministic: force all relays off, then reboot after
  a short delay.
- Make the host-visible status authoritative; local LED and buzzer behavior
  should be secondary.
- Keep relay control single-owner so multiple host or transport paths cannot
  race output state.
- Avoid broad telemetry, navigation-style policy, or persistent relay-on state
  unless those are explicitly promoted into requirements.

## Applicable SmartPDU health-detection lessons

Leading SmartPDUs are not a direct model for this hardware, because they often
measure current, voltage, energy, power factor, environmental sensors, breaker
state, and outlet feedback that this controller does not measure. The useful
health-detection lesson is the structure of status and events.

Applicable findings:

- Model health as state plus counters plus events, not only logs.
- Keep health domains separate: controller health, transport or heartbeat
  health, relay GPIO command health, commanded relay state, and observed relay
  or load state only if future hardware adds feedback.
- Use clear severities such as normal, warning, fault, and reboot pending.
- Treat communication loss as a trip condition with a defined delay.
- Make recovery explicit; do not silently resume relay-on behavior after a
  communication-loss timeout.
- Record events for heartbeat timeout, off-all safety action, reboot
  scheduling, relay I/O failures, and daemon reconnects.
- Expose enough status for automation: current health state,
  timeout/deadline, counters, last event or reason, and firmware identity.
- Do not claim load power, relay contact closure, current, voltage, or outlet
  health without measurement hardware.
- In daemon mode, treat the daemon like a local management controller: it owns
  heartbeat, serial access, status cache, event log, and local API access.
- Keep startup behavior explicit and conservative: relay outputs default off
  unless a future requirement changes that.

## Firmware safety model explored

The explored firmware model is build-time-enabled communication-loss
supervision:

- The firmware starts a dedicated heartbeat deadline after relay GPIO
  initialization succeeds.
- The deadline starts at boot.
- The timeout is 30 seconds.
- Only a valid heartbeat command refreshes the deadline.
- Normal relay commands, status queries, and info queries do not refresh the
  deadline.
- If the deadline expires, firmware immediately calls `relay_off_all()`,
  records communication-loss timeout state, sets fault or reboot-pending local
  indication where available, and schedules a delayed cold reboot.
- The explored reboot delay is 500 ms, mainly to give the firmware time to
  apply off-all and expose local indication before reset.

Status fields considered:

- `comm_loss_safety_enabled`
- `comm_loss_timeout_ms`
- `comm_loss_deadline_ms`
- `comm_loss_state`
- `comm_loss_timeouts`
- `comm_loss_reboot_pending`

The distinction between timeout and deadline is important:

- `comm_loss_timeout_ms` is the configured interval length.
- `comm_loss_deadline_ms` is the current absolute uptime by which the next
  heartbeat must arrive.

Starting the deadline at boot is stricter than most RC failsafe designs. It
means a supervised build intentionally reboots if no host heartbeat arrives
within the timeout. That makes the host-side operating model important.

## Operator workflow evaluation

Several operator interfaces were considered.

Python interactive shell was rejected. It exposes Python syntax, object state,
exceptions, and arbitrary execution semantics to an operator-facing safety
workflow.

A domain-specific CLI session prompt was considered. It would look like a relay
command prompt rather than a Python REPL and could own the heartbeat while open.
This was not selected as the Linux production automation model, because it does
not solve cross-process coordination for short-lived shell and Python clients.
It remains a good fit for a Windows operator PC where a single long-lived
terminal session can own the COM port during manual recovery work.

Standalone `keepalive` was rejected. A separate heartbeat sidecar creates
lifecycle ambiguity and serial-port contention, especially because CDC serial
ports are normally exclusive.

`--script <file>` and stdin batch modes are deferred for the current direction.
They keep heartbeat ownership inside one process, but they still make short
automation commands awkward and do not solve cross-process coordination.

Linux and Windows have different production host-control shapes. Linux
operator ergonomics favor daemon ownership. Windows operator ergonomics favor a
domain-specific CLI session that owns the assigned COM port while open.

## Host-control mode selection

Three host-control modes should remain supported, but the production owner for
a given platform should be explicit.

Direct mode is the current implementation shape. Host tools open the USB CDC
serial device directly and send relay management commands to firmware. This is
useful for bench testing, bring-up, diagnostics, and simple deployments where a
long-running owner is not desired.

Session mode is the Windows production operator shape. A long-lived
`rp2350-relay --port COM7 session` process owns the Windows COM port while
open, serializes commands through the direct host client, and can later own
firmware heartbeat if communication-loss safety is promoted into scope.

Daemon mode is the Linux production automation shape. A local daemon owns the
USB CDC serial device, heartbeat, command serialization, reconnect behavior,
and shutdown policy. Short-lived CLI and Python clients talk to the daemon
instead of opening the serial device.

Mode selection should be explicit in operator guidance and release profiles. A
Windows session workflow should not imply that a separate keepalive sidecar is
available, and a Linux daemon workflow should not allow normal operator relay
control through direct serial commands while the daemon is running. This avoids
two independent host processes racing the same exclusive serial device or
splitting heartbeat ownership.

Diagnostics may still need an escape hatch, but it should be deliberate. For
example, daemon-mode tooling can require the daemon to be stopped before direct
serial diagnostics are allowed.

## Windows session model

The preferred direction for Windows production operation is a local CLI
session.

`rp2350-relay --port COM7 session` would own:

- The assigned Windows COM port.
- Command serialization while the session is open.
- The future firmware heartbeat, if communication-loss safety is later added.
- Operator-visible command results and errors.

The session should reuse the existing direct host client and CLI conventions:
one-based channel arguments, board labels such as `CH1` through `CH6`, typed
exceptions, and human or JSON response formatting where practical.

One-shot direct commands remain useful for diagnostics and simple checks:

```powershell
rp2350-relay --port COM7 status
rp2350-relay --port COM7 off-all
```

Those commands are not the Windows heartbeat-owner model once communication
loss safety exists.

## Linux daemon model

The preferred direction for Linux production operation is a local daemon.

`rp2350-relayd` would own:

- The USB CDC serial device, typically `/dev/ttyACM*`.
- The firmware heartbeat.
- Command serialization.
- Device reconnect after firmware reboot or USB serial reset.
- Best-effort `off-all` on clean daemon shutdown.
- A cached view of recent status, communication-loss state, and recent
  health/event records.

`rp2350-relayctl` would be a short-lived local client that talks to the daemon
instead of opening the serial port directly.

Python automation would use a daemon client, for example:

```python
from rp2350_relay_6ch import RelayDaemonClient

with RelayDaemonClient.connect() as relay:
    relay.set_relay(0, True)
    relay.pulse_relay(1, 100)
    status = relay.get_status()
    relay.off_all()
```

The Python process would connect to a local Unix domain socket. The daemon
would serialize the request, send the actual relay management command to
firmware, and return the decoded response. If the Python script exits or
crashes, the daemon remains the heartbeat owner.

This shape fits bash automation as well:

```sh
rp2350-relayctl status
rp2350-relayctl pulse 1 100
rp2350-relayctl off-all
```

The daemon keeps supervision continuous across those short commands.

## Non-privileged Linux operation

Daemon mode should be friendly to a non-privileged operator account:

- No root daemon in the normal control path.
- No privileged socket path.
- No `sudo` for ordinary relay commands.
- Serial access handled through user group membership, such as `dialout`, or a
  documented udev rule for the device.
- Runtime files placed under `$XDG_RUNTIME_DIR` when available, falling back to
  `/run/user/$UID/`.
- Unix socket path owned by the operator user, for example
  `$XDG_RUNTIME_DIR/rp2350-relay.sock`.
- Lock files or PID files, if needed, also live under the user runtime
  directory.

The recommended Linux deployment path is a `systemd --user` service under the
operator account. That keeps lifecycle management in the user session rather
than requiring a system service.

Example management commands to document during implementation planning:

```sh
systemctl --user start rp2350-relayd
systemctl --user stop rp2350-relayd
systemctl --user status rp2350-relayd
journalctl --user -u rp2350-relayd
```

An optional enable-on-login flow can be considered later:

```sh
systemctl --user enable rp2350-relayd
```

Exact unit-file content, installation helpers, and whether lingering should be
recommended are implementation-planning questions, not decisions made by this
discussion note.

## Open questions

1. Should the daemon run foreground-only at first, or should a user systemd
   unit be part of the first daemon implementation?
2. Should the local daemon protocol use newline-delimited JSON, CBOR, or
   another framing?
3. Should daemon clients be limited to the same Unix user, or should group
   access be supported through socket permissions?
4. May the daemon keep relays on indefinitely when no client is connected, as
   long as the daemon itself is healthy and sending heartbeat?
5. What is the correct policy if the daemon loses firmware communication while
   relays were commanded on but firmware has not yet timed out?
6. How should the daemon rediscover the serial device after firmware reboot,
   USB disconnect, or `/dev/ttyACM*` renumbering?
7. What host-side audit fields are required for relay operations: user,
   command, channel, timestamp, firmware identity, result, and daemon version?

## Suggested direction

Use the flight-control study to justify a small, explicit communication-loss
state model in firmware, but keep the host operation model platform-specific:
session-owned on Windows and daemon-owned on Linux.

The near-term direction should be:

- Dedicated firmware heartbeat.
- Deterministic timeout action: `off-all` then delayed reboot.
- Windows session owns heartbeat and COM-port access.
- Linux daemon owns heartbeat and USB CDC serial access.
- Short-lived Linux CLI and Python clients talk to the daemon.
- Direct, session, and daemon modes are supported for their intended roles, but
  not enabled together for normal operation on the same serial device.
- Non-privileged user operation through `$XDG_RUNTIME_DIR` and `systemd --user`.
- No standalone keepalive command, no Python REPL, and no separate sidecar
  heartbeat process.
