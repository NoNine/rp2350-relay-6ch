#!/usr/bin/env bash
set -euo pipefail

cat >&2 <<'MSG'
Hardware smoke tests are not implemented in Phase 0.

Phase 1 will add relay GPIO bring-up and a smoke test that verifies CH1 through
CH6 switch independently and return off after reset.
MSG

exit 2
