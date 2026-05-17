# RP2350 Relay 6CH

Zephyr firmware and Python host tooling for the Waveshare RP2350-Relay-6CH
controller.

This repository is currently implementing Phase 6: CLI utility for the custom
Zephyr MCUmgr/SMP relay management group. The firmware builds as a Zephyr
application with safe direct relay control, pulse handling, protocol handlers,
and USB CDC SMP transport. Host tooling includes an importable Python RPC
library and a library-backed CLI utility. Firmware update support is planned
but not implemented yet.

## Current Status

- Development board target: `rpi_pico2/rp2350a/m33/w` for Raspberry Pi Pico 2 W.
- Final hardware target: Waveshare RP2350-Relay-6CH.
- Relay outputs: `CH1` through `CH6` on GPIO26 through GPIO31.
- Relay polarity: active high unless board testing proves otherwise.
- Safety requirement: all relays default off on boot, reset, firmware restart,
  and test setup/teardown.

## Quick Start

Use an initialized Zephyr workspace on your machine. The wrapper scripts default
to `$HOME/zephyrproject` and use `<zephyr-workspace>/.venv` when it is present.
Set `ZEPHYR_WORKSPACE` or `ZEPHYR_VENV` if your paths differ.

Build the baseline firmware:

```sh
scripts/build-firmware.sh
```

Flash the latest firmware build:

```sh
west flash -d build/firmware
```

Run host-side tests:

```sh
scripts/test-host.sh
```

Run firmware relay tests:

```sh
west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay
build/firmware-tests/relay/zephyr/zephyr.exe
```

Run the current hardware smoke-test entry point:

```sh
scripts/smoke-hardware.sh
```

Run the Phase 6 CLI hardware smoke command:

```sh
tools/rp2350_relay_cli.py --port <serial-port> smoke
```

## Adopting This Project in Your Environment

This project targets the Waveshare RP2350-Relay-6CH. The firmware controls six
active-high relay outputs on GPIO26 through GPIO31 and keeps all relays off on
boot, reset, firmware restart, and test setup/teardown. Host tooling includes
an importable Python RPC library, CLI utility, and test harness.

Prerequisites:

- Zephyr workspace with the Zephyr SDK/toolchain available.
- Python 3.12 or newer in the Zephyr workspace virtual environment.
- Waveshare RP2350-Relay-6CH hardware, USB connection, and suitable power.
- A 3.3 V USB-UART adapter connected to UART0 for Zephyr shell relay commands:
  adapter RX to `TXD0` / GPIO0, adapter TX to `RXD0` / GPIO1, and adapter GND
  to MCU-side `GND`.
- Safe relay-side wiring; do not connect hazardous loads during bring-up.

Clone and enter the repository:

```sh
git clone git@github.com:NoNine/rp2350-relay-6ch.git
cd rp2350-relay-6ch
```

Use the Zephyr workspace virtual environment before running the Quick Start
commands:

```sh
source <zephyr-workspace>/.venv/bin/activate
pip install -e .
```

Configuration checklist:

- Confirm the Zephyr board name. The current default is
  `rpi_pico2/rp2350a/m33/w` until a dedicated RP2350-Relay-6CH board definition
  is added.
- Confirm relay GPIO mapping before hardware tests: `CH1` through `CH6` map to
  GPIO26 through GPIO31.
- Confirm the operator PC serial device path for the UART0 USB-UART adapter.
- Confirm power wiring and keep relay loads disconnected during first bring-up.
- Keep MCU `GND` and isolated relay/RS485 `SGND` assumptions documented.

Porting to another environment:

- Add or adapt Zephyr board overlays under `firmware/boards/` when hardware
  differs from the current development target.
- Update relay GPIO mappings only if the new hardware differs.
- Preserve default-off relay behavior on boot, reset, firmware restart, and
  test teardown.
- Run firmware tests and relay smoke tests before connecting relay loads.

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
- [Phase 1 plan](docs/phase-1-plan.md)
- [Phase 1 verification](docs/testing/phase-1-verification.md)
- [Phase 2 verification](docs/testing/phase-2-verification.md)
- [Phase 3 plan](docs/phase-3-plan.md)
- [Phase 3 verification](docs/testing/phase-3-verification.md)
- [Phase 4 plan](docs/phase-4-plan.md)
- [Phase 5 plan](docs/phase-5-plan.md)
- [Phase 6 plan](docs/phase-6-plan.md)
- [Host library](docs/host-library.md)
- [CLI utility](docs/cli.md)
- [USB RPC smoke test](docs/testing/usb-rpc-smoke-test.md)

## Safety Notes

- Do not repurpose GPIO26, GPIO27, GPIO28, GPIO29, GPIO30, or GPIO31; they are
  relay outputs on the target hardware.
- Keep relay outputs off by default and force them off during test teardown.
- Use UART0 for the manual Zephyr shell. Keep USB CDC dedicated to host control
  protocol traffic, and keep UART1 available for the isolated RS485 path.
- Treat MCU `GND` and isolated relay/RS485 `SGND` as separate domains in design
  notes, firmware assumptions, and tests.
