#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-${HOME}/zephyrproject}"
VENV_DIR="${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE}/.venv}"
TARGET="${TARGET:-waveshare}"
EXTRA_CONF_ARGS=()

require_relay_overlay() {
	local relay_overlay_path="${RELAY_OVERLAY:-}"

	if [[ -z "${RELAY_OVERLAY:-}" ]]; then
		echo "TARGET=${TARGET} requires RELAY_OVERLAY=<path-to-relay-overlay>." >&2
		echo "The overlay must define /relays children ch1 through ch6." >&2
		exit 2
	fi

	if [[ "${relay_overlay_path}" != /* ]]; then
		relay_overlay_path="${PWD}/${relay_overlay_path}"
		if [[ ! -f "${relay_overlay_path}" ]]; then
			relay_overlay_path="${ROOT_DIR}/${RELAY_OVERLAY}"
		fi
	fi

	if [[ ! -f "${relay_overlay_path}" ]]; then
		echo "RELAY_OVERLAY '${RELAY_OVERLAY}' does not exist." >&2
		exit 2
	fi

	EXTRA_CONF_ARGS=(
		"-DEXTRA_DTC_OVERLAY_FILE=${relay_overlay_path}"
	)
}

case "${TARGET}" in
	waveshare)
		BOARD="${BOARD:-waveshare_rp2350_relay_6ch/rp2350b/m33}"
		BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/firmware}"
		;;
	pico2)
		BOARD="${BOARD:-rpi_pico2/rp2350a/m33}"
		BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/firmware-pico2}"
		require_relay_overlay
		;;
	pico2w)
		BOARD="${BOARD:-rpi_pico2/rp2350a/m33/w}"
		BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/firmware-pico2w}"
		require_relay_overlay
		;;
	*)
		echo "Unknown TARGET '${TARGET}'. Use 'waveshare', 'pico2', or 'pico2w'." >&2
		exit 2
		;;
esac

if [[ -n "${EXTRA_CONF_FILE:-}" ]]; then
	EXTRA_CONF_ARGS+=(
		"-DEXTRA_CONF_FILE=${EXTRA_CONF_FILE}"
	)
fi

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
