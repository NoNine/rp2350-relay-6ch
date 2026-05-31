#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-${HOME}/zephyrproject}"
VENV_DIR="${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE}/.venv}"
DEFAULT_BUILD_LUNCH="rp2350_relay_6ch-standard-userdebug"
DEFAULT_RELEASE_LUNCH="rp2350_relay_6ch-standard-user"
PYTHON_BIN=""

usage() {
	cat >&2 <<'EOF'
Usage:
  scripts/build.sh [--lunch <product_name-release_config-build_variant>] \
    [--extra-conf-file <repo-relative-fragment>] \
    [--dry-run]
  scripts/build.sh release <version> [--publish] [--allow-non-user-publish] \
    [--lunch <product_name-release_config-build_variant>] [--dry-run]

Build the complete relay-controller product: host wheel plus required firmware
UF2 images. The default build lunch is rp2350_relay_6ch-standard-userdebug.
The default release lunch is rp2350_relay_6ch-standard-user.
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

reject_forbidden_overrides() {
	local name

	for name in TARGET BOARD BUILD_DIR RELAY_OVERLAY; do
		if [[ -n "${!name+x}" ]]; then
			die "${name} is a firmware helper override; use scripts/build-firmware.sh for custom firmware builds"
		fi
	done
}

setup_python() {
	if [[ -f "${VENV_DIR}/bin/activate" ]]; then
		# shellcheck disable=SC1091
		source "${VENV_DIR}/bin/activate"
	fi

	if [[ -n "${PYTHON:-}" ]]; then
		PYTHON_BIN="${PYTHON}"
	elif [[ -x "${VENV_DIR}/bin/python" ]]; then
		PYTHON_BIN="${VENV_DIR}/bin/python"
	else
		PYTHON_BIN="python3"
	fi

	require_command "${PYTHON_BIN}"
}

get_declared_version() {
	"${PYTHON_BIN}" - <<'PY'
from pathlib import Path
import re

text = Path("pyproject.toml").read_text()
match = re.search(r'^version = "([^"]+)"$', text, re.MULTILINE)
if not match:
    raise SystemExit("pyproject.toml version not found")
print(match.group(1))
PY
}

parse_lunch_variant() {
	local lunch="$1"
	local variant

	for variant in userdebug user eng; do
		if [[ "${lunch}" == *"-${variant}" ]]; then
			TARGET_BUILD_VARIANT="${variant}"
			LUNCH_RECEIPT_NAME="${lunch%-${variant}}"
			return 0
		fi
	done

	die "lunch '${lunch}' must end with one of: user, userdebug, eng"
}

load_product_composition() {
	local lunch="$1"
	local product_path
	local release_config_path
	local lunch_product
	local lunch_release
	local fragment
	local config_records
	local record_key
	local record_value

	parse_lunch_variant "${lunch}"
	if [[ ! "${LUNCH_RECEIPT_NAME}" =~ ^([A-Za-z0-9_]+)-([A-Za-z0-9_]+)$ ]]; then
		die "lunch '${lunch}' must use product_name-release_config-build_variant"
	fi
	lunch_product="${BASH_REMATCH[1]}"
	lunch_release="${BASH_REMATCH[2]}"

	product_path="${ROOT_DIR}/products/${lunch_product}/product.yaml"
	if [[ ! -f "${product_path}" ]]; then
		die "product config '${product_path#${ROOT_DIR}/}' does not exist"
	fi

	release_config_path="${ROOT_DIR}/products/${lunch_product}/release_configs/${lunch_release}.yaml"
	if [[ ! -f "${release_config_path}" ]]; then
		die "release config '${release_config_path#${ROOT_DIR}/}' does not exist"
	fi

	TARGET_PRODUCT=""
	PRODUCT_HOST_WHEEL=""
	PRODUCT_FIRMWARE_IMAGE_LIST=()
	FIRMWARE_KCONFIG_FRAGMENT_LIST=()
	if ! config_records="$("${PYTHON_BIN}" - "${product_path}" "${release_config_path}" <<'PY'
from pathlib import Path
import sys

try:
    import yaml
except ModuleNotFoundError:
    raise SystemExit(
        "PyYAML is required to read product YAML configs; "
        "install PyYAML in the Zephyr workspace virtual environment"
    )


def repo_relative(path: Path) -> str:
    try:
        return str(path.relative_to(Path.cwd()))
    except ValueError:
        return str(path)


def load_mapping(path: Path, label: str) -> dict:
    try:
        data = yaml.safe_load(path.read_text())
    except yaml.YAMLError as exc:
        raise SystemExit(
            f"{label} config '{repo_relative(path)}' is invalid YAML: {exc}"
        ) from exc
    if not isinstance(data, dict):
        raise SystemExit(f"{label} config '{repo_relative(path)}' must be a mapping")
    return data


def require_string(data: dict, key: str, path: Path, label: str) -> str:
    value = data.get(key)
    if not isinstance(value, str) or not value:
        raise SystemExit(f"{label} config '{repo_relative(path)}' must set {key}")
    return value


def require_bool(data: dict, key: str, path: Path, label: str) -> bool:
    value = data.get(key)
    if not isinstance(value, bool):
        raise SystemExit(f"{label} config '{repo_relative(path)}' must set {key}")
    return value


def require_string_list(data: dict, key: str, path: Path, label: str) -> list[str]:
    value = data.get(key)
    if not isinstance(value, list) or not all(
        isinstance(item, str) and item for item in value
    ):
        raise SystemExit(
            f"{label} config '{repo_relative(path)}' must set {key} as a list"
        )
    return value


def emit(key: str, value: str) -> None:
    if "\n" in value or "\t" in value:
        raise SystemExit(f"{key} value must not contain tabs or newlines")
    print(f"{key}\t{value}")


product_path = Path(sys.argv[1])
release_config_path = Path(sys.argv[2])
product = load_mapping(product_path, "product")
release = load_mapping(release_config_path, "release")

emit("target_product", require_string(product, "target_product", product_path, "product"))
emit(
    "host_wheel",
    "1" if require_bool(product, "host_wheel", product_path, "product") else "0",
)
for image in require_string_list(product, "firmware_images", product_path, "product"):
    emit("image", image)
for fragment in require_string_list(
    release, "firmware_kconfig_fragments", release_config_path, "release"
):
    emit("fragment", fragment)
PY
	)"; then
		die "failed to load product composition"
	fi

	while IFS=$'\t' read -r record_key record_value; do
		case "${record_key}" in
			target_product) TARGET_PRODUCT="${record_value}" ;;
			host_wheel) PRODUCT_HOST_WHEEL="${record_value}" ;;
			image) PRODUCT_FIRMWARE_IMAGE_LIST+=("${record_value}") ;;
			fragment) FIRMWARE_KCONFIG_FRAGMENT_LIST+=("${record_value}") ;;
			"") ;;
			*) die "unknown product config record '${record_key}'" ;;
		esac
	done <<<"${config_records}"

	TARGET_RELEASE="${lunch_release}"

	if [[ -z "${TARGET_PRODUCT:-}" ]]; then
		die "product config '${product_path#${ROOT_DIR}/}' must set target_product"
	fi

	if [[ "${TARGET_PRODUCT}" != "${lunch_product}" ]]; then
		die "lunch '${lunch}' does not match product config target_product='${TARGET_PRODUCT}'"
	fi

	if [[ "${TARGET_PRODUCT}" != "rp2350_relay_6ch" ]]; then
		die "unsupported target_product '${TARGET_PRODUCT}'"
	fi

	if [[ "${PRODUCT_HOST_WHEEL}" != "1" ]]; then
		die "product config '${product_path#${ROOT_DIR}/}' must enable host_wheel=true"
	fi
	if [[ ${#PRODUCT_FIRMWARE_IMAGE_LIST[@]} -eq 0 ]]; then
		die "product config '${product_path#${ROOT_DIR}/}' must set firmware_images"
	fi
	if [[ ${#FIRMWARE_KCONFIG_FRAGMENT_LIST[@]} -eq 0 ]]; then
		die "release config '${release_config_path#${ROOT_DIR}/}' must set firmware_kconfig_fragments"
	fi

	for fragment in "${FIRMWARE_KCONFIG_FRAGMENT_LIST[@]}"; do
		if [[ ! -f "${ROOT_DIR}/${fragment}" ]]; then
			die "firmware Kconfig fragment '${fragment}' does not exist"
		fi
	done
}

append_extra_firmware_conf_fragments() {
	local fragment

	for fragment in "${EXTRA_FIRMWARE_CONF_LIST[@]}"; do
		if [[ -z "${fragment}" ]]; then
			die "--extra-conf-file requires a non-empty repo-relative path"
		fi
		if [[ "${fragment}" == /* || "${fragment}" == ".." || "${fragment}" == ../* ||
			"${fragment}" == */../* || "${fragment}" == */.. ]]; then
			die "--extra-conf-file '${fragment}' must be a repo-relative path"
		fi
		if [[ ! -f "${ROOT_DIR}/${fragment}" ]]; then
			die "firmware Kconfig fragment '${fragment}' does not exist"
		fi
		FIRMWARE_KCONFIG_FRAGMENT_LIST+=("${fragment}")
	done
}

set_image_metadata() {
	local lunch="$1"
	local version="$2"
	local image
	local overlay

	for image in "${PRODUCT_FIRMWARE_IMAGE_LIST[@]}"; do
		overlay=""
		case "${image}" in
			waveshare)
				IMAGE_BOARDS["${image}"]="waveshare_rp2350_relay_6ch/rp2350b/m33"
				;;
			pico2)
				IMAGE_BOARDS["${image}"]="rpi_pico2/rp2350a/m33"
				overlay="firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay"
				;;
			*)
				die "firmware image '${image}' has no product metadata"
				;;
		esac

		if [[ -n "${overlay}" && ! -f "${ROOT_DIR}/${overlay}" ]]; then
			die "firmware image '${image}' overlay '${overlay}' does not exist"
		fi

		IMAGE_OVERLAYS["${image}"]="${overlay}"
		IMAGE_BUILD_DIRS["${image}"]="build/product/${lunch}/${image}"
		IMAGE_ARTIFACTS["${image}"]="dist/rp2350_relay_6ch-${version}-${image}.uf2"
	done
}

join_semicolon() {
	local result=""
	local item

	for item in "$@"; do
		if [[ -n "${result}" ]]; then
			result+=";"
		fi
		result+="${item}"
	done
	printf '%s\n' "${result}"
}

write_manifest() {
	local manifest_path="$1"
	local lunch="$2"
	local version="$3"
	local host_wheel="$4"
	local host_wheel_build_dir="$5"
	local args=()
	local fragment
	local image

	for fragment in "${FIRMWARE_KCONFIG_FRAGMENT_LIST[@]}"; do
		args+=(--fragment "${fragment}")
	done
	for image in "${PRODUCT_FIRMWARE_IMAGE_LIST[@]}"; do
		args+=(
			--image
			"${image}|${IMAGE_BOARDS[${image}]}|${IMAGE_BUILD_DIRS[${image}]}|${IMAGE_OVERLAYS[${image}]}|${IMAGE_ARTIFACTS[${image}]}"
		)
	done

	mkdir -p "$(dirname "${manifest_path}")"
	find "$(dirname "${manifest_path}")" -maxdepth 1 -type f \
		-name 'rp2350_relay_6ch-*-*-product-manifest.json' -delete
	"${PYTHON_BIN}" - "${manifest_path}" "${lunch}" "${TARGET_PRODUCT}" \
		"${TARGET_RELEASE}" "${TARGET_BUILD_VARIANT}" "${version}" \
		"${host_wheel}" "${host_wheel_build_dir}" "${args[@]}" <<'PY'
import json
from pathlib import Path
import sys

manifest_path = Path(sys.argv[1])
(
    lunch,
    product,
    release,
    variant,
    version,
    host_wheel,
    host_wheel_build_dir,
) = sys.argv[2:9]
fragments = []
images = []
args = sys.argv[9:]
i = 0
while i < len(args):
    if args[i] == "--fragment":
        fragments.append(args[i + 1])
        i += 2
    elif args[i] == "--image":
        name, board, build_dir, overlay, artifact = args[i + 1].split("|", 4)
        images.append(
            {
                "name": name,
                "board": board,
                "build_dir": build_dir,
                "overlay": overlay or None,
                "artifact": artifact,
            }
        )
        i += 2
    else:
        raise SystemExit(f"unknown manifest argument: {args[i]}")

payload = {
    "lunch": lunch,
    "product": product,
    "release": release,
    "variant": variant,
    "target_product": product,
    "target_release": release,
    "target_build_variant": variant,
    "version": version,
    "host_wheel": host_wheel,
    "host_wheel_build_dir": host_wheel_build_dir,
    "firmware_kconfig_fragments": fragments,
    "firmware_images": images,
}
manifest_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
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

build_host_wheel() {
	local staging_dir="${ROOT_DIR}/${HOST_WHEEL_BUILD_DIR}"

	echo "Building host wheel"
	mkdir -p "${staging_dir}/egg-info"
	"${PYTHON_BIN}" -m build --wheel --no-isolation --skip-dependency-check \
		--outdir "${ROOT_DIR}/dist" \
		--config-setting=--build-option=--bdist-dir="${staging_dir}/bdist" \
		--config-setting=--build-option=build \
		--config-setting=--build-option=--build-base="${staging_dir}/build" \
		--config-setting=--build-option=egg_info \
		--config-setting=--build-option=--egg-base="${staging_dir}/egg-info"
	if [[ ! -s "${ROOT_DIR}/${HOST_WHEEL}" ]]; then
		die "missing or empty artifact: ${HOST_WHEEL}"
	fi
}

build_firmware_image() {
	local image="$1"
	local pristine="$2"
	local build_dir="${IMAGE_BUILD_DIRS[${image}]}"
	local artifact="${IMAGE_ARTIFACTS[${image}]}"
	local overlay="${IMAGE_OVERLAYS[${image}]}"
	local fragment_abs=()
	local fragment
	local extra_conf
	local args=()
	local env_args=()

	for fragment in "${FIRMWARE_KCONFIG_FRAGMENT_LIST[@]}"; do
		fragment_abs+=("${ROOT_DIR}/${fragment}")
	done
	extra_conf="$(join_semicolon "${fragment_abs[@]}")"

	if [[ "${pristine}" == true ]]; then
		args+=(--pristine)
	fi

	env_args=(
		"TARGET=${image}"
		"BOARD=${IMAGE_BOARDS[${image}]}"
		"BUILD_DIR=${ROOT_DIR}/${build_dir}"
		"EXTRA_CONF_FILE=${extra_conf}"
	)
	if [[ -n "${overlay}" ]]; then
		env_args+=("RELAY_OVERLAY=${ROOT_DIR}/${overlay}")
	fi

	echo "Building ${image} firmware"
	(
		cd "${ROOT_DIR}"
		env "${env_args[@]}" scripts/build-firmware.sh "${args[@]}"
	)
	if [[ ! -s "${ROOT_DIR}/${build_dir}/zephyr/zephyr.uf2" ]]; then
		die "missing firmware output for ${image}: ${build_dir}/zephyr/zephyr.uf2"
	fi
	cp "${ROOT_DIR}/${build_dir}/zephyr/zephyr.uf2" "${ROOT_DIR}/${artifact}"
}

verify_artifacts() {
	local artifact

	echo "Verifying artifacts"
	for artifact in "${RELEASE_ARTIFACTS[@]}"; do
		if [[ ! -s "${ROOT_DIR}/${artifact}" ]]; then
			die "missing or empty artifact: ${artifact}"
		fi
		ls -l "${ROOT_DIR}/${artifact}"
	done
}

run_product_build() {
	local pristine="$1"
	local image

	mkdir -p "${ROOT_DIR}/dist"
	build_host_wheel
	for image in "${PRODUCT_FIRMWARE_IMAGE_LIST[@]}"; do
		build_firmware_image "${image}" "${pristine}"
	done
}

check_release_preconditions() {
	local version="$1"
	local tag="v${version}"
	local declared_version

	require_command git
	require_command west
	require_command sha256sum
	if [[ "${PUBLISH}" == true ]]; then
		require_command gh
	fi

	if [[ -n "$(git status --porcelain)" ]]; then
		die "working tree is not clean"
	fi

	if ! git tag --points-at HEAD | grep -Fxq "${tag}"; then
		die "HEAD is not tagged ${tag}"
	fi

	declared_version="$(get_declared_version)"
	if [[ "${declared_version}" != "${version}" ]]; then
		die "pyproject.toml version ${declared_version} does not match ${version}"
	fi

	if [[ "${PUBLISH}" == true ]] && release_exists "${tag}"; then
		die "GitHub Release ${tag} already exists"
	fi
}

publish_release() {
	local version="$1"
	local tag="v${version}"
	local notes_path="/tmp/rp2350-relay-${tag}-notes.md"
	local release_files=()
	local artifact

	for artifact in "${RELEASE_ARTIFACTS[@]}"; do
		release_files+=("${ROOT_DIR}/${artifact}")
	done

	extract_release_notes "${version}" "${notes_path}"

	echo "Publishing ${tag}"
	gh release create "${tag}" \
		--title "${tag}" \
		--notes-file "${notes_path}" \
		"${release_files[@]}"

	verify_release_assets "${tag}" "${RELEASE_ASSET_NAMES[@]}"
}

COMMAND="build"
VERSION=""
PUBLISH=false
ALLOW_NON_USER_PUBLISH=false
DRY_RUN=false
EXPLICIT_LUNCH=""
EXTRA_FIRMWARE_CONF_LIST=()

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
	usage
	exit 0
fi

if [[ "${1:-}" == "release" ]]; then
	COMMAND="release"
	shift
	VERSION="${1:-}"
	if [[ -z "${VERSION}" ]]; then
		usage
		exit 2
	fi
	shift
	case "${VERSION}" in
		v*) die "version must not start with 'v'; use ${VERSION#v}" ;;
	esac
	if [[ ! "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
		die "version must look like 0.8.0"
	fi
fi

while [[ $# -gt 0 ]]; do
	case "$1" in
		--lunch)
			if [[ -z "${2:-}" ]]; then
				die "--lunch requires a value"
			fi
			EXPLICIT_LUNCH="$2"
			shift
			;;
		--extra-conf-file)
			if [[ "${COMMAND}" == "release" ]]; then
				die "--extra-conf-file is only valid with the build command"
			fi
			if [[ -z "${2:-}" ]]; then
				die "--extra-conf-file requires a value"
			fi
			EXTRA_FIRMWARE_CONF_LIST+=("$2")
			shift
			;;
		--publish)
			if [[ "${COMMAND}" != "release" ]]; then
				die "--publish is only valid with the release command"
			fi
			PUBLISH=true
			;;
		--allow-non-user-publish)
			if [[ "${COMMAND}" != "release" ]]; then
				die "--allow-non-user-publish is only valid with the release command"
			fi
			ALLOW_NON_USER_PUBLISH=true
			;;
		--dry-run)
			DRY_RUN=true
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

reject_forbidden_overrides
setup_python
cd "${ROOT_DIR}"

if [[ -n "${EXPLICIT_LUNCH}" && -n "${LUNCH:-}" && "${EXPLICIT_LUNCH}" != "${LUNCH}" ]]; then
	die "--lunch '${EXPLICIT_LUNCH}' conflicts with LUNCH='${LUNCH}'"
fi

if [[ "${COMMAND}" == "release" ]]; then
	LUNCH_TARGET="${EXPLICIT_LUNCH:-${LUNCH:-${DEFAULT_RELEASE_LUNCH}}}"
else
	LUNCH_TARGET="${EXPLICIT_LUNCH:-${LUNCH:-${DEFAULT_BUILD_LUNCH}}}"
	VERSION="$(get_declared_version)"
fi

declare -a PRODUCT_FIRMWARE_IMAGE_LIST=()
declare -a FIRMWARE_KCONFIG_FRAGMENT_LIST=()
declare -A IMAGE_BOARDS=()
declare -A IMAGE_OVERLAYS=()
declare -A IMAGE_BUILD_DIRS=()
declare -A IMAGE_ARTIFACTS=()

TARGET_BUILD_VARIANT=""
LUNCH_RECEIPT_NAME=""
load_product_composition "${LUNCH_TARGET}"
append_extra_firmware_conf_fragments

if [[ "${PUBLISH}" == true && "${TARGET_BUILD_VARIANT}" != "user" && "${ALLOW_NON_USER_PUBLISH}" != true ]]; then
	die "publishing '${TARGET_BUILD_VARIANT}' requires --allow-non-user-publish"
fi

set_image_metadata "${LUNCH_TARGET}" "${VERSION}"
HOST_WHEEL="dist/rp2350_relay_6ch-${VERSION}-py3-none-any.whl"
HOST_WHEEL_BUILD_DIR="build/product/${LUNCH_TARGET}/host-wheel"
MANIFEST="dist/rp2350_relay_6ch-${VERSION}-product-manifest.json"
RELEASE_ARTIFACTS=("${HOST_WHEEL}")
RELEASE_ASSET_NAMES=("$(basename "${HOST_WHEEL}")")
for image in "${PRODUCT_FIRMWARE_IMAGE_LIST[@]}"; do
	RELEASE_ARTIFACTS+=("${IMAGE_ARTIFACTS[${image}]}")
	RELEASE_ASSET_NAMES+=("$(basename "${IMAGE_ARTIFACTS[${image}]}")")
done

write_manifest "${ROOT_DIR}/${MANIFEST}" "${LUNCH_TARGET}" "${VERSION}" \
	"${HOST_WHEEL}" "${HOST_WHEEL_BUILD_DIR}"

echo "Resolved lunch: ${LUNCH_TARGET}"
echo "Product: ${TARGET_PRODUCT}"
echo "Release: ${TARGET_RELEASE}"
echo "Variant: ${TARGET_BUILD_VARIANT}"
echo "Manifest: ${MANIFEST}"

if [[ "${DRY_RUN}" == true ]]; then
	echo "Dry run complete; no build commands run."
	exit 0
fi

if [[ "${COMMAND}" == "release" ]]; then
	check_release_preconditions "${VERSION}"
	echo "Cleaning release outputs for ${VERSION}"
	rm -rf "${ROOT_DIR}/build/product/${LUNCH_TARGET}" \
		"${ROOT_DIR}/host/rp2350_relay_6ch.egg-info"
	for artifact in "${RELEASE_ARTIFACTS[@]}"; do
		rm -f "${ROOT_DIR}/${artifact}"
	done
	run_product_build true
	echo "Running host tests"
	scripts/test-host.sh
	verify_artifacts
	verify_wheel_metadata "${VERSION}" "${ROOT_DIR}/${HOST_WHEEL}"
	echo "SHA256 checksums"
	sha256sum "${RELEASE_ARTIFACTS[@]}"
	if [[ "${PUBLISH}" != true ]]; then
		echo "Build and verification complete. Re-run with --publish to create v${VERSION}."
		exit 0
	fi
	publish_release "${VERSION}"
else
	require_command west
	run_product_build false
	verify_artifacts
fi
