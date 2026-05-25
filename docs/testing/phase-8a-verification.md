# Phase 8a Verification

Date: 2026-05-25
Hardware: RP2350 relay controller reported by operator
Firmware commit: e7fec01
Interfaces: USB CDC ACM SMP on explicit operator-selected serial port
Result: PASS

## Commands Run

- `scripts/test-host.sh`
- `PYTHONPATH=host python -m rp2350_relay_6ch.cli session --help`
- `ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true`

Operator-reported manual hardware session commands:

- `rp2350-relay --help`
- `rp2350-relay session --help`
- Serial-port discovery command for the operator platform
- `rp2350-relay session --port <serial-port>`
- `status`
- `pulse 1 100`
- `status`
- `off-all`
- `status`
- `exit`

## Results

- Host test gate passed: `84 passed in 0.67s`.
- Session CLI help rendered successfully in the Zephyr workspace virtual
  environment.
- No `/dev/ttyACM*` or `/dev/ttyUSB*` device was attached to this Linux
  machine during local checks.
- Manual hardware preflight was reported PASS by the operator.
- Session startup with an explicit serial port was reported PASS by the
  operator.
- Initial session `status` was reported PASS with relays off.
- `pulse 1 100` was reported PASS: board channel `CH1` pulsed briefly and all
  relays were off afterward.
- Final `off-all`, `status`, and normal `exit` were reported PASS by the
  operator.

## Coverage

- Verified by automated tests: session parser entry, discovery handling,
  command dispatch, safe close behavior, reconnect behavior, heartbeat polling,
  completion behavior, and compatibility with one-shot direct CLI commands.
- Verified by operator hardware smoke: explicit-port session startup, status,
  one-based `CH1` pulse, all-off teardown, and normal safe exit.
- Not run locally: hardware session smoke on this Linux machine because no USB
  serial relay device was attached.
- Not Phase 8a scope: daemon mode, communication-loss safety actions,
  firmware upload, test-image, confirmation, and rollback workflows.

## Hardware

- Hardware smoke testing was performed by the human operator on an
  operator-selected serial port.
- Hazardous relay-side loads were not reported connected during hardware
  testing.

## Notes

- Hardware results were reported by the human operator.
- The report records only commands that were run locally or reported as run
  during Phase 8a verification.
