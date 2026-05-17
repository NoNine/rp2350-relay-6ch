#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROCEDURE="${ROOT_DIR}/docs/testing/relay-smoke-test.md"

cat <<MSG
Phase 3 relay hardware smoke test

Follow the procedure in:
  ${PROCEDURE}

Required checks:
  - All relays are off after boot, reset, and power-cycle.
  - CH1 through CH6 switch on and off independently.
  - CH1 through CH6 each pulse briefly and return off.
  - Phase 3 protocol handler tests pass before hardware validation.
  - 'relay off' is run during teardown so no relay remains energized.

This helper is a manual hardware checklist entry point; it does not switch
relays itself.
MSG
