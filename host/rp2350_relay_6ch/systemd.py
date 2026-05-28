"""systemd user-unit helper functions."""

from __future__ import annotations

import os
import re
import subprocess
import sys
from dataclasses import dataclass
from importlib import resources
from pathlib import Path

from .config import configured_config_path, default_config_path, resolve_instance_config
from .exceptions import RelayValidationError

UNIT_NAME = "rp2350-relayd@.service"
PACKAGE_ASSET_ROOT = "assets"


@dataclass(frozen=True)
class SystemdPaths:
    config_home: Path
    systemd_user_dir: Path
    relay_config_dir: Path
    config_path: Path
    unit_path: Path


@dataclass(frozen=True)
class DoctorResult:
    ok: bool
    messages: list[str]


def default_paths() -> SystemdPaths:
    config_home = Path(os.path.expanduser(os.environ.get("XDG_CONFIG_HOME", "~/.config")))
    systemd_user_dir = config_home / "systemd" / "user"
    relay_config_dir = config_home / "rp2350-relay"
    config_path = Path(configured_config_path())
    return SystemdPaths(
        config_home=config_home,
        systemd_user_dir=systemd_user_dir,
        relay_config_dir=relay_config_dir,
        config_path=config_path,
        unit_path=systemd_user_dir / UNIT_NAME,
    )


def render_unit(*, python_path: str | None = None, config_path: str | None = None) -> str:
    python = os.path.abspath(os.path.expanduser(python_path or sys.executable))
    config = os.path.expandvars(os.path.expanduser(config_path or default_config_path()))
    _validate_python_imports_package(python)
    template = _read_asset("rp2350-relayd@.service.in")
    return template.format(python=_systemd_escape_arg(python), config=_systemd_escape_arg(config))


def sample_config() -> str:
    return _read_asset("config.toml.example")


def install_user_unit(
    *,
    force: bool = False,
    python_path: str | None = None,
    print_unit: bool = False,
) -> str:
    paths = default_paths()
    unit = render_unit(python_path=python_path, config_path=str(paths.config_path))
    if print_unit:
        return unit

    paths.systemd_user_dir.mkdir(parents=True, exist_ok=True)
    paths.relay_config_dir.mkdir(parents=True, exist_ok=True)
    _write_file(paths.unit_path, unit, force=force)
    if not paths.config_path.exists() or force:
        _write_file(paths.config_path, sample_config(), force=force)
    return _install_summary(paths)


def doctor(*, instance: str | None = None) -> DoctorResult:
    paths = default_paths()
    messages: list[str] = []
    ok = True

    if paths.unit_path.exists():
        messages.append(f"unit: {paths.unit_path}")
    else:
        messages.append(f"missing unit: {paths.unit_path}")
        ok = False

    python = _python_from_unit(paths.unit_path) if paths.unit_path.exists() else None
    if python is None:
        if paths.unit_path.exists():
            messages.append("unit ExecStart does not use python -m rp2350_relay_6ch.daemon")
            ok = False
    elif not Path(python).exists():
        messages.append(f"missing python: {python}")
        ok = False
    else:
        try:
            _validate_python_imports_package(python)
            messages.append(f"python imports package: {python}")
        except RelayValidationError as exc:
            messages.append(str(exc))
            ok = False

    try:
        resolved = resolve_instance_config(instance=instance) if instance else None
    except RelayValidationError as exc:
        messages.append(f"config error: {exc}")
        ok = False
    else:
        if resolved is not None:
            messages.append(
                f"instance {resolved.name}: {resolved.selector_type}={resolved.selector_value}, "
                f"socket={resolved.socket_path}"
            )
        else:
            messages.append(f"config: {paths.config_path}")

    return DoctorResult(ok=ok, messages=messages)


def _write_file(path: Path, content: str, *, force: bool) -> None:
    if path.exists() and not force:
        raise RelayValidationError(f"file exists, use --force to overwrite: {path}")
    path.write_text(content, encoding="utf-8")


def _install_summary(paths: SystemdPaths) -> str:
    return "\n".join(
        [
            f"installed unit: {paths.unit_path}",
            f"config file:    {paths.config_path}",
            "",
            "Next commands:",
            "  systemctl --user daemon-reload",
            "  rp2350-relayctl systemd doctor --instance bench-a",
            "  systemctl --user enable --now rp2350-relayd@bench-a",
            "  journalctl --user -u rp2350-relayd@bench-a",
            "",
            "For PC-boot startup before login:",
            '  sudo loginctl enable-linger "$USER"',
        ]
    )


def _validate_python_imports_package(python: str) -> None:
    try:
        result = subprocess.run(
            [python, "-c", "import rp2350_relay_6ch"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except OSError as exc:
        raise RelayValidationError(f"python is not executable: {python}") from exc
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "import failed"
        raise RelayValidationError(f"python cannot import rp2350_relay_6ch: {python}: {detail}")


def _read_asset(name: str) -> str:
    return (
        resources.files("rp2350_relay_6ch")
        .joinpath(PACKAGE_ASSET_ROOT, name)
        .read_text(encoding="utf-8")
    )


def _python_from_unit(path: Path) -> str | None:
    content = path.read_text(encoding="utf-8")
    match = re.search(r"^ExecStart=(\S+)\s+-m\s+rp2350_relay_6ch\.daemon\b", content, re.MULTILINE)
    if match is None:
        return None
    return _systemd_unescape_arg(match.group(1))


def _systemd_escape_arg(value: str) -> str:
    return value.replace("%", "%%")


def _systemd_unescape_arg(value: str) -> str:
    return value.replace("%%", "%")
