#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-${HOME}/zephyrproject}"
VENV_DIR="${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE}/.venv}"
DEFAULT_LUNCH="rp2350_relay_6ch-standard-userdebug"
TARGET="waveshare"
EXPLICIT_LUNCH=""
DRY_RUN=false
WEST_ARGS=()

usage() {
	cat >&2 <<'EOF'
Usage:
  scripts/flash.sh [--target waveshare|pico2] [--lunch <lunch>] [--dry-run] [-- <west flash args>]

Flash a firmware image from the product build output directory. The default
target is waveshare and the default lunch is rp2350_relay_6ch-standard-userdebug.
Run scripts/build.sh first to create the product build outputs.
EOF
}

die() {
	echo "error: $*" >&2
	exit 1
}

setup_zephyr_env() {
	if [[ -f "${VENV_DIR}/bin/activate" ]]; then
		# shellcheck disable=SC1091
		source "${VENV_DIR}/bin/activate"
	fi

	if [[ -z "${ZEPHYR_BASE:-}" && -d "${ZEPHYR_WORKSPACE}/zephyr" ]]; then
		export ZEPHYR_BASE="${ZEPHYR_WORKSPACE}/zephyr"
	fi
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--target)
			if [[ -z "${2:-}" ]]; then
				die "--target requires a value"
			fi
			TARGET="$2"
			shift
			;;
		--lunch)
			if [[ -z "${2:-}" ]]; then
				die "--lunch requires a value"
			fi
			EXPLICIT_LUNCH="$2"
			shift
			;;
		--dry-run)
			DRY_RUN=true
			;;
		-h|--help)
			usage
			exit 0
			;;
		--)
			shift
			WEST_ARGS=("$@")
			break
			;;
		*)
			usage
			die "unknown argument '$1'"
			;;
	esac
	shift
done

case "${TARGET}" in
	waveshare|pico2)
		;;
	*)
		die "unknown target '${TARGET}'. Use 'waveshare' or 'pico2'."
		;;
esac

if [[ -n "${EXPLICIT_LUNCH}" && -n "${LUNCH:-}" && "${EXPLICIT_LUNCH}" != "${LUNCH}" ]]; then
	die "--lunch '${EXPLICIT_LUNCH}' conflicts with LUNCH='${LUNCH}'"
fi

LUNCH_TARGET="${EXPLICIT_LUNCH:-${LUNCH:-${DEFAULT_LUNCH}}}"
BUILD_DIR="${ROOT_DIR}/build/product/${LUNCH_TARGET}/${TARGET}"
DISPLAY_BUILD_DIR="${BUILD_DIR#${ROOT_DIR}/}"

echo "Resolved lunch: ${LUNCH_TARGET}"
echo "Target: ${TARGET}"
echo "Build dir: ${DISPLAY_BUILD_DIR}"

if [[ "${DRY_RUN}" == true ]]; then
	printf 'Dry run: west flash -d %s' "${DISPLAY_BUILD_DIR}"
	if [[ ${#WEST_ARGS[@]} -gt 0 ]]; then
		printf ' %q' "${WEST_ARGS[@]}"
	fi
	printf '\n'
	exit 0
fi

if [[ ! -d "${BUILD_DIR}" ]]; then
	die "build dir '${DISPLAY_BUILD_DIR}' does not exist; run scripts/build.sh --lunch '${LUNCH_TARGET}' first"
fi

setup_zephyr_env

if ! command -v west >/dev/null 2>&1; then
	die "west not found. Activate the Zephyr workspace venv or set ZEPHYR_VENV to a venv containing west."
fi

west flash -d "${BUILD_DIR}" "${WEST_ARGS[@]}"
