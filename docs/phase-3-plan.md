# Phase 3 Plan: Custom SMP Relay Management Group

## Summary

Phase 3 exposes the relay control surface through a Zephyr MCUmgr/SMP
application management group. It does not add USB CDC transport, host-side SMP
framing, or custom packet handling; those remain later phases.

The custom code in this phase is limited to the relay group command handlers,
relay-specific CBOR schema, stable relay error codes, and counters. Packet
framing, group registration, CBOR encoding/decoding primitives, SMP error
responses, and reboot support use Zephyr components.

## Implementation

- Register an application-specific MCUmgr group at group ID `64`.
- Define protocol version `1`, command IDs, request fields, response fields,
  and stable relay error codes.
- Implement handlers for:
  - device info
  - get relay state
  - set relay
  - set all relays
  - pulse relay
  - off all relays
  - get status
  - reboot
- Use the existing relay module for state changes and validation.
- Use Zephyr zcbor helpers for CBOR maps and Zephyr SMP error response helpers
  for structured group errors.
- Track counters for received commands, successful commands, decode errors,
  invalid arguments, and busy responses.
- Keep USB CDC transport out of scope for Phase 3.

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

- Firmware builds for the configured RP2350 development target.
- Existing relay unit tests still pass.
- Relay management group tests cover valid command handlers, malformed CBOR,
  missing fields, invalid channels, invalid durations, busy pulses, and group
  registration.
- Protocol documentation records group ID, command IDs, CBOR request/response
  fields, and error code table.
- No automated or manual test leaves a relay on as a side effect.

## Dependencies

- Phase 2 complete and verified.
- Relay GPIO mapping remains `CH1` through `CH6` on GPIO26 through GPIO31.
- USB CDC SMP transport is not required until Phase 4.

## Deliverables

- Relay MCUmgr group implementation in firmware.
- Protocol handler tests under `firmware/tests/`.
- Updated `docs/protocol/relay-management.md`.
- Phase 3 verification report in `docs/testing/phase-3-verification.md`.
