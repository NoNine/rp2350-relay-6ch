# Repository Guidelines

## Project Structure & Module Organization

This is a Zephyr firmware and Python host tooling project for the Waveshare
RP2350-Relay-6CH.

Before adding or refining repository rules, search existing guidance first and
update the single most authoritative location. Avoid duplicating the same rule
across `AGENTS.md`, phase plans, and implementation docs unless a short
cross-reference is clearer than repeating the full text.

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

Use:

- `west build -s firmware -b <rp2350-board> -d build/firmware`: build firmware.
- `west flash -d build/firmware`: flash the latest firmware build.
- `pytest host/tests`: run Python host library and CLI tests.
- `pytest firmware/tests` or Zephyr `twister`: run firmware unit tests,
  depending on the selected harness.

Prefer wrapper scripts in `scripts/` so CI, benches, and developers use the same
entry points.

Use the Zephyr workspace virtual environment for Python and Zephyr tooling.
Default to `${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv`; set
`ZEPHYR_VENV` only when a different venv is required. For one-off Python
commands, use
`${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/python`
first, which resolves to `/home/ubuntu/zephyrproject/.venv/bin/python` in the
default workspace. Fall back to system `python3` only when the workspace
interpreter is unavailable, and call out that fallback.

## Coding Style & Naming Conventions

For firmware C, follow Zephyr conventions: 4-space indentation, `snake_case`
functions/variables, uppercase macros, and headers under
`firmware/include/rp2350_relay_6ch/`. Use Zephyr GPIO, USB, MCUmgr/SMP, CBOR,
logging, flash map, and DFU APIs where available.

For Python, use `snake_case` modules/functions, `PascalCase` classes, and typed
exceptions for protocol, timeout, transport, validation, and device errors.

For README updates, keep `Quick Start` brief and focused on the shortest
successful hardware path, using short operation titles. Put command variations,
full CLI usage, and test matrices in the dedicated docs under `docs/` instead
of repeating them in both `Quick Start` and example sections.

Keep documentation roles distinct. `docs/prd.md` should state product
requirements, not setup steps, implementation instructions, or marketing copy.
`docs/implementation-plan.md` should hold the current cross-phase
implementation plan and rules. Dedicated docs should hold detailed target,
build, wiring, protocol, and test guidance.

Do not retrofit completed phase plans for new direction changes. Treat completed
`docs/phase-*-plan.md` files as historical records unless the user explicitly
asks to correct or revise them.

## Implementation Discipline

Make the smallest change that satisfies the requested behavior while keeping
the repository buildable and testable. Prefer existing project patterns,
components, helpers, wrapper scripts, and Zephyr or Python library APIs where
practical. Do not introduce new components, abstractions, protocols, or helper
layers unless they remove real complexity, match an established project
pattern, or are required by the requested behavior.

Before committing or handing off changes, review the diff for incidental churn.
Do not update tests, docs, fixtures, or examples solely to mirror a version bump
unless they assert released package metadata or represent current user-facing
output. Prefer deriving expected versions from the authoritative declaration.

## Scope Discipline

Treat discussion documents as idea exploration, not implementation approval.
RAS, SmartPDU, dual-core, networking, telemetry, monitoring, aliases, audit
logs, persistent state, and similar future-looking ideas must not become
firmware, protocol, host, or release scope unless the user explicitly promotes
them into the PRD, implementation plan, or a phase plan.

For v1, prefer the smallest reliable product path: safe local relay control,
deterministic host RPC, firmware update/rollback, and narrowly justified
watchdog or health checks. Put operator ergonomics such as aliases, sequences,
monitoring, and support bundles in host tooling before adding firmware
complexity. Do not add network control, persistent relay-on state, telemetry
claims, SmartPDU-like mains-power behavior, or dual-core isolation as
incidental follow-up work.

## Testing Guidelines

Every phase should leave the repo buildable and testable. Firmware tests should
cover default-off behavior, `CH1` through `CH6` changes, invalid channels, pulse
bounds, busy pulses, and all-off teardown. Host tests should use simulated
transports before requiring hardware. Name tests by behavior, for example
`test_pulse_rejects_busy_relay`.

When hardware is attached to a separate Windows operator PC, run the hardware
portion of each phase's manual checks from that PC and use the assigned Windows
serial port, for example `COM7`.

Every completed phase must have a verification report at
`docs/testing/phase-{phase-no}-verification.md`, following the format of
`docs/testing/phase-1-verification.md`. Include date, commit, commands run,
hardware used or `Not used`, result, test outcomes, and safety notes. Record
skipped hardware checks or blockers explicitly. Do not create or update a phase
verification report automatically during implementation. Write or update the
report only when explicitly requested. When writing one, include only
verification commands that actually ran, and never record speculative results.

## Commit & Pull Request Guidelines

Existing commits use short imperative summaries, such as `Add RP2350 relay
hardware info` and `Plan PRD implementation phases`. Continue that style.
For release commits that only prepare a versioned release, use
`Release <version>`, for example `Release 0.8.8`.
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

When staged changes are under review, do not stage follow-up edits unless the
user explicitly asks. Leave cleanup edits unstaged so staged and working-tree
diffs remain separately reviewable.

Every GitHub Release must include at least these artifacts:

- `rp2350_relay_6ch-<version>-py3-none-any.whl`: host CLI wheel.
- `rp2350_relay_6ch-<version>-waveshare.uf2`: Waveshare firmware.
- `rp2350_relay_6ch-<version>-pico2.uf2`: Raspberry Pi Pico 2 firmware.

Additional artifacts such as an sdist or Pico 2 W firmware may be attached when
useful. Firmware UF2 artifact names should use short release qualifiers such as
`waveshare`, `pico2`, or `pico2w`, not full Zephyr board names.

## Safety & Configuration Tips

Relays are active-high on GPIO26 through GPIO31 and must default off on boot,
reset, firmware restart, and test setup/teardown. Do not assign those GPIOs to
alternate functions. Treat MCU `GND` and isolated relay/RS485 `SGND` as separate
domains in design notes and tests.
