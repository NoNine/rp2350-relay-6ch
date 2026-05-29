# Development Setup

This guide takes a new contributor from an empty machine to a working
development checkout for firmware, host library, CLI, and tests.

The commands below assume an Ubuntu shell with Python 3.12 or newer and the
default Zephyr workspace at `$HOME/zephyrproject`. For macOS or Windows package
installation details, follow the official Zephyr Getting Started Guide, then
return here for the app-specific steps:

<https://docs.zephyrproject.org/latest/develop/getting_started/index.html>

## 1. Install System Dependencies

Install the host tools required by Zephyr:

```sh
sudo apt update
sudo apt install --no-install-recommends git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3-dev python3-venv python3-tk \
  xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1
```

On ARM64 Linux hosts, omit `gcc-multilib` and `g++-multilib` if the packages are
not available.

## 2. Create the Zephyr Workspace

Create and activate the workspace virtual environment:

```sh
python3 --version
mkdir -p "$HOME/zephyrproject"
python3 -m venv "$HOME/zephyrproject/.venv"
source "$HOME/zephyrproject/.venv/bin/activate"
python -m pip install --upgrade pip
python -m pip install west
```

Initialize Zephyr and install its Python dependencies:

```sh
west init "$HOME/zephyrproject"
cd "$HOME/zephyrproject"
west update
west zephyr-export
west packages pip --install
```

Install the Zephyr SDK:

```sh
cd "$HOME/zephyrproject/zephyr"
west sdk install
```

Reactivate this virtual environment whenever starting a new shell:

```sh
source "$HOME/zephyrproject/.venv/bin/activate"
```

## 3. Clone This App

Keep this application inside the Zephyr workspace so wrapper scripts can find
the default virtual environment and Zephyr checkout:

```sh
mkdir -p "$HOME/zephyrproject/apps"
cd "$HOME/zephyrproject/apps"
```

Run one of the clone commands below.

Clone with SSH:

```sh
git clone git@github.com:NoNine/rp2350-relay-6ch.git
cd rp2350-relay-6ch
```

Or clone with HTTPS:

```sh
git clone https://github.com/NoNine/rp2350-relay-6ch.git
cd rp2350-relay-6ch
```

If the workspace lives somewhere else, export `ZEPHYR_WORKSPACE` before running
the repo scripts:

```sh
export ZEPHYR_WORKSPACE=/path/to/zephyrproject
```

Set `ZEPHYR_VENV` only when the virtual environment is not at
`$ZEPHYR_WORKSPACE/.venv`.

## 4. Install App Python Dependencies

Install the host package and test runner into the active Zephyr virtual
environment:

```sh
python -m pip install -e . pytest
```

This provides the importable `rp2350_relay_6ch` package, its serial/SMP
dependencies, and host-side test tooling.

## 5. Verify the Checkout

Run the host tests first because they do not require hardware:

```sh
scripts/test-host.sh
```

Build the product outputs for the current development target:

```sh
scripts/build.sh
```

The product build defaults to:

```text
LUNCH=rp2350_relay_6ch-standard-userdebug
```

This produces the host wheel and the Waveshare/Pico 2 firmware images. Use the
lower-level firmware helper only for custom board or overlay experiments:

```sh
BOARD=<zephyr-board> BUILD_DIR=build/<name> scripts/build-firmware.sh
```

Default firmware rotates the optional OLED 180 degrees. Disable that for custom
hardware with:

```sh
scripts/build-firmware.sh --pristine -- \
  -DCONFIG_RP2350_RELAY_6CH_DISPLAY_ROTATED_180=n
```

Use `BOARD=waveshare_rp2350_relay_6ch/rp2350b/m33/w` only when explicitly
building for the optional RM2 Wi-Fi assembly. For Raspberry Pi Pico 2 DIY relay
hardware, see [Pico 2 DIY targets](pico-diy-targets.md).

Run firmware unit tests on `native_sim`:

```sh
west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay
build/firmware-tests/relay/zephyr/zephyr.exe

west build -s firmware/tests/relay_mgmt -b native_sim -d build/firmware-tests/relay-mgmt
build/firmware-tests/relay-mgmt/zephyr/zephyr.exe
```

## 6. Flash and Check Hardware

Flash the most recent firmware build:

```sh
west flash -d build/product/rp2350_relay_6ch-standard-userdebug/waveshare
```

Then run a CLI smoke test from the machine connected to the board:

```sh
rp2350-relay --port <serial-port> smoke
```

On Linux, the serial port is typically similar to `/dev/ttyACM0`. On Windows,
use the assigned `COM` port, for example `COM7`.

Keep hazardous relay-side loads disconnected during bring-up. Confirm all
relays are off after boot, reset, smoke tests, and teardown.

## Troubleshooting

- `west not found`: activate `$HOME/zephyrproject/.venv` or set `ZEPHYR_VENV`
  to the virtual environment that contains `west`.
- `pytest not found`: run `python -m pip install -e . pytest` in the active
  Zephyr virtual environment.
- `west build` cannot find Zephyr: run `west zephyr-export`, set
  `ZEPHYR_WORKSPACE`, or export `ZEPHYR_BASE=/path/to/zephyrproject/zephyr`.
- Linux serial permission errors: add the user to `dialout`, log out and back
  in, and see [CLI Linux serial permissions](cli.md#linux-serial-permissions).
- Wrong serial port: check the device list before and after plugging in the
  board, then pass the new port explicitly with `--port`.
