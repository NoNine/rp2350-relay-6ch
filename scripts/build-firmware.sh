#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-${HOME}/zephyrproject}"
VENV_DIR="${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE}/.venv}"
TARGET="${TARGET:-waveshare}"
EXTRA_CONF_ARGS=()

case "${TARGET}" in
	waveshare)
		BOARD="${BOARD:-waveshare_rp2350_relay_6ch/rp2350b/m33}"
		BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/firmware}"
		;;
	pico2w-dev)
		BOARD="${BOARD:-rpi_pico2/rp2350a/m33/w}"
		BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/firmware-pico2w-dev}"
		EXTRA_CONF_ARGS=(
			"-DEXTRA_DTC_OVERLAY_FILE=${ROOT_DIR}/firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay"
		)
		;;
	*)
		echo "Unknown TARGET '${TARGET}'. Use 'waveshare' or 'pico2w-dev'." >&2
		exit 2
		;;
esac

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

if [[ ${#EXTRA_CONF_ARGS[@]} -gt 0 ]]; then
	west build -s "${ROOT_DIR}/firmware" -b "${BOARD}" -d "${BUILD_DIR}" \
		"$@" -- "${EXTRA_CONF_ARGS[@]}"
else
	west build -s "${ROOT_DIR}/firmware" -b "${BOARD}" -d "${BUILD_DIR}" "$@"
fi
