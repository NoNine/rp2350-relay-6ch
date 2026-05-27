from __future__ import annotations

from pathlib import Path
from typing import Any

import pytest

from rp2350_relay_6ch.config import resolve_instance_config, resolve_socket_for_instance
from rp2350_relay_6ch.exceptions import RelayValidationError


def write_config(tmp_path: Path, body: str) -> str:
    path = tmp_path / "config.toml"
    path.write_text(body, encoding="utf-8")
    return str(path)


def test_resolve_instance_config_from_global_toml(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("XDG_RUNTIME_DIR", str(tmp_path / "run"))
    config_path = write_config(
        tmp_path,
        """
[instances.bench-a]
serial = "abc"
socket = "${XDG_RUNTIME_DIR}/rp2350-relay/bench-a.sock"
wait_device = true
baud = 9600
timeout = 3.5
retries = 2
reconnect_interval = 4.0
log_level = "DEBUG"
""",
    )

    resolved = resolve_instance_config(instance="bench-a", config_path=config_path)

    assert resolved.selector_type == "serial"
    assert resolved.selector_value == "abc"
    assert resolved.socket_path == str(tmp_path / "run" / "rp2350-relay" / "bench-a.sock")
    assert resolved.wait_device is True
    assert resolved.baud == 9600
    assert resolved.timeout == 3.5
    assert resolved.retries == 2
    assert resolved.reconnect_interval == 4.0
    assert resolved.log_level == "DEBUG"


def test_resolve_instance_precedence_cli_env_toml(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    config_path = write_config(
        tmp_path,
        """
[instances.bench-a]
serial = "toml"
socket = "/tmp/toml.sock"
wait_device = false
""",
    )
    monkeypatch.setenv("RP2350_RELAY_SERIAL", "env")
    monkeypatch.setenv("RP2350_RELAY_SOCKET", "/tmp/env.sock")

    resolved = resolve_instance_config(
        instance="bench-a",
        config_path=config_path,
        overrides={"serial": "cli"},
    )

    assert resolved.selector_type == "serial"
    assert resolved.selector_value == "cli"
    assert resolved.socket_path == "/tmp/env.sock"


def test_selector_override_replaces_lower_precedence_selector(tmp_path: Path) -> None:
    config_path = write_config(
        tmp_path,
        """
[instances.bench-a]
serial = "toml"
socket = "/tmp/toml.sock"
""",
    )

    resolved = resolve_instance_config(
        instance="bench-a",
        config_path=config_path,
        overrides={"port": "/dev/ttyACM0"},
    )

    assert resolved.selector_type == "port"
    assert resolved.selector_value == "/dev/ttyACM0"


@pytest.mark.parametrize(
    ("body", "message"),
    [
        ("[instances.bench-a]\nsocket = '/tmp/a.sock'\n", "exactly one"),
        (
            "[instances.bench-a]\nport = '/dev/ttyACM0'\nserial = 'abc'\nsocket = '/tmp/a.sock'\n",
            "exactly one",
        ),
        ("[instances.bench-a]\nserial = 'abc'\n", "socket"),
        ("[instances.bench-a]\nserial = 'abc'\nsocket = '/tmp/a.sock'\nextra = true\n", "unknown"),
    ],
)
def test_resolve_instance_rejects_invalid_config(
    tmp_path: Path,
    body: str,
    message: str,
) -> None:
    config_path = write_config(tmp_path, body)

    with pytest.raises(RelayValidationError, match=message):
        resolve_instance_config(instance="bench-a", config_path=config_path)


def test_resolve_socket_for_instance(tmp_path: Path) -> None:
    config_path = write_config(
        tmp_path,
        """
[instances.bench-a]
port = "/dev/ttyACM0"
socket = "/tmp/bench-a.sock"
""",
    )

    assert (
        resolve_socket_for_instance(instance="bench-a", config_path=config_path)
        == "/tmp/bench-a.sock"
    )
