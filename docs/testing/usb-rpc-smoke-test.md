# USB RPC Smoke Test

This procedure validates Phase 4 USB CDC ACM SMP transport on the
RP2350-Relay-6CH firmware. Keep relay loads disconnected during first bring-up
and never leave a relay energized at teardown.

## Setup

- Build and flash the firmware with `scripts/build-firmware.sh` and
  `west flash -d build/firmware`.
- Connect the board USB-C device connector directly to the operator PC.
- Keep the UART0 shell/debug adapter separate from USB CDC RPC.
- Identify the CDC ACM serial device, for example `/dev/ttyACM0` on Linux.
- Disable tools such as ModemManager if they interfere with the CDC serial
  port.

## Checks

1. Confirm all relays are off immediately after boot.
2. Send an SMP `info` request to group `64`, command `0`, over the CDC serial
   device. Confirm the response reports protocol version `1`, relay count `6`,
   and hardware `Waveshare RP2350-Relay-6CH`.
3. Send a `status` request to group `64`, command `6`. Confirm:
   - `transport` is `usb_cdc_acm_smp`
   - `usb_cdc_acm` is true
   - `smp_uart` is true
   - relay `state` is `0`
4. Send `get`, `set`, `set_all`, `pulse`, and `off_all` requests over USB CDC
   and confirm responses match `docs/protocol/relay-management.md`.
5. Send invalid channel and invalid pulse-duration requests. Confirm responses
   use the documented group error payload and the firmware remains responsive.
6. Run `off_all` before disconnecting power or relay loads.

Phase 5 adds the project Python host library. Until then, use an MCUmgr client
or a temporary host script that can send SMP serial frames and CBOR payloads
matching the documented protocol.
