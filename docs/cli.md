# CLI Utility

`tools/rp2350_relay_cli.py` provides manual and scripted relay control through
the Phase 5 Python RPC library.

## Common Options

```sh
tools/rp2350_relay_cli.py --port <serial-port> [options] <command>
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
tools/rp2350_relay_cli.py --port COM7 info
tools/rp2350_relay_cli.py --port COM7 get
tools/rp2350_relay_cli.py --port COM7 get 1
tools/rp2350_relay_cli.py --port COM7 set 1 on
tools/rp2350_relay_cli.py --port COM7 set 1 off
tools/rp2350_relay_cli.py --port COM7 set-all 0x21
tools/rp2350_relay_cli.py --port COM7 pulse 1 100
tools/rp2350_relay_cli.py --port COM7 off-all
tools/rp2350_relay_cli.py --port COM7 status
tools/rp2350_relay_cli.py --port COM7 reboot
tools/rp2350_relay_cli.py --port COM7 smoke --pulse-ms 100
```

Use JSON output for scripts:

```sh
tools/rp2350_relay_cli.py --port COM7 --output json status
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
