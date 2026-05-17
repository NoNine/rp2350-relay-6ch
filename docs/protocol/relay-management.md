# Relay Management Protocol

## Status

This document starts the relay management protocol notes required by Phase 2.
The custom SMP management group, command IDs, CBOR fields, response schemas,
and stable error code table are Phase 3 scope.

## Relay Pulse Bounds

Pulse commands must use a duration from `10` ms through `60000` ms inclusive.
Firmware rejects pulse durations outside that range with an invalid-argument
error.

Pulse behavior:

- Relay channel indexes are zero-based in firmware internals.
- A valid pulse turns the selected relay on immediately.
- The relay turns off when the requested duration expires.
- Relay state remains queryable during and after a pulse.
- A second pulse request for a relay that is already pulsing is rejected as
  busy.
- `off all` behavior cancels any active pulses and leaves all relays off.
