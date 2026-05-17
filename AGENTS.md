# Repository Guidelines

## Project Structure & Module Organization

This is a Zephyr firmware and Python host tooling project for the Waveshare
RP2350-Relay-6CH.

- `docs/` holds requirements, hardware notes, protocol specs, and test
  procedures. Update it when behavior or hardware assumptions change.
- `firmware/` is the Zephyr application area. Put application C sources in
  `firmware/src/`, public/internal headers in
  `firmware/include/rp2350_relay_6ch/`, board overlays in `firmware/boards/`,
  and tests in `firmware/tests/`.
- `host/` is the Python package area. Put library code in
  `host/rp2350_relay_6ch/` and host-side tests in `host/tests/`.
- `tools/` is for CLI entry points and operational helpers.
- `scripts/` is for repeatable build, flash, signing, and smoke-test commands.

## Build, Test, and Development Commands

Build/test metadata is planned but not yet committed. Once Phase 0 lands, use:

- `west build -s firmware -b <rp2350-board> -d build/firmware`: build firmware.
- `west flash -d build/firmware`: flash the latest firmware build.
- `pytest host/tests`: run Python host library and CLI tests.
- `pytest firmware/tests` or Zephyr `twister`: run firmware unit tests,
  depending on the selected harness.

Prefer wrapper scripts in `scripts/` so CI, benches, and developers use the same
entry points.

For Python environment, use Zephyr workspace's venv.

## Coding Style & Naming Conventions

For firmware C, follow Zephyr conventions: 4-space indentation, `snake_case`
functions/variables, uppercase macros, and headers under
`firmware/include/rp2350_relay_6ch/`. Use Zephyr GPIO, USB, MCUmgr/SMP, CBOR,
logging, flash map, and DFU APIs where available.

For Python, use `snake_case` modules/functions, `PascalCase` classes, and typed
exceptions for protocol, timeout, transport, validation, and device errors.

## Testing Guidelines

Every phase should leave the repo buildable and testable. Firmware tests should
cover default-off behavior, `CH1` through `CH6` changes, invalid channels, pulse
bounds, busy pulses, and all-off teardown. Host tests should use simulated
transports before requiring hardware. Name tests by behavior, for example
`test_pulse_rejects_busy_relay`.

Every completed phase must have a verification report at
`docs/testing/phase-{phase-no}-verification.md`, following the format of
`docs/testing/phase-1-verification.md`. Include date, commit, commands run,
hardware used or `Not used`, result, test outcomes, and safety notes. Record
skipped hardware checks or blockers explicitly.

## Commit & Pull Request Guidelines

Existing commits use short imperative summaries, such as `Add RP2350 relay
hardware info` and `Plan PRD implementation phases`. Continue that style.
Keep commit message lines within 80 columns. In commit bodies, every section
title should be followed immediately by its content, with no blank line after
the title. Add one blank line between two sections. Start each item in every
section with `- `, put each action or request item on its own line, and wrap
long bullet items with continuation lines indented by two spaces.
When including prompt or conversation context in a commit body:

- Use a `Prompt:` section for the user request.
- Use a `Conversation context:` section for relevant actions and decisions.

Before running `git commit`, verify the commit message manually:

- Summary is short, imperative, and within 80 columns.
- Body lines are within 80 columns.
- Section titles such as `Prompt:` and `Conversation context:` are followed
  immediately by content, with no blank line after the title.
- There is one blank line between sections.
- Every item starts with `- `.
- Wrapped bullet continuation lines are indented by two spaces.

Pull requests should include a summary, commands run, hardware used, linked
issues or phase references, and docs updates for changed protocol, GPIO, safety,
or test behavior. Never leave a relay on as a test side effect.

## Safety & Configuration Tips

Relays are active-high on GPIO26 through GPIO31 and must default off on boot,
reset, firmware restart, and test setup/teardown. Do not assign those GPIOs to
alternate functions. Treat MCU `GND` and isolated relay/RS485 `SGND` as separate
domains in design notes and tests.
