# RP2350 dual-core on Zephyr: current support, gaps, and useful experiments
https://github.com/NoNine/rp2350-relay-6ch/discussions/1

RP2350 is interesting because it is not just a faster RP2040 successor. The
silicon includes two Cortex-M33 cores and two Hazard3 RISC-V cores, with the
boot image selecting which architecture pair is used. That makes the Pico 2
family and RP2350B boards attractive for small products that want one core for
hard real-time work and another core for protocol, UI, logging, or experiments.

For this relay controller project, the current production path is deliberately
single-core: a Zephyr image for the RP2350B Cortex-M33 target controls six relay
outputs and exposes a USB MCUmgr/SMP command channel. In this repository,
`SMP` currently means Zephyr's Simple Management Protocol, not Zephyr symmetric
multiprocessing.

This discussion is meant to explore what useful RP2350 dual-core support in
Zephyr could look like, and which experiments would be worth trying first.

## Current Zephyr Status

As of 2026-05-21, Zephyr's Raspberry Pi Pico 2 board documentation says the
board can run Zephyr on a single Cortex-M33 core or a single Hazard3 RISC-V core.
The same page also states that Zephyr currently does not support running code on
the second core for that target.

Relevant upstream docs:

- Zephyr Pico 2 board docs:
  <https://docs.zephyrproject.org/latest/boards/raspberrypi/rpi_pico2/doc/index.html>
- Zephyr symmetric multiprocessing:
  <https://docs.zephyrproject.org/latest/kernel/services/smp/smp.html>
- Zephyr OpenAMP sample:
  <https://docs.zephyrproject.org/latest/samples/subsys/ipc/openamp/README.html>
- Zephyr SMP Pi sample:
  <https://docs.zephyrproject.org/latest/samples/arch/smp/pi/README.html>
- Raspberry Pi RP2350 product page:
  <https://www.raspberrypi.com/products/rp2350/>

Zephyr already has general multicore patterns elsewhere:

- Symmetric multiprocessing (`CONFIG_SMP`): one Zephyr kernel schedules threads
  across supported CPUs.
- Asymmetric multiprocessing / OpenAMP: separate images run on separate cores
  and communicate through IPC.
- Mailbox, IPM, IPC service, and remote-shell examples exist for other SoCs.

The missing piece is not whether Zephyr has multicore concepts. The question is
what RP2350-specific support should look like, and what minimum useful feature
set would justify the complexity.

## Why It Might Matter

Some possible RP2350 use cases:

- Keep safety-critical GPIO control on one core while USB, serial protocol,
  logging, or host-facing management runs on the other.
- Reserve one core for deterministic timing loops, PIO coordination, DMA refill,
  software-defined protocols, pulse trains, audio, or video generation.
- Split emulator-style workloads so one core handles timing-sensitive display or
  audio while the other handles application logic.
- Use one core as a supervised worker for measurement, signal processing, or
  background diagnostics.
- Explore TrustZone, secure boot, and RP2350 protection features with stricter
  isolation boundaries.
- Run Hazard3/RISC-V experiments without disturbing the production M33 firmware
  path.

For this relay board specifically, dual-core is not required to switch relays
safely. The interesting question is whether a second core could improve
isolation, latency, or observability once the device grows beyond basic relay
control.

## Questions For The Community

1. What should the first useful RP2350 dual-core Zephyr milestone be?
   - Full `CONFIG_SMP` support?
   - AMP/OpenAMP with separate images?
   - A smaller RP2350-specific core1 launcher and mailbox example?

2. Which proof-of-concept would provide the best signal?
   - Core0 runs normal Zephyr shell or USB CDC, core1 toggles timing-critical
     GPIO.
   - Core0 owns host protocol, core1 owns relay-safe-state enforcement.
   - Core0 sends commands to a core1 worker through a minimal shared-memory
     queue.
   - A PIO/DMA/audio/video demo where second-core work is visibly useful.

3. What RP2350 platform pieces are missing upstream?
   - Second-core boot/startup code.
   - Shared-memory layout conventions.
   - Mailbox or interrupt plumbing.
   - Devicetree bindings for core-local resources.
   - Debug and flashing support for dual images.
   - Clear docs explaining M33 versus Hazard3 target selection.

4. Should RP2350 support aim first at symmetric scheduling or explicit
   partitioning?
   - SMP is ergonomic for normal Zephyr applications.
   - AMP may be easier to reason about for timing, isolation, and mixed
     firmware experiments.

5. Are there existing community projects that show compelling RP2350 second-core
   workloads?
   - Especially projects involving PIO, DMA, VGA/video, audio, emulation,
     deterministic protocols, or split real-time/control-plane designs.

## Practical Constraints

For a relay controller, the safety bar stays the same regardless of core count:

- Relay GPIOs must default off on boot, reset, firmware restart, and test
  teardown.
- GPIO26 through GPIO31 must remain dedicated to relay outputs.
- Dual-core experiments must not leave relay state ambiguous if one core hangs,
  resets, or stops communicating.
- Any host protocol change needs deterministic error behavior and test coverage.

That suggests an initial experiment should keep relay behavior simple and make
the second core observable without making it responsible for unsafe side
effects.

## Suggested First Experiment

A conservative first experiment would be:

- Core0: normal Zephyr application, shell or USB CDC logging, and host-visible
  status.
- Core1: tiny worker that increments a counter, toggles a non-relay test GPIO,
  or services a shared-memory command queue.
- IPC: minimal shared-memory mailbox or queue with explicit heartbeat and
  timeout behavior.
- Success criteria: both cores boot, core0 can observe core1 progress, core0 can
  detect a stalled core1, and relay outputs remain off unless commanded through
  the existing single-core-safe path.

That would not solve full RP2350 SMP, but it would create a concrete base for
deciding whether Zephyr should grow RP2350 AMP support, SMP support, or both.
