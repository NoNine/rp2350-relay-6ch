#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
	cat >&2 <<'EOF'
Usage: scripts/release-github.sh <version> [--publish]

Build and verify the required GitHub Release artifacts for <version>.
Use --publish to create the GitHub Release and upload the artifacts.

The version must be plain SemVer without a leading "v", for example 0.8.0.

This compatibility wrapper delegates to scripts/build.sh release.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
	usage
	exit 0
fi

if [[ -z "${1:-}" ]]; then
	usage
	exit 2
fi

exec "${ROOT_DIR}/scripts/build.sh" release "$@"
