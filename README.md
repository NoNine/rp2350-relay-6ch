# RP2350 Relay 6CH

Zephyr firmware and Python host tooling for the Waveshare RP2350-Relay-6CH
controller.

The firmware controls six relay outputs, exposes a custom Zephyr MCUmgr/SMP
relay management group, and provides USB CDC transport for host control. The
host side includes an importable Python RPC library, smoke-test helpers, and a
library-backed CLI. Firmware update support is planned but not implemented.

## Features

Implemented:

- Safe six-channel relay control for `CH1` through `CH6`.
- Default-off relay behavior on boot, reset, firmware restart, and test
  setup/teardown.
- Relay get, set, set-all, pulse, off-all, status, info, and reboot command
  handling through a custom MCUmgr/SMP management group.
- USB CDC SMP transport for host control.
- Python RPC library with typed transport, timeout, protocol, validation, and
  device errors.
- CLI utility for manual control, JSON output, scripted checks, and hardware
  smoke tests.
- Host-side tests with simulated transports and firmware tests for relay and
  relay-management behavior.

Planned:

- MCUboot-compatible A/B firmware update and rollback support.
- Host library and CLI firmware image upload, test-image, and confirm-image
  workflows.
- Firmware signing, flashing, and release helper scripts.
- Dedicated Waveshare RP2350-Relay-6CH board definition or board-specific
  overlay once the target configuration is finalized.
- Optional status outputs for the buzzer and WS2812 RGB LED if they do not
  interfere with relay control or RPC behavior.
- Optional communication-loss safety timeout, disabled by default unless a
  deployment explicitly configures it.

## Current Status

- Development board target: `rpi_pico2/rp2350a/m33/w` for Raspberry Pi Pico 2 W.
- Final hardware target: Waveshare RP2350-Relay-6CH.
- Relay outputs: `CH1` through `CH6` on GPIO26 through GPIO31.
- Relay polarity: active high unless board testing proves otherwise.
- Host control: Python RPC library and CLI over the configured SMP serial route.
- Safety requirement: all relays default off on boot, reset, firmware restart,
  and test setup/teardown.

## Prerequisites

- Zephyr workspace with the Zephyr SDK/toolchain available.
- Python 3.12 or newer.
- Python dependencies installed in the active environment:

  ```sh
  pip install -e . pytest
  ```

- Waveshare RP2350-Relay-6CH hardware, USB connection, and suitable power for
  hardware smoke tests.
- Safe relay-side wiring. Keep hazardous loads disconnected during bring-up.

Wrapper scripts default to `${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv`.
Set `ZEPHYR_WORKSPACE` or `ZEPHYR_VENV` only when your environment differs.

## Quick Start

Build the firmware:

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

Run firmware relay management tests:

```sh
west build -s firmware/tests/relay_mgmt -b native_sim -d build/firmware-tests/relay-mgmt
build/firmware-tests/relay-mgmt/zephyr/zephyr.exe
```

Run the hardware smoke-test wrapper:

```sh
scripts/smoke-hardware.sh
```

Run the CLI hardware smoke test:

```sh
tools/rp2350_relay_cli.py --port <serial-port> smoke
```

## CLI Examples

CLI channel numbers are one-based and match board labels: `1` is `CH1` and `6`
is `CH6`.

```sh
tools/rp2350_relay_cli.py --port COM7 info
tools/rp2350_relay_cli.py --port COM7 get
tools/rp2350_relay_cli.py --port COM7 set 1 on
tools/rp2350_relay_cli.py --port COM7 pulse 1 100
tools/rp2350_relay_cli.py --port COM7 off-all
tools/rp2350_relay_cli.py --port COM7 status
tools/rp2350_relay_cli.py --port COM7 --output json status
```

See [docs/cli.md](docs/cli.md) for the full command list and exit codes.

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
- [Relay management protocol](docs/protocol/relay-management.md)
- [Host library](docs/host-library.md)
- [CLI utility](docs/cli.md)
- [Relay smoke test](docs/testing/relay-smoke-test.md)
- [USB RPC smoke test](docs/testing/usb-rpc-smoke-test.md)

Phase plans and verification reports live under `docs/phase-*-plan.md` and
`docs/testing/phase-*-verification.md`.

## Safety Notes

- Do not repurpose GPIO26, GPIO27, GPIO28, GPIO29, GPIO30, or GPIO31; they are
  relay outputs on the target hardware.
- Keep relay outputs off by default and force them off during test teardown.
- Use UART0 for the manual Zephyr shell. Keep USB CDC dedicated to host control
  protocol traffic, and keep UART1 available for the isolated RS485 path.
- Treat MCU `GND` and isolated relay/RS485 `SGND` as separate domains in design
  notes, firmware assumptions, and tests.
