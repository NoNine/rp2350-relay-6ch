import os
import subprocess
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
FLASH_SCRIPT = ROOT_DIR / "scripts" / "flash.sh"


def run_flash(
    *args: str, env: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    command_env = os.environ.copy()
    command_env.pop("LUNCH", None)
    if env:
        command_env.update(env)

    return subprocess.run(
        [str(FLASH_SCRIPT), *args],
        cwd=ROOT_DIR,
        env=command_env,
        check=False,
        capture_output=True,
        text=True,
    )


def test_flash_dry_run_resolves_default_waveshare_product_build() -> None:
    result = run_flash("--dry-run")

    assert result.returncode == 0, result.stderr
    assert "Resolved lunch: rp2350_relay_6ch-standard-userdebug" in result.stdout
    assert "Target: waveshare" in result.stdout
    assert (
        "Build dir: build/product/rp2350_relay_6ch-standard-userdebug/waveshare"
        in result.stdout
    )
    assert (
        "Dry run: west flash -d build/product/rp2350_relay_6ch-standard-userdebug/waveshare"
        in result.stdout
    )


def test_flash_dry_run_resolves_explicit_pico2_product_build() -> None:
    result = run_flash(
        "--dry-run",
        "--target",
        "pico2",
        "--lunch",
        "rp2350_relay_6ch-standard-user",
    )

    assert result.returncode == 0, result.stderr
    assert "Resolved lunch: rp2350_relay_6ch-standard-user" in result.stdout
    assert "Target: pico2" in result.stdout
    assert "Build dir: build/product/rp2350_relay_6ch-standard-user/pico2" in result.stdout


def test_flash_lunch_env_and_flag_must_match() -> None:
    result = run_flash(
        "--dry-run",
        "--lunch",
        "rp2350_relay_6ch-standard-userdebug",
        env={"LUNCH": "rp2350_relay_6ch-standard-eng"},
    )

    assert result.returncode != 0
    assert "conflicts with LUNCH" in result.stderr


def test_flash_rejects_unknown_target() -> None:
    result = run_flash("--dry-run", "--target", "pico2w")

    assert result.returncode != 0
    assert "unknown target 'pico2w'" in result.stderr
