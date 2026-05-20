# CLI Utility

`rp2350-relay` provides manual and scripted relay control through the Python RPC
library. Developers can also run the repository compatibility wrapper at
`tools/rp2350_relay_cli.py`.

## Operator Install

Operators need Python 3.12 or newer and the release artifacts; no Zephyr
workspace or source checkout is required. Download the matching files from the
same GitHub Release:

- `rp2350_relay_6ch-<version>-py3-none-any.whl`
- `rp2350_relay_6ch-<version>-waveshare.uf2`
- `rp2350_relay_6ch-<version>-pico2.uf2`

### Firmware

Flash the `.uf2` file when the board is not already running the matching
firmware. Keep relay loads disconnected during first flash and smoke test.

UF2 drag-and-drop:

1. Put the board in USB bootloader mode.
2. Copy the `.uf2` file to the mounted `RP2350` drive.
3. Reconnect the board normally.

`picotool`:

```sh
picotool load -x rp2350_relay_6ch-<version>-waveshare.uf2
```

Install `picotool` separately and run this command while the board is in USB
bootloader mode. The `-x` option runs the firmware after flashing; do not use
`--force-no-reboot` or `-F` for this operator flow. If Linux cannot access the
device, retry with appropriate USB permissions or `sudo`.

### CLI Wheel

Windows PowerShell:

```powershell
python -m pip install --user pipx
python -m pipx ensurepath
python -m pipx install .\rp2350_relay_6ch-<version>-py3-none-any.whl
rp2350-relay --port COM7 info
```

Linux shell:

```sh
python3 -m pip install --user pipx
python3 -m pipx ensurepath
python3 -m pipx install ./rp2350_relay_6ch-<version>-py3-none-any.whl
rp2350-relay --port /dev/ttyACM0 info
```

Open a new terminal after `pipx ensurepath` if `rp2350-relay` is not found.
Upgrade after downloading a newer wheel with:

```sh
pipx install --force ./rp2350_relay_6ch-<version>-py3-none-any.whl
```

Release maintainers build the wheel from the repository root:

```sh
python -m pip install build
python -m build
```

Attach at least the generated `dist/*.whl` file and renamed firmware images to
the GitHub Release:

- `build/firmware/zephyr/zephyr.uf2` as
  `rp2350_relay_6ch-<version>-waveshare.uf2`
- `build/firmware-pico2/zephyr/zephyr.uf2` as
  `rp2350_relay_6ch-<version>-pico2.uf2`

## Common Options

```sh
rp2350-relay --port <serial-port> [options] <command>
```

- `--port`: SMP serial port, for example `COM7` or `/dev/ttyACM0`.
- `--baud`: serial baud rate, default `115200`.
- `--timeout`: request timeout in seconds, default `2.0`.
- `--retries`: timeout retry count, default `1`.
- `--output`: `human` or `json`, default `human`.

CLI channel arguments are one-based and match the board labels: `1` is `CH1`
and `6` is `CH6`.

## Commands

```sh
rp2350-relay --port COM7 info
rp2350-relay --port COM7 build-info
rp2350-relay --port COM7 get
rp2350-relay --port COM7 get 1
rp2350-relay --port COM7 set 1 on
rp2350-relay --port COM7 set 1 off
rp2350-relay --port COM7 set-all 0x21
rp2350-relay --port COM7 pulse 1 100
rp2350-relay --port COM7 off-all
rp2350-relay --port COM7 status
rp2350-relay --port COM7 reboot
rp2350-relay --port COM7 smoke --pulse-ms 100
```

Use JSON output for scripts:

```sh
rp2350-relay --port COM7 --output json status
```

## Hardware Smoke Test

The `smoke` command queries the controller, pulses `CH1` through `CH6`, and
attempts final `off-all` teardown. Keep relay loads disconnected during
bring-up and confirm all relays are off after the command exits.

## Exit Codes

- `0`: success.
- `2`: argument or validation error.
- `3`: transport setup, read, or write failure.
- `4`: timeout.
- `5`: malformed or mismatched protocol response.
- `6`: device-side relay management error.
