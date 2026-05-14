# RP2350 Relay 6CH

Zephyr firmware and Python host tooling for the Waveshare RP2350-Relay-6CH
controller.

This repository is currently at Phase 0: project baseline and build harness.
The firmware builds as a minimal Zephyr application, the Python package imports,
and wrapper scripts provide repeatable local entry points. Relay GPIO control,
USB RPC, CLI tooling, and firmware update support are planned but not
implemented yet.

## Current Status

- Development board target: `rpi_pico2/rp2350a/m33/w` for Raspberry Pi Pico 2 W.
- Final hardware target: Waveshare RP2350-Relay-6CH.
- Relay outputs: `CH1` through `CH6` on GPIO26 through GPIO31.
- Relay polarity: active high unless board testing proves otherwise.
- Safety requirement: all relays default off on boot, reset, firmware restart,
  and test setup/teardown.

## Quick Start

The expected Zephyr workspace is `/home/ubuntu/zephyrproject`. Scripts use the
workspace virtual environment at `/home/ubuntu/zephyrproject/.venv` when it is
present.

Build the baseline firmware:

```sh
scripts/build-firmware.sh
```

Run host-side tests:

```sh
scripts/test-host.sh
```

Run the current hardware smoke-test entry point:

```sh
scripts/smoke-hardware.sh
```

In Phase 0, the smoke-test script exits non-zero with a message explaining that
relay hardware smoke tests begin in Phase 1.

## Repository Layout

```text
firmware/   Zephyr application sources, config, board files, and tests
host/       Python package and host-side tests
scripts/    Build, test, flash, and smoke-test entry points
tools/      CLI entry points and operational helpers
docs/       Requirements, hardware notes, phase plans, protocol, and tests
```

## Documentation

- [Product requirements](docs/prd.md)
- [Hardware information](docs/hardware-info.md)
- [Implementation plan](docs/implementation-plan.md)
- [Phase 0 plan](docs/phase-0-plan.md)

## Safety Notes

- Do not repurpose GPIO26, GPIO27, GPIO28, GPIO29, GPIO30, or GPIO31; they are
  relay outputs on the target hardware.
- Keep relay outputs off by default and force them off during test teardown.
- Treat MCU `GND` and isolated relay/RS485 `SGND` as separate domains in design
  notes, firmware assumptions, and tests.
