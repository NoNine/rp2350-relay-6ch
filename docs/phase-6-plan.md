# Phase 6 Plan: CLI Utility

## Summary

Phase 6 adds a command-line utility on top of the Phase 5 Python RPC library for
manual debug, scripted checks, and manufacturing-friendly relay workflows. The
CLI keeps firmware protocol behavior unchanged and uses the host library for all
relay communication.

## Implementation

- Add `tools/rp2350_relay_cli.py` as the library-backed CLI entry point.
- Support commands:
  - `info`
  - `get`
  - `set`
  - `set-all`
  - `pulse`
  - `off-all`
  - `status`
  - `reboot`
  - `smoke`
- Use one-based relay channel numbers at the CLI boundary: `CH1` through `CH6`.
- Keep protocol-facing channel numbers zero-based inside the host library.
- Add shared serial and execution options:
  - `--port`
  - `--baud`
  - `--timeout`
  - `--retries`
  - `--output human|json`
- Return stable non-zero exit codes for argument, transport, timeout, protocol,
  and device failures.
- Add a hardware smoke command that pulses each relay and attempts `off-all`
  teardown before exiting.
- Document CLI usage in `docs/cli.md`.

## Acceptance Checks

Automated host checks:

```sh
scripts/test-host.sh
```

Expected results:

- CLI tests cover argument validation.
- CLI tests cover human-readable and JSON output modes.
- CLI tests cover success paths with a fake client.
- CLI tests cover failure exit codes for timeout, transport, protocol, and
  device failures.
- CLI smoke-test coverage confirms all six relays are pulsed and final
  `off_all()` is attempted.

Manual hardware smoke check:

```sh
${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/python \
  tools/rp2350_relay_cli.py --port <serial-port> smoke
```

Expected hardware results:

- The CLI can query `info` and `status`.
- The CLI pulses `CH1` through `CH6`.
- The CLI attempts final `off-all` teardown and leaves all relay states off.

## Dependencies

- Phase 5 complete and verified.
- Python 3.12 or newer.
- `pyserial`, `smp`, and compatible `cbor2` dependencies available through the
  Phase 5 host library environment.

## Deliverables

- CLI entry point under `tools/`.
- CLI tests under `host/tests/`.
- CLI documentation in `docs/cli.md`.
