#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-${HOME}/zephyrproject}"
VENV_DIR="${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE}/.venv}"
PYTHON_BIN="${PYTHON:-python3}"
PICO2_RELAY_OVERLAY="firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay"

usage() {
	cat >&2 <<'EOF'
Usage: scripts/release-github.sh <version> [--publish]

Build and verify the required GitHub Release artifacts for <version>.
Use --publish to create the GitHub Release and upload the artifacts.

The version must be plain SemVer without a leading "v", for example 0.8.0.
EOF
}

die() {
	echo "error: $*" >&2
	exit 1
}

require_command() {
	local command_name="$1"

	if ! command -v "${command_name}" >/dev/null 2>&1; then
		die "${command_name} not found"
	fi
}

release_exists() {
	local tag="$1"

	gh release view "${tag}" >/dev/null 2>&1
}

extract_release_notes() {
	local version="$1"
	local notes_path="$2"

	"${PYTHON_BIN}" - "${version}" "${notes_path}" <<'PY'
from pathlib import Path
import sys

version = sys.argv[1]
notes_path = Path(sys.argv[2])
text = Path("CHANGELOG.md").read_text()
marker = f"## {version} - "
try:
    start = text.index(marker)
except ValueError as exc:
    raise SystemExit(f"CHANGELOG.md has no section for {version}") from exc
try:
    end = text.index("\n## ", start + 1)
except ValueError:
    end = len(text)
notes_path.write_text(text[start:end].strip() + "\n")
PY
}

verify_wheel_metadata() {
	local version="$1"
	local wheel_path="$2"

	"${PYTHON_BIN}" - "${version}" "${wheel_path}" <<'PY'
from pathlib import Path
import sys
import zipfile

version = sys.argv[1]
wheel_path = Path(sys.argv[2])
metadata_name = f"rp2350_relay_6ch-{version}.dist-info/METADATA"
with zipfile.ZipFile(wheel_path) as zf:
    metadata = zf.read(metadata_name).decode()
metadata_version = None
for line in metadata.splitlines():
    if line.startswith("Version:"):
        metadata_version = line.split(":", 1)[1].strip()
        break
if metadata_version != version:
    raise SystemExit(
        f"wheel metadata version {metadata_version!r} does not match {version!r}"
    )
print(f"wheel metadata version: {metadata_version}")
PY
}

verify_release_assets() {
	local tag="$1"
	shift
	local expected_assets=("$@")

	"${PYTHON_BIN}" - "${tag}" "${expected_assets[@]}" <<'PY'
import json
import subprocess
import sys

tag = sys.argv[1]
expected = set(sys.argv[2:])
result = subprocess.run(
    [
        "gh",
        "release",
        "view",
        tag,
        "--json",
        "isDraft,isPrerelease,assets",
    ],
    check=True,
    capture_output=True,
    text=True,
)
payload = json.loads(result.stdout)
if payload["isDraft"]:
    raise SystemExit(f"{tag} is a draft release")
if payload["isPrerelease"]:
    raise SystemExit(f"{tag} is a prerelease")
actual = {asset["name"] for asset in payload["assets"]}
missing = expected - actual
extra = actual - expected
if missing:
    raise SystemExit(f"{tag} is missing assets: {sorted(missing)}")
if extra:
    raise SystemExit(f"{tag} has unexpected assets: {sorted(extra)}")
print(f"{tag} assets verified: {', '.join(sorted(actual))}")
PY
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
	usage
	exit 0
fi

version="${1:-}"
publish=false

if [[ -z "${version}" ]]; then
	usage
	exit 2
fi
shift

case "${version}" in
	v*) die "version must not start with 'v'; use ${version#v}" ;;
esac
if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
	die "version must look like 0.8.0"
fi

while [[ $# -gt 0 ]]; do
	case "$1" in
		--publish)
			publish=true
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			usage
			die "unknown argument '$1'"
			;;
	esac
	shift
done

cd "${ROOT_DIR}"

if [[ -f "${VENV_DIR}/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "${VENV_DIR}/bin/activate"
fi

require_command git
require_command "${PYTHON_BIN}"
require_command west
require_command sha256sum
if [[ "${publish}" == true ]]; then
	require_command gh
fi

if [[ -n "$(git status --porcelain)" ]]; then
	die "working tree is not clean"
fi

tag="v${version}"
if ! git tag --points-at HEAD | grep -Fxq "${tag}"; then
	die "HEAD is not tagged ${tag}"
fi

declared_version="$("${PYTHON_BIN}" - <<'PY'
from pathlib import Path
import re

text = Path("pyproject.toml").read_text()
match = re.search(r'^version = "([^"]+)"$', text, re.MULTILINE)
if not match:
    raise SystemExit("pyproject.toml version not found")
print(match.group(1))
PY
)"
if [[ "${declared_version}" != "${version}" ]]; then
	die "pyproject.toml version ${declared_version} does not match ${version}"
fi

if [[ "${publish}" == true ]] && release_exists "${tag}"; then
	die "GitHub Release ${tag} already exists"
fi

wheel="dist/rp2350_relay_6ch-${version}-py3-none-any.whl"
waveshare_uf2="dist/rp2350_relay_6ch-${version}-waveshare.uf2"
pico2_uf2="dist/rp2350_relay_6ch-${version}-pico2.uf2"
asset_names=(
	"$(basename "${wheel}")"
	"$(basename "${waveshare_uf2}")"
	"$(basename "${pico2_uf2}")"
)

echo "Cleaning release outputs for ${version}"
rm -rf build host/rp2350_relay_6ch.egg-info
rm -f "${wheel}" "${waveshare_uf2}" "${pico2_uf2}"
mkdir -p dist

echo "Building host wheel"
"${PYTHON_BIN}" -m build --wheel

echo "Building Waveshare firmware"
scripts/build-firmware.sh --pristine
cp build/firmware/zephyr/zephyr.uf2 "${waveshare_uf2}"

echo "Building Pico 2 firmware"
TARGET=pico2 RELAY_OVERLAY="${PICO2_RELAY_OVERLAY}" scripts/build-firmware.sh --pristine
cp build/firmware-pico2/zephyr/zephyr.uf2 "${pico2_uf2}"

echo "Running host tests"
scripts/test-host.sh

echo "Verifying artifacts"
for artifact in "${wheel}" "${waveshare_uf2}" "${pico2_uf2}"; do
	if [[ ! -s "${artifact}" ]]; then
		die "missing or empty artifact: ${artifact}"
	fi
	ls -l "${artifact}"
done
verify_wheel_metadata "${version}" "${wheel}"

echo "SHA256 checksums"
sha256sum "${wheel}" "${waveshare_uf2}" "${pico2_uf2}"

if [[ "${publish}" != true ]]; then
	echo "Build and verification complete. Re-run with --publish to create ${tag}."
	exit 0
fi

notes_path="/tmp/rp2350-relay-${tag}-notes.md"
extract_release_notes "${version}" "${notes_path}"

echo "Publishing ${tag}"
gh release create "${tag}" \
	--title "${tag}" \
	--notes-file "${notes_path}" \
	"${wheel}" \
	"${waveshare_uf2}" \
	"${pico2_uf2}"

verify_release_assets "${tag}" "${asset_names[@]}"
