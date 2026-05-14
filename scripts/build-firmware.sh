#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-/home/ubuntu/zephyrproject}"
VENV_DIR="${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE}/.venv}"
BOARD="${BOARD:-rpi_pico2/rp2350a/m33/w}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/firmware}"

if [[ -f "${VENV_DIR}/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "${VENV_DIR}/bin/activate"
fi

if ! command -v west >/dev/null 2>&1; then
	echo "west not found. Activate the Zephyr workspace venv or set ZEPHYR_VENV to a venv containing west." >&2
	exit 127
fi

if [[ -z "${ZEPHYR_BASE:-}" && -d "${ZEPHYR_WORKSPACE}/zephyr" ]]; then
	export ZEPHYR_BASE="${ZEPHYR_WORKSPACE}/zephyr"
fi

west build -s "${ROOT_DIR}/firmware" -b "${BOARD}" -d "${BUILD_DIR}" "$@"
