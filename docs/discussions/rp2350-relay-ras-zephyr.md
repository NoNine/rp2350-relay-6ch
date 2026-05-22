# RAS ideas for an unmanned server-room relay controller

This discussion explores how server-style RAS ideas could apply to the
RP2350-Relay-6CH project. In server language, RAS usually means Reliability,
Availability, and Serviceability. The relay controller is much smaller than a
server, but the operating context is similar in one important way: it may be
installed in a remote, unmanned server room where physical access is slow,
expensive, or impossible during an incident.

This is not a new requirements document. The intent is to identify useful
directions and tradeoffs while keeping the v1 implementation grounded in the
current hardware and Zephyr support.

## Why RAS matters here

A relay controller in a server room is often part of a recovery path. It might
power-cycle a modem, reset an out-of-band management appliance, switch an
auxiliary circuit, or drive a staged recovery sequence. If that controller is
unavailable or ambiguous, the recovery path may fail exactly when it is needed.

For this project, RAS should start from a few practical rules:

- Relay state must be predictable after boot, reset, firmware restart, and
  test teardown.
- Remote commands must be bounded, validated, and observable.
- Firmware updates must not turn a working controller into an unreachable one.
- Operators need enough identity, status, and diagnostic information to decide
  what to do without opening the enclosure.
- Future features should improve recovery confidence without making relay state
  harder to reason about.

## Reliability

Reliability is about correct behavior over time. For a relay controller, that
means the safest state is explicit, every command is checked, and failures are
reported in a form that host tooling can handle.

Current reliability pieces already present in the project:

- Six relay outputs are modeled explicitly and default off.
- Relay commands validate channels, masks, pulse durations, and CBOR payloads.
- Pulse durations are bounded, and overlapping pulse requests return busy
  instead of silently extending or replacing active pulses.
- `off_all` cancels active pulses and forces the state back to off.
- The custom MCUmgr/SMP relay group returns stable structured errors.
- Host-side code uses typed exceptions for validation, timeout, transport,
  protocol, and device failures.
- Firmware and host tests cover default-off behavior, relay state changes,
  invalid channels, pulse bounds, busy pulses, and teardown safety.

Useful reliability hardening ideas:

- Add a watchdog or task watchdog path once the firmware has long-running
  services beyond simple command handlers.
- Make health checks explicit enough that update confirmation depends on relay
  GPIO initialization, safe state, management service startup, and boot-state
  visibility.
- Keep relay control single-owner. Even if future transports are added, one
  firmware module should arbitrate final relay state.
- Treat communication loss carefully. A forced "all off after silence" policy
  may be useful for some loads and harmful for others, so it should be an
  explicit operating mode rather than an assumed default.
- Avoid persisting relay-on state across reboot unless there is a strong
  product reason and a clear safety procedure.

## Availability

Availability is about keeping the recovery path usable. This project cannot
make the attached server room equipment highly available by itself, but it can
avoid becoming the weakest link in a remote recovery workflow.

Current availability pieces already present:

- USB CDC MCUmgr/SMP transport provides a packet-based host command path.
- The Python library and CLI support timeouts, retries, JSON output, and
  scripted smoke checks.
- `status`, `info`, and `build_info` commands expose enough state for basic
  automation and operator checks.
- A controlled reboot command exists for update and debug workflows.

Planned availability work:

- MCUboot-compatible A/B firmware update.
- Signed image upload to the inactive slot.
- Test boot, health-check-based confirmation, and rollback when a new image
  fails to confirm.
- Release artifacts that pair host tooling with matching firmware images.

Possible future availability work:

- RS485 as a longer-distance or electrically isolated management path when USB
  is not the right operational interface.
- Wi-Fi or other network management only after authentication, update safety,
  and remote recovery behavior are defined.
- External monitoring that periodically runs `status` or a lightweight health
  check and alerts on missing responses, unexpected relay state, or repeated
  errors.
- Redundant control paths, such as local USB plus isolated field bus, with one
  relay arbiter in firmware so transports do not race each other.

## Serviceability

Serviceability is the ability to understand, repair, and upgrade the system
without guesswork. In an unmanned room, serviceability often matters as much as
the relay action itself, because the operator may only have logs, serial ports,
and release artifacts.

Current serviceability pieces already present:

- The CLI exposes human-readable and JSON output.
- The protocol exposes command counters and basic transport capability fields.
- Build identity includes application version, Zephyr version, board target,
  source commit, dirty-tree flag, timestamp, and compiler identity.
- UART shell and Zephyr logging are available for bench debug.
- Verification reports document phase checks, hardware used, skipped checks,
  safety notes, and command outcomes.
- Smoke-test procedures require confirming that no relay is left on.

Useful serviceability hardening ideas:

- Keep release artifacts easy to match: wheel plus board-specific UF2 images
  from the same release.
- Add persistent fault history only if there is a clear flash-wear policy and a
  concise operator-facing output format.
- Consider a compact "support bundle" command in host tooling that collects
  `info`, `build_info`, `status`, and relevant host environment details.
- Preserve a bench/debug path that does not share bytes with the production
  relay RPC transport.
- Document recovery drills for failed firmware update, unreachable device,
  unexpected relay state, and operator PC serial-port changes.

## Zephyr support today

Zephyr already provides many of the building blocks this project needs for
practical v1 RAS:

- GPIO, USB CDC ACM, logging, shell, reboot, hardware-info, and devicetree
  support cover the basic board-control and diagnostic surface.
- MCUmgr provides remote management operations, including image, OS, settings,
  shell, statistics, and Zephyr management groups over transports such as
  serial, BLE, and UDP:
  <https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html>
- The project already uses an application-specific MCUmgr/SMP group for relay
  commands, while leaving room for standard image management later.
- Zephyr DFU provides image-management and bootloader interface pieces, and
  Zephyr is directly compatible with MCUboot when MCUboot is the selected
  bootloader:
  <https://docs.zephyrproject.org/latest/services/device_mgmt/dfu.html>
- Zephyr task watchdog support can supervise multiple firmware tasks and can
  use a hardware watchdog as a fallback on supported hardware:
  <https://docs.zephyrproject.org/latest/services/task_wdt/index.html>
- As of 2026-05-22, Zephyr 4.4.0 is the latest stable release, Zephyr 3.7.0 is
  the current LTS3 release, and Zephyr 4.5 is the next planned release:
  <https://docs.zephyrproject.org/latest/releases/index.html>

That means the near-term RAS path should rely on normal single-core Zephyr
subsystems first: watchdogs, MCUmgr, MCUboot-compatible update flow, structured
status, and clear release practices.

## Zephyr support to watch

Future Zephyr development may make stronger RAS patterns practical on RP2350,
but the project should not depend on those features for v1.

The main RP2350-specific gap is second-core support. The upstream Raspberry Pi
Pico 2 board documentation currently says Pico 2 can run Zephyr on either a
single Cortex-M33 or a single Hazard3 RISC-V core, and that there is no support
for running code on the second core:
<https://docs.zephyrproject.org/latest/boards/raspberrypi/rpi_pico2/doc/index.html>

If that changes, second-core support could become interesting for RAS:

- One core could own relay-safe-state enforcement while the other handles
  protocol, logging, or network management.
- A second core could provide a heartbeat or independent diagnostic worker.
- AMP or explicit partitioning may be easier to reason about than symmetric
  scheduling for safety-oriented relay behavior.

Those ideas should remain exploratory until upstream RP2350 support is real
enough to test. See the existing dual-core discussion for that topic:
[RP2350 dual-core on Zephyr](rp2350-dual-core-zephyr.md).

## Open questions

1. Which failures should the device recover from automatically, and which
   should require an explicit operator command?
2. Should communication loss ever force all relays off, or should v1 keep relay
   state entirely command-driven?
3. What is the minimum useful health check before confirming a new firmware
   image?
4. Which status fields would be most useful to remote operators during an
   incident?
5. When RS485 or Wi-Fi is added, should it be an alternate transport for the
   same management protocol or a separate operating mode?
6. What service artifacts should every release include so an operator can
   recover a remote installation with minimal repository knowledge?

## Suggested direction

For v1, adopt RAS as an operational discipline rather than a large architecture
change:

- Keep relay behavior fail-safe and deterministic.
- Complete MCUboot-backed update and rollback before adding remote network
  control.
- Add watchdog and health checks when there is enough firmware surface to
  supervise meaningfully.
- Expand status, build identity, and host diagnostics in small, scriptable
  steps.
- Treat RP2350 dual-core and redundant control paths as future experiments,
  not prerequisites for reliable server-room use.
