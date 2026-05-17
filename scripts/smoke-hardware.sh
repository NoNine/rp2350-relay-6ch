#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RELAY_PROCEDURE="${ROOT_DIR}/docs/testing/relay-smoke-test.md"
USB_RPC_PROCEDURE="${ROOT_DIR}/docs/testing/usb-rpc-smoke-test.md"

cat <<MSG
Phase 4 relay and USB RPC hardware smoke test

Follow the procedures in:
  ${RELAY_PROCEDURE}
  ${USB_RPC_PROCEDURE}

Required checks:
  - All relays are off after boot, reset, and power-cycle.
  - CH1 through CH6 switch on and off independently.
  - CH1 through CH6 each pulse briefly and return off.
  - Phase 4 firmware builds with USB CDC ACM and SMP UART enabled.
  - The USB CDC serial device appears on the operator PC.
  - USB RPC info, status, get, set, set-all, pulse, and off-all requests work.
  - Invalid USB RPC requests return structured errors without crashing firmware.
  - 'relay off' is run during teardown so no relay remains energized.

This helper is a manual hardware checklist entry point; it does not switch
relays or send USB RPC requests itself.
MSG
