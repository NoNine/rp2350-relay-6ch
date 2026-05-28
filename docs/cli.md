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

Use the wheel for the primary operator install path. The wheel requires
Python 3.12 or newer in the environment that installs it.

`pipx` is the shortest path when Python 3.12 or newer is already available.

Windows PowerShell with `pipx`:

```powershell
python -m pip install --user pipx
python -m pipx ensurepath
python -m pipx install .\rp2350_relay_6ch-<version>-py3-none-any.whl
rp2350-relay --port <serial-port> info
```

Linux shell with `pipx`:

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

If the Linux system Python is older than 3.12 and cannot be upgraded, use conda
to provide an operator-local Python 3.12 environment:

```sh
conda create -n rp2350-relay python=3.12
conda activate rp2350-relay
python -m pip install ./rp2350_relay_6ch-<version>-py3-none-any.whl
rp2350-relay --port <serial-port> info
```

If a separate Python 3.12 or newer binary is already available, a local venv is
also supported:

```sh
/path/to/python3.12 -m venv ~/.venvs/rp2350-relay
. ~/.venvs/rp2350-relay/bin/activate
python -m pip install ./rp2350_relay_6ch-<version>-py3-none-any.whl
rp2350-relay --port <serial-port> info
```

Run a smoke test after installing the CLI and flashing the matching firmware:

```sh
rp2350-relay --port <serial-port> smoke
```

Use `COM7`-style ports on Windows and `/dev/ttyACM0`-style ports on Linux.
The default smoke pulse timing is paced for visible relay and local indicator
observation. Confirm all relays are off after the command exits.

### Optional CLI Executable

Some releases may include a platform executable as a convenience add-on. Use it
when avoiding a Python install matters. The executable bundles the Python host
CLI and its Python dependencies, but it is not the primary install path.

Windows PowerShell:

```powershell
.\rp2350_relay_6ch-<version>-windows-x64.exe --port <serial-port> info
.\rp2350_relay_6ch-<version>-windows-x64.exe --port <serial-port> smoke
```

Linux shell:

```sh
chmod +x ./rp2350_relay_6ch-<version>-linux-x64
./rp2350_relay_6ch-<version>-linux-x64 --port <serial-port> info
./rp2350_relay_6ch-<version>-linux-x64 --port <serial-port> smoke
```

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

Daemon mode uses the same Linux device-node permissions as direct CLI mode. If
a `systemd --user` daemon reports permission denied for `/dev/ttyACM*` even
after group changes, fully log out and back in so the user service manager has
the updated group membership. The daemon smoke test includes the exact
inspection and session-refresh commands in
[Daemon smoke test](testing/daemon-smoke-test.md).

## Release Artifact Notes

Release maintainers should use the fixed workflow in
[Release workflow](release.md). Every GitHub Release must include the host CLI
wheel, Waveshare UF2, and Pico 2 UF2 from the same version tag. Optional
platform executables may also be attached when useful.

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
rp2350-relay session --port <serial-port>
rp2350-relay --serial <usb-serial> session
rp2350-relay session --serial <usb-serial>
```

Without `--port` or `--serial`, the session discovers relay controllers and
asks the operator to choose from a list showing port and USB serial number.
If no matching controller is connected, the session stays open in disconnected
mode so you can plug in hardware and run `connect`. Startup `--port` or
`--serial` failures are also recoverable from the disconnected prompt.
Run `off-all` before exiting; normal session exit confirms relays are off.
Connected sessions use a bracket prompt such as `rp2350-relay[COM7]$`; the
disconnected prompt is `rp2350-relay[disconnected]$`. The REPL Plus UX contract
is [REPL Plus CLI UX contract](host-cli-ux-repl-plus.md). USB removal and
reinsert recovery semantics are defined in
[Host session mode](host-session-mode.md#usb-removal-and-recovery).

## Linux Daemon Mode

`rp2350-relayd` is a Linux-only foreground daemon that owns one relay
controller and serves local clients over an explicit Unix socket. Use it when
automation should issue short-lived commands without opening the serial port for
each operation.

Start one daemon per relay controller:

```sh
SOCKET="$XDG_RUNTIME_DIR/rp2350-relay/bench-a.sock"
rp2350-relayd --serial <usb-serial> --socket "$SOCKET" --wait-device
```

Development and bench setups may use an exact port instead of USB serial:

```sh
rp2350-relayd --port /dev/ttyACM0 --socket "$SOCKET"
```

The daemon requires exactly one device selector, `--port` or `--serial`, and a
socket path. It creates the socket parent directory with user-only permissions
and binds the socket for the current user. Stop the daemon before using
`rp2350-relay --port ...` against the same device.

For production, define named instances in
`~/.config/rp2350-relay/config.toml`:

```toml
[instances.bench-a]
serial = "E6614C311F4B8B2F"
socket = "${XDG_RUNTIME_DIR}/rp2350-relay/bench-a.sock"
wait_device = true
```

Then start and target the daemon by instance name:

```sh
rp2350-relayd --instance bench-a
rp2350-relayctl --instance bench-a daemon-status
rp2350-relayctl --instance bench-a status
```

Use `rp2350-relayctl` for daemon-client commands:

```sh
rp2350-relayctl --socket "$SOCKET" daemon-status
rp2350-relayctl --socket "$SOCKET" info
rp2350-relayctl --socket "$SOCKET" status
rp2350-relayctl --socket "$SOCKET" pulse 1 100
rp2350-relayctl --socket "$SOCKET" off-all
```

`rp2350-relayctl` accepts `--socket` or `--instance`, plus `--timeout` and
`--output human|json`. It does not accept direct serial options such as
`--port` or `--baud`.
`daemon-status` reports daemon state and exits successfully while the daemon is
running, even if the relay controller is disconnected.

Install the systemd user-service template from the same Python environment that
should run the daemon:

```sh
rp2350-relayctl systemd install
systemctl --user daemon-reload
systemctl --user start rp2350-relayd@bench-a
```

The generated unit uses an absolute Python interpreter, for example
`/home/user/.local/share/pipx/venvs/rp2350-relay-6ch/bin/python -m
rp2350_relay_6ch.daemon --instance %i`. If using conda or venv, run
`rp2350-relayctl systemd install` after activating that environment, or pass
`--python /path/to/env/bin/python`.

Useful systemd commands:

```sh
systemctl --user stop rp2350-relayd@bench-a
systemctl --user status rp2350-relayd@bench-a
journalctl --user -u rp2350-relayd@bench-a
rp2350-relayctl systemd doctor --instance bench-a
```

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
rp2350-relay --port <serial-port> smoke --pulse-ms 1000
```

Use JSON output for scripts:

```sh
rp2350-relay --port <serial-port> --output json status
rp2350-relayctl --socket "$SOCKET" --output json daemon-status
```

## Hardware Smoke Test

The `smoke` command queries the controller, pulses `CH1` through `CH6`
sequentially, waits for each pulse to complete, and attempts final `off-all`
teardown. The default `--pulse-ms 1000` is intended to make relay and optional
OLED indicator updates observable; shorter values are useful for automation but
may be too fast to see on the local display. Keep relay loads disconnected
during bring-up and confirm all relays are off after the command exits.

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

`rp2350-relayctl` uses the same exit codes for daemon-client commands.
