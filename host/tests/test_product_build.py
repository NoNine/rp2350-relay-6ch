import json
import os
import subprocess
import tomllib
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
BUILD_SCRIPT = ROOT_DIR / "scripts" / "build.sh"
VERSION = tomllib.loads((ROOT_DIR / "pyproject.toml").read_text())["project"]["version"]


def run_build(
    *args: str, env: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    command_env = os.environ.copy()
    for name in ("TARGET", "BOARD", "BUILD_DIR", "RELAY_OVERLAY", "LUNCH"):
        command_env.pop(name, None)
    if env:
        command_env.update(env)

    return subprocess.run(
        [str(BUILD_SCRIPT), *args],
        cwd=ROOT_DIR,
        env=command_env,
        check=False,
        capture_output=True,
        text=True,
    )


def read_manifest(lunch: str) -> dict:
    path = ROOT_DIR / "dist" / f"{lunch}-product-manifest.json"
    return json.loads(path.read_text())


def test_default_build_resolves_userdebug_product() -> None:
    result = run_build("--dry-run")

    assert result.returncode == 0, result.stderr
    assert "Resolved lunch: rp2350_relay_6ch-standard-userdebug" in result.stdout
    manifest = read_manifest("rp2350_relay_6ch-standard-userdebug")
    assert manifest["product"] == "rp2350_relay_6ch"
    assert manifest["release"] == "standard"
    assert manifest["variant"] == "userdebug"


def test_release_build_resolves_user_product() -> None:
    result = run_build("release", VERSION, "--dry-run")

    assert result.returncode == 0, result.stderr
    assert "Resolved lunch: rp2350_relay_6ch-standard-user" in result.stdout
    manifest = read_manifest("rp2350_relay_6ch-standard-user")
    assert manifest["variant"] == "user"


def test_lunch_env_and_flag_must_match() -> None:
    result = run_build(
        "--dry-run",
        "--lunch",
        "rp2350_relay_6ch-standard-userdebug",
        env={"LUNCH": "rp2350_relay_6ch-standard-userdebug"},
    )
    assert result.returncode == 0, result.stderr

    conflict = run_build(
        "--dry-run",
        "--lunch",
        "rp2350_relay_6ch-standard-userdebug",
        env={"LUNCH": "rp2350_relay_6ch-standard-eng"},
    )
    assert conflict.returncode != 0
    assert "conflicts with LUNCH" in conflict.stderr


def test_ordered_kconfig_fragments_and_release_artifacts_are_preserved() -> None:
    result = run_build("release", VERSION, "--dry-run")

    assert result.returncode == 0, result.stderr
    manifest = read_manifest("rp2350_relay_6ch-standard-user")
    assert manifest["firmware_kconfig_fragments"] == [
        "firmware/profiles/standard.conf"
    ]
    assert (
        manifest["host_wheel"]
        == f"dist/rp2350_relay_6ch-{VERSION}-py3-none-any.whl"
    )
    assert (
        manifest["host_wheel_build_dir"]
        == "build/product/rp2350_relay_6ch-standard-user/host-wheel"
    )
    assert [image["artifact"] for image in manifest["firmware_images"]] == [
        f"dist/rp2350_relay_6ch-{VERSION}-waveshare.uf2",
        f"dist/rp2350_relay_6ch-{VERSION}-pico2.uf2",
    ]


def test_boardfarm_release_config_resolves_boardfarm_fragments() -> None:
    result = run_build("--dry-run", "--lunch", "rp2350_relay_6ch-boardfarm-userdebug")

    assert result.returncode == 0, result.stderr
    assert "Resolved lunch: rp2350_relay_6ch-boardfarm-userdebug" in result.stdout
    manifest = read_manifest("rp2350_relay_6ch-boardfarm-userdebug")
    assert manifest["release"] == "boardfarm"
    assert manifest["firmware_kconfig_fragments"] == [
        "firmware/profiles/always_on_owner.conf",
        "firmware/profiles/display_rotated_180.conf",
    ]


def test_extra_conf_file_appends_temporary_fragment() -> None:
    result = run_build(
        "--dry-run",
        "--extra-conf-file",
        "firmware/profiles/no_comm_timeout.conf",
    )

    assert result.returncode == 0, result.stderr
    manifest = read_manifest("rp2350_relay_6ch-standard-userdebug")
    assert manifest["firmware_kconfig_fragments"] == [
        "firmware/profiles/standard.conf",
        "firmware/profiles/no_comm_timeout.conf",
    ]


def test_extra_conf_file_can_repeat_preserving_order() -> None:
    result = run_build(
        "--dry-run",
        "--extra-conf-file",
        "firmware/profiles/no_comm_timeout.conf",
        "--extra-conf-file",
        "firmware/profiles/always_on_owner.conf",
    )

    assert result.returncode == 0, result.stderr
    manifest = read_manifest("rp2350_relay_6ch-standard-userdebug")
    assert manifest["firmware_kconfig_fragments"] == [
        "firmware/profiles/standard.conf",
        "firmware/profiles/no_comm_timeout.conf",
        "firmware/profiles/always_on_owner.conf",
    ]


def test_manifest_records_image_metadata() -> None:
    result = run_build("--dry-run")

    assert result.returncode == 0, result.stderr
    manifest = read_manifest("rp2350_relay_6ch-standard-userdebug")
    images = {image["name"]: image for image in manifest["firmware_images"]}
    assert images["waveshare"]["board"] == "waveshare_rp2350_relay_6ch/rp2350b/m33"
    assert images["waveshare"]["overlay"] is None
    assert images["pico2"]["board"] == "rpi_pico2/rp2350a/m33"
    assert (
        images["pico2"]["overlay"]
        == "firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay"
    )
    assert images["pico2"]["build_dir"].endswith("/pico2")


def test_forbidden_firmware_override_fails_before_build() -> None:
    result = run_build("--dry-run", env={"TARGET": "pico2"})

    assert result.returncode != 0
    assert "TARGET is a firmware helper override" in result.stderr
    assert "Building host wheel" not in result.stdout


def test_unknown_variant_missing_product_and_missing_release_fail_before_build() -> None:
    bad_variant = run_build("--dry-run", "--lunch", "rp2350_relay_6ch-standard-prod")
    assert bad_variant.returncode != 0
    assert "must end with one of" in bad_variant.stderr

    missing_product = run_build("--dry-run", "--lunch", "unknown-standard-userdebug")
    assert missing_product.returncode != 0
    assert "product config" in missing_product.stderr
    assert "products/unknown/product.env" in missing_product.stderr
    assert "does not exist" in missing_product.stderr

    missing_release = run_build(
        "--dry-run", "--lunch", "rp2350_relay_6ch-missing-userdebug"
    )
    assert missing_release.returncode != 0
    assert "release config" in missing_release.stderr
    assert (
        "products/rp2350_relay_6ch/release_configs/missing.env"
        in missing_release.stderr
    )
    assert "does not exist" in missing_release.stderr

    missing_extra_conf = run_build(
        "--dry-run", "--extra-conf-file", "firmware/profiles/missing.conf"
    )
    assert missing_extra_conf.returncode != 0
    assert (
        "firmware Kconfig fragment 'firmware/profiles/missing.conf' does not exist"
        in missing_extra_conf.stderr
    )


def test_non_user_publish_requires_override() -> None:
    result = run_build(
        "release",
        VERSION,
        "--dry-run",
        "--publish",
        "--lunch",
        "rp2350_relay_6ch-standard-userdebug",
    )

    assert result.returncode != 0
    assert "requires --allow-non-user-publish" in result.stderr

    allowed = run_build(
        "release",
        VERSION,
        "--dry-run",
        "--publish",
        "--allow-non-user-publish",
        "--lunch",
        "rp2350_relay_6ch-standard-userdebug",
    )
    assert allowed.returncode == 0, allowed.stderr


def test_extra_conf_file_is_rejected_for_release_build() -> None:
    result = run_build(
        "release",
        VERSION,
        "--dry-run",
        "--extra-conf-file",
        "firmware/profiles/no_comm_timeout.conf",
    )

    assert result.returncode != 0
    assert "--extra-conf-file is only valid with the build command" in result.stderr
