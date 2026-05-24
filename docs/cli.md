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

Use the Waveshare UF2 for RP2350-Relay-6CH and RP2350-Relay-6CH-W boards. Use
the Pico 2 artifact only for a supported DIY target with a matching relay
overlay.

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
rp2350-relay --port <serial-port> info
```

Linux shell:

```sh
python3 -m pip install --user pipx
python3 -m pipx ensurepath
python3 -m pipx install ./rp2350_relay_6ch-<version>-py3-none-any.whl
rp2350-relay --port <serial-port> info
```

Open a new terminal after `pipx ensurepath` if `rp2350-relay` is not found.
Upgrade after downloading a newer wheel with:

```sh
pipx install --force ./rp2350_relay_6ch-<version>-py3-none-any.whl
```

Run a smoke test after installing the CLI and flashing the matching firmware:

```sh
rp2350-relay --port <serial-port> smoke
```

Use `COM7`-style ports on Windows and `/dev/ttyACM0`-style ports on Linux.
Confirm all relays are off after the command exits.

### Linux Serial Permissions

If Linux cannot open the serial port, the CLI may report:

```text
transport error: [Errno 13] could not open port /dev/ttyACM0: Permission denied
```

Do not use `sudo rp2350-relay` as the normal fix. The command is often installed
for the current user, so root may not find it and may report:

```text
sudo: rp2350-relay: command not found
```

Check the device permissions and add your user to the serial-port group:

```sh
ls -l /dev/ttyACM0
sudo usermod -aG dialout "$USER"
```

Fully log out and log back in, then verify `dialout` appears:

```sh
groups
rp2350-relay --port <serial-port> info
```

For a temporary local workaround, grant access to the current device node:

```sh
sudo chmod a+rw /dev/ttyACM0
```

The `chmod` workaround is reset when the board is unplugged, reconnected, or the
system reboots.

## Release Artifact Notes

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

## Session Mode

Session mode is a long-lived manual operator workflow for Windows and Linux:

```sh
rp2350-relay session
rp2350-relay --port <serial-port> session
rp2350-relay --serial <usb-serial> session
```

Without `--port` or `--serial`, the session discovers relay controllers and
asks the operator to choose from a list showing port and USB serial number.
Run `off-all` before exiting; normal session exit confirms relays are off.

## Commands

```sh
rp2350-relay session
rp2350-relay --port <serial-port> info
rp2350-relay --port <serial-port> build-info
rp2350-relay --port <serial-port> get
rp2350-relay --port <serial-port> get 1
rp2350-relay --port <serial-port> set 1 on
rp2350-relay --port <serial-port> set 1 off
rp2350-relay --port <serial-port> set-all 0x21
rp2350-relay --port <serial-port> pulse 1 100
rp2350-relay --port <serial-port> off-all
rp2350-relay --port <serial-port> status
rp2350-relay --port <serial-port> reboot
rp2350-relay --port <serial-port> smoke --pulse-ms 100
```

Use JSON output for scripts:

```sh
rp2350-relay --port <serial-port> --output json status
```

## Hardware Smoke Test

The `smoke` command queries the controller, pulses `CH1` through `CH6`, and
attempts final `off-all` teardown. Keep relay loads disconnected during
bring-up and confirm all relays are off after the command exits.

## Local Status Indicators

The operator manual for RGB LED and buzzer behavior is
[Status indicators](status-indicators.md). Local indicators are diagnostic aids;
use CLI command responses and `status` output as the source of truth.

## Exit Codes

- `0`: success.
- `2`: argument or validation error.
- `3`: transport setup, read, or write failure.
- `4`: timeout.
- `5`: malformed or mismatched protocol response.
- `6`: device-side relay management error.
