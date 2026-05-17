# Phase 4 Plan: USB CDC SMP Transport

## Summary

Phase 4 exposes the existing relay MCUmgr/SMP management group over the board
USB-C device connector using Zephyr's CDC ACM serial device and SMP UART
transport. Relay behavior, command IDs, CBOR payloads, and error codes remain
the Phase 3 protocol surface.

UART0 remains the manual Zephyr shell/debug console. USB CDC ACM is reserved for
host relay RPC and must not share bytes with the shell.

## Implementation

- Enable Zephyr USB device support with a CDC ACM serial function.
- Enable Zephyr's SMP UART transport and bind `zephyr,uart-mcumgr` to the CDC
  ACM UART device in the application overlay.
- Keep relay initialization in `main()` before the application reports ready,
  and avoid any transport startup path that can energize relay GPIOs.
- Keep the custom relay management group unchanged except for host-visible
  transport status fields.
- Extend the `status` command response with:
  - transport name
  - whether USB CDC ACM support is compiled in
  - whether the SMP UART transport is compiled in
- Add manual USB RPC smoke-test notes for discovering the CDC serial port and
  exercising the relay management commands.
- Keep Phase 5 host library work out of scope; Phase 4 may use external MCUmgr
  tooling or later host code for hardware validation.

## Acceptance Checks

Run:

```sh
scripts/build-firmware.sh
west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay
build/firmware-tests/relay/zephyr/zephyr.exe
west build -s firmware/tests/relay_mgmt -b native_sim -d build/firmware-tests/relay-mgmt
build/firmware-tests/relay-mgmt/zephyr/zephyr.exe
scripts/smoke-hardware.sh
```

Expected results:

- Firmware builds for the configured RP2350 development target with USB CDC ACM
  and SMP UART transport enabled.
- Existing relay and relay management tests still pass.
- Status responses include transport state fields.
- Hardware smoke testing can identify the USB CDC serial device and send relay
  SMP requests over it.
- Invalid RPC requests return structured errors without crashing firmware.
- No automated or manual test leaves a relay on as a side effect.

## Dependencies

- Phase 3 complete and verified.
- RP2350 Zephyr target provides a usable USB device controller node.
- The operator PC can access the board's CDC ACM serial device.

## Deliverables

- USB CDC ACM SMP transport enabled in firmware configuration and devicetree.
- Transport status fields in the relay management `status` response.
- USB RPC smoke-test procedure in `docs/testing/usb-rpc-smoke-test.md`.
