from __future__ import annotations

import sys
from pathlib import Path

import pytest

from rp2350_relay_6ch import systemd
from rp2350_relay_6ch.exceptions import RelayValidationError


def test_render_unit_uses_absolute_python_and_config(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(systemd, "_validate_python_imports_package", lambda python: None)

    unit = systemd.render_unit(
        python_path=sys.executable,
        config_path="/tmp/rp2350-relay/config.toml",
    )

    assert f"ExecStart={sys.executable} -m rp2350_relay_6ch.daemon --instance %i" in unit
    assert "Environment=RP2350_RELAY_CONFIG=/tmp/rp2350-relay/config.toml" in unit
    assert "EnvironmentFile=-%h/.config/rp2350-relay/%i.env" in unit


def test_install_user_unit_writes_templates(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    monkeypatch.setattr(systemd, "_validate_python_imports_package", lambda python: None)

    summary = systemd.install_user_unit(python_path=sys.executable)

    unit_path = tmp_path / "systemd" / "user" / "rp2350-relayd@.service"
    config_path = tmp_path / "rp2350-relay" / "config.toml"
    assert unit_path.exists()
    assert config_path.exists()
    assert f"ExecStart={sys.executable} -m rp2350_relay_6ch.daemon --instance %i" in unit_path.read_text(
        encoding="utf-8"
    )
    assert "[instances.bench-a]" in config_path.read_text(encoding="utf-8")
    assert "systemctl --user daemon-reload" in summary
    assert "systemctl --user enable --now rp2350-relayd@bench-a" in summary
    assert 'sudo loginctl enable-linger "$USER"' in summary


def test_install_user_unit_refuses_overwrite_without_force(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    monkeypatch.setattr(systemd, "_validate_python_imports_package", lambda python: None)
    systemd.install_user_unit(python_path=sys.executable)

    with pytest.raises(RelayValidationError, match="--force"):
        systemd.install_user_unit(python_path=sys.executable)


def test_doctor_reports_valid_unit_and_instance(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    monkeypatch.setattr(systemd, "_validate_python_imports_package", lambda python: None)
    systemd.install_user_unit(python_path=sys.executable)

    result = systemd.doctor(instance="bench-a")

    assert result.ok is True
    assert any("python imports package" in message for message in result.messages)
    assert any("instance bench-a" in message for message in result.messages)
