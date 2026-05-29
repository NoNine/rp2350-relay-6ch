#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-${HOME}/zephyrproject}"
VENV_DIR="${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE}/.venv}"

if [[ -f "${VENV_DIR}/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "${VENV_DIR}/bin/activate"
fi

if [[ -x "${VENV_DIR}/bin/python" ]]; then
	PYTHON_BIN="${VENV_DIR}/bin/python"
elif [[ -n "${PYTHON:-}" ]]; then
	PYTHON_BIN="${PYTHON}"
else
	PYTHON_BIN="python3"
fi

if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
	echo "Python not found. Activate the Zephyr workspace venv or set PYTHON to a Python executable." >&2
	exit 127
fi

if ! "${PYTHON_BIN}" -m PyInstaller --version >/dev/null 2>&1; then
	echo "PyInstaller not found for ${PYTHON_BIN}. Install executable build dependencies with:" >&2
	echo "  ${PYTHON_BIN} -m pip install -e '.[release]'" >&2
	exit 127
fi

cd "${ROOT_DIR}"
"${PYTHON_BIN}" scripts/build_host_executable.py
