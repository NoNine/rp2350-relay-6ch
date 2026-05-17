# USB RPC Smoke Test

This procedure validates Phase 4 USB CDC ACM SMP transport on the
RP2350-Relay-6CH firmware. Keep relay loads disconnected during first bring-up
and never leave a relay energized at teardown.

## Setup

- Build and flash the firmware with `scripts/build-firmware.sh` and
  `west flash -d build/firmware`.
- Connect the board USB-C device connector directly to the operator PC.
- Keep the UART0 shell/debug adapter separate from USB CDC RPC.
- Identify the CDC ACM serial device, for example `COM7` on Windows or
  `/dev/ttyACM0` on Linux.
- Disable tools such as ModemManager if they interfere with the CDC serial
  port.
- Install `pyserial` for the Python interpreter used on the operator PC if it
  is not already available:

  ```sh
  python -m pip install pyserial
  ```
- Install the Apache `mcumgr` CLI if it is not already available and Go is
  installed:

  ```sh
  go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest
  ```

  Ensure the Go binary directory is on `PATH`, then confirm the tool starts:

  ```sh
  mcumgr -h
  ```

The Phase 4 smoke client is `tools/usb_rpc_smoke.py`. It sends Zephyr
SMP-over-serial frames directly to custom management group `64` and prints CBOR
responses as JSON. It is a manual validation helper, not the Phase 5 host
library.

Use the operator PC's Python command in the examples below. On Windows this is
usually `python` or `py -3`; on Linux this is usually `python3`.

Set the serial port once before running the commands below:

```sh
# Windows PowerShell
$env:PORT = "COM7"

# Linux shell
export PORT=/dev/ttyACM0
```

When running from PowerShell, replace `$PORT` in the commands below with
`$env:PORT`.

## MCUmgr CLI Operation

Use `mcumgr` to configure and debug the serial SMP transport. The current Phase
4 firmware does not enable Zephyr's standard OS, image, shell, file-system, or
stat management groups, and the Apache `mcumgr` CLI does not provide a generic
raw custom-group command for this relay protocol. Relay group `64` operations
therefore use `tools/usb_rpc_smoke.py` in the next section.

Create a saved `mcumgr` serial connection profile.

Windows PowerShell:

```powershell
mcumgr conn add rp2350relay type="serial" connstring="dev=$env:PORT,baud=115200,mtu=512"
mcumgr conn show rp2350relay
```

Linux shell:

```sh
mcumgr conn add rp2350relay type="serial" connstring="dev=${PORT},baud=115200,mtu=512"
mcumgr conn show rp2350relay
```

Run `mcumgr` with an inline serial connection instead of a saved profile.

Windows PowerShell:

```powershell
mcumgr --conntype serial --connstring "dev=$env:PORT,baud=115200,mtu=512" -h
```

Linux shell:

```sh
mcumgr --conntype serial --connstring "dev=${PORT},baud=115200,mtu=512" -h
```

Use verbose logging and longer timeouts when debugging serial transport issues:

```sh
mcumgr -c rp2350relay -l debug -t 10 -r 3 -h
```

Do not use these standard `mcumgr` commands as Phase 4 relay pass/fail checks
unless the corresponding firmware groups are enabled in a later phase:

```sh
mcumgr -c rp2350relay echo hello
mcumgr -c rp2350relay image list
mcumgr -c rp2350relay reset
mcumgr -c rp2350relay shell exec "relay get"
```

For the current Phase 4 firmware, those commands are expected to fail or report
unsupported/unknown management groups because the firmware exposes only the
custom relay management group and MCUmgr enumeration support.

## Checks

1. Confirm all relays are off immediately after boot.
2. Send an SMP `info` request to group `64`, command `0`:

   ```sh
   python tools/usb_rpc_smoke.py --port "$PORT" info
   ```

   PowerShell:

   ```powershell
   python tools/usb_rpc_smoke.py --port $env:PORT info
   ```

   Confirm the response reports protocol version `1`, relay count `6`, and
   hardware `Waveshare RP2350-Relay-6CH`.
3. Send a `status` request to group `64`, command `6`:

   ```sh
   python tools/usb_rpc_smoke.py --port "$PORT" status
   ```

   PowerShell:

   ```powershell
   python tools/usb_rpc_smoke.py --port $env:PORT status
   ```

   Confirm:
   - `transport` is `usb_cdc_acm_smp`
   - `usb_cdc_acm` is true
   - `smp_uart` is true
   - relay `state` is `0`
4. Send `get`, `set`, `set_all`, `pulse`, and `off_all` requests over USB CDC
   and confirm responses match `docs/protocol/relay-management.md`. Relay
   channels are zero-based in USB RPC commands, so channel `0` is `CH1` and
   channel `5` is `CH6`.

   ```sh
   python tools/usb_rpc_smoke.py --port "$PORT" get
   python tools/usb_rpc_smoke.py --port "$PORT" set 0 on
   python tools/usb_rpc_smoke.py --port "$PORT" get --channel 0
   python tools/usb_rpc_smoke.py --port "$PORT" set 0 off
   python tools/usb_rpc_smoke.py --port "$PORT" set-all 0x21
   python tools/usb_rpc_smoke.py --port "$PORT" off-all
   python tools/usb_rpc_smoke.py --port "$PORT" pulse 0 100
   python tools/usb_rpc_smoke.py --port "$PORT" get --channel 0
   ```

   PowerShell uses the same subcommands with `--port $env:PORT`.

5. Send invalid channel and invalid pulse-duration requests. Confirm responses
   use the documented group error payload and the firmware remains responsive.

   ```sh
   python tools/usb_rpc_smoke.py --port "$PORT" invalid-channel
   python tools/usb_rpc_smoke.py --port "$PORT" invalid-pulse
   python tools/usb_rpc_smoke.py --port "$PORT" status
   ```

   PowerShell uses the same subcommands with `--port $env:PORT`.

6. Optionally run the combined smoke sequence:

   ```sh
   python tools/usb_rpc_smoke.py --port "$PORT" smoke
   ```

7. Run `off_all` before disconnecting power or relay loads:

   ```sh
   python tools/usb_rpc_smoke.py --port "$PORT" off-all
   ```
