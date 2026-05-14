#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-/home/ubuntu/zephyrproject}"
VENV_DIR="${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE}/.venv}"

if [[ -f "${VENV_DIR}/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "${VENV_DIR}/bin/activate"
fi

PYTHON_BIN="${PYTHON:-python3}"

if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
	echo "Python not found. Activate the Zephyr workspace venv or set PYTHON to a Python executable." >&2
	exit 127
fi

if ! "${PYTHON_BIN}" -m pytest --version >/dev/null 2>&1; then
	echo "pytest not found for ${PYTHON_BIN}. Install pytest in the Zephyr workspace venv." >&2
	exit 127
fi

cd "${ROOT_DIR}"
"${PYTHON_BIN}" -m pytest host/tests "$@"
