# Phase 0 Plan: Project Baseline And Build Harness

## Summary

Phase 0 establishes the minimal Zephyr firmware and Python host-tooling shell
needed for repeatable implementation. The development board target is
`rpi_pico2/rp2350a/m33/w` for Raspberry Pi Pico 2 W until a custom
RP2350-Relay-6CH board definition is available.

Assumption: Pico 2 W is the temporary development board until the custom board
definition or overlay lands.

No relay GPIO behavior is implemented in this phase. Relay safety, GPIO
configuration, and hardware smoke testing begin in Phase 1.

## Implementation

- Add a minimal Zephyr application in `firmware/` with `CMakeLists.txt`,
  `prj.conf`, and `src/main.c`.
- Add Python packaging metadata in `pyproject.toml` with package discovery
  rooted at `host/`.
- Add an importable `rp2350_relay_6ch` host package and a placeholder pytest
  test that verifies discovery.
- Add wrapper scripts:
  - `scripts/build-firmware.sh` builds the firmware for
    `rpi_pico2/rp2350a/m33/w` by default.
  - `scripts/test-host.sh` runs host-side pytest.
  - `scripts/smoke-hardware.sh` exits with an actionable Phase 1 placeholder
    message.

## Environment

The expected Zephyr workspace is `/home/ubuntu/zephyrproject`. Scripts use the
workspace virtual environment at `/home/ubuntu/zephyrproject/.venv` when it is
present.

The firmware build script sets `ZEPHYR_BASE` to
`/home/ubuntu/zephyrproject/zephyr` when `ZEPHYR_BASE` is unset and that path
exists.

Useful overrides:

- `BOARD=<zephyr-board-target>` changes the firmware board target.
- `BUILD_DIR=<path>` changes the firmware build directory.
- `ZEPHYR_WORKSPACE=<path>` changes the workspace root.
- `ZEPHYR_VENV=<path>` changes the Python virtual environment.
- `PYTHON=<python-executable>` changes the Python used by host tests.

## Acceptance Checks

Run:

```sh
scripts/build-firmware.sh
scripts/test-host.sh
scripts/smoke-hardware.sh
```

Expected results:

- `scripts/build-firmware.sh` configures and builds the baseline Zephyr app for
  `rpi_pico2/rp2350a/m33/w`, or fails with a clear missing-prerequisite
  message.
- `scripts/test-host.sh` starts pytest and passes the placeholder host test.
- `scripts/smoke-hardware.sh` exits non-zero with a message explaining that
  hardware smoke testing starts in Phase 1.

## Phase 1 Handoff

Phase 1 should replace the temporary Pico 2 W development target with a custom
RP2350-Relay-6CH board definition or overlay when available, then add safe
active-high relay GPIO control for GPIO26 through GPIO31.
