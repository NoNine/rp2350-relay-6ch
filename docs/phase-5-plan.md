# Phase 5 Plan: Python RPC Library

## Summary

Phase 5 turns the Phase 4 USB RPC smoke-test protocol into an importable Python
library. The library owns SMP packet framing, serial CDC transport handling,
CBOR payload encoding and decoding, typed exceptions, retries, timeouts, and
high-level relay command wrappers.

Firmware behavior, command IDs, payload fields, and relay safety semantics stay
unchanged from the Phase 3 and Phase 4 protocol surface.

## Implementation

- Add reusable host package modules under `host/rp2350_relay_6ch/`.
- Use the `smp` package for Zephyr SMP headers and serial packet framing.
- Use `cbor2`, through `smp`'s dependency set, for CBOR payload encoding and
  decoding.
- Keep the host API synchronous for Phase 5. `smpclient` was reviewed, but its
  serial transport is asyncio-oriented and its packaged request classes do not
  include this project's custom relay group, so Phase 5 uses the lower-level
  `smp` package directly.
- Provide a packet-level serial transport for USB CDC ACM and a simulated
  transport for unit tests.
- Provide `RelayClient` methods:
  - `get_info()`
  - `get_relays()`
  - `set_relay()`
  - `set_all_relays()`
  - `pulse_relay()`
  - `off_all()`
  - `get_status()`
  - `reboot()`
- Add typed exceptions for transport, timeout, protocol, validation, and device
  errors.
- Keep the Phase 4 manual smoke-test helper available until Phase 6 replaces
  command-line behavior with the library-backed CLI.
- Document the library API in `docs/host-library.md`.

## Acceptance Checks

Run:

```sh
scripts/test-host.sh
```

Expected results:

- Host tests cover CBOR encode/decode, SMP packet round trips, and serial frame
  encoding through package-backed helpers.
- Host tests cover relay wrappers and request payloads.
- Host tests cover response matching, timeouts, retries, validation errors, and
  structured device errors.
- The package remains importable through `rp2350_relay_6ch`.

Hardware validation:

- Use `RelayClient.connect("<port>")` from the operator PC to run the same relay
  actions exercised by the Phase 4 smoke test.
- End hardware validation with `off_all()`.

## Dependencies

- Phase 4 complete and verified.
- Python 3.12 or newer.
- `pyserial` available for hardware serial transport.
- `smp` and its compatible `cbor2` dependency available for SMP and CBOR
  protocol handling.

## Deliverables

- Host Python RPC library under `host/rp2350_relay_6ch/`.
- Unit tests under `host/tests/`.
- Host library documentation in `docs/host-library.md`.
