"""Named-instance configuration for RP2350 relay host tooling."""

from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .exceptions import RelayValidationError

DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = 2.0
DEFAULT_RETRIES = 1
DEFAULT_RECONNECT_INTERVAL = 1.0
DEFAULT_LOG_LEVEL = "INFO"

ENV_CONFIG = "RP2350_RELAY_CONFIG"
ENV_INSTANCE = "RP2350_RELAY_INSTANCE"

_ENV_FIELDS = {
    "port": "RP2350_RELAY_PORT",
    "serial": "RP2350_RELAY_SERIAL",
    "socket": "RP2350_RELAY_SOCKET",
    "baud": "RP2350_RELAY_BAUD",
    "timeout": "RP2350_RELAY_TIMEOUT",
    "retries": "RP2350_RELAY_RETRIES",
    "reconnect_interval": "RP2350_RELAY_RECONNECT_INTERVAL",
    "wait_device": "RP2350_RELAY_WAIT_DEVICE",
    "log_level": "RP2350_RELAY_LOG_LEVEL",
}

_ALLOWED_INSTANCE_KEYS = set(_ENV_FIELDS)


@dataclass(frozen=True)
class RelayInstanceConfig:
    name: str
    selector_type: str
    selector_value: str
    socket_path: str
    baud: int = DEFAULT_BAUD
    timeout: float = DEFAULT_TIMEOUT
    retries: int = DEFAULT_RETRIES
    reconnect_interval: float = DEFAULT_RECONNECT_INTERVAL
    wait_device: bool = False
    log_level: str = DEFAULT_LOG_LEVEL
    config_path: str | None = None


def default_config_path() -> str:
    config_home = os.environ.get("XDG_CONFIG_HOME")
    if config_home:
        base = Path(os.path.expanduser(config_home))
    else:
        base = Path.home() / ".config"
    return str(base / "rp2350-relay" / "config.toml")


def configured_config_path(explicit_path: str | None = None) -> str:
    return _expand_path(explicit_path or os.environ.get(ENV_CONFIG) or default_config_path())


def load_config(path: str | None = None) -> dict[str, Any]:
    config_path = configured_config_path(path)
    try:
        with open(config_path, "rb") as config_file:
            payload = tomllib.load(config_file)
    except FileNotFoundError as exc:
        raise RelayValidationError(f"config file not found: {config_path}") from exc
    except tomllib.TOMLDecodeError as exc:
        raise RelayValidationError(f"config file is invalid TOML: {exc}") from exc
    if not isinstance(payload, dict):
        raise RelayValidationError("config root must be a TOML table")
    return payload


def resolve_instance_config(
    *,
    instance: str | None = None,
    config_path: str | None = None,
    overrides: dict[str, Any] | None = None,
    environ: dict[str, str] | None = None,
) -> RelayInstanceConfig:
    env = os.environ if environ is None else environ
    instance_name = instance or env.get(ENV_INSTANCE)
    if not instance_name:
        raise RelayValidationError("--instance is required")
    if not isinstance(instance_name, str):
        raise RelayValidationError("instance name must be a string")

    resolved_config_path = _expand_path(config_path or env.get(ENV_CONFIG) or default_config_path())
    payload = load_config(resolved_config_path)
    instances = payload.get("instances")
    if not isinstance(instances, dict):
        raise RelayValidationError("config must contain an [instances] table")
    raw_instance = instances.get(instance_name)
    if not isinstance(raw_instance, dict):
        raise RelayValidationError(f"instance not found in config: {instance_name}")

    values = dict(raw_instance)
    _validate_instance_keys(values, instance_name)
    _apply_env(values, env)
    for key, value in (overrides or {}).items():
        if value is not None:
            _clear_other_selector(values, key)
            values[key] = value

    return _coerce_instance(instance_name, values, resolved_config_path)


def resolve_socket_for_instance(
    *,
    instance: str | None = None,
    config_path: str | None = None,
    socket_override: str | None = None,
    environ: dict[str, str] | None = None,
) -> str:
    overrides = {"socket": socket_override} if socket_override is not None else None
    return resolve_instance_config(
        instance=instance,
        config_path=config_path,
        overrides=overrides,
        environ=environ,
    ).socket_path


def _validate_instance_keys(values: dict[str, Any], instance_name: str) -> None:
    unknown = sorted(set(values) - _ALLOWED_INSTANCE_KEYS)
    if unknown:
        raise RelayValidationError(
            f"instance {instance_name} has unknown config keys: {', '.join(unknown)}"
        )


def _apply_env(values: dict[str, Any], env: dict[str, str]) -> None:
    for key, env_name in _ENV_FIELDS.items():
        if env_name in env:
            _clear_other_selector(values, key)
            values[key] = env[env_name]


def _clear_other_selector(values: dict[str, Any], key: str) -> None:
    if key == "port":
        values.pop("serial", None)
    elif key == "serial":
        values.pop("port", None)


def _coerce_instance(
    name: str,
    values: dict[str, Any],
    config_path: str,
) -> RelayInstanceConfig:
    port = _optional_str(values.get("port"), "port")
    serial = _optional_str(values.get("serial"), "serial")
    if bool(port) == bool(serial):
        raise RelayValidationError("instance must define exactly one of port or serial")
    socket_path = _required_path(values.get("socket"), "socket")

    return RelayInstanceConfig(
        name=name,
        selector_type="port" if port else "serial",
        selector_value=port or serial or "",
        socket_path=socket_path,
        baud=_coerce_int(values.get("baud", DEFAULT_BAUD), "baud", minimum=1),
        timeout=_coerce_float(values.get("timeout", DEFAULT_TIMEOUT), "timeout", positive=True),
        retries=_coerce_int(values.get("retries", DEFAULT_RETRIES), "retries", minimum=0),
        reconnect_interval=_coerce_float(
            values.get("reconnect_interval", DEFAULT_RECONNECT_INTERVAL),
            "reconnect_interval",
            positive=True,
        ),
        wait_device=_coerce_bool(values.get("wait_device", False), "wait_device"),
        log_level=_coerce_str(values.get("log_level", DEFAULT_LOG_LEVEL), "log_level"),
        config_path=config_path,
    )


def _required_path(value: Any, name: str) -> str:
    text = _coerce_str(value, name)
    if not text:
        raise RelayValidationError(f"{name} is required")
    return _expand_path(text)


def _optional_str(value: Any, name: str) -> str | None:
    if value is None:
        return None
    text = _coerce_str(value, name)
    return text or None


def _coerce_str(value: Any, name: str) -> str:
    if not isinstance(value, str):
        raise RelayValidationError(f"{name} must be a string")
    return value


def _coerce_int(value: Any, name: str, *, minimum: int) -> int:
    if isinstance(value, bool):
        raise RelayValidationError(f"{name} must be an integer")
    try:
        result = int(value)
    except (TypeError, ValueError) as exc:
        raise RelayValidationError(f"{name} must be an integer") from exc
    if result < minimum:
        raise RelayValidationError(f"{name} must be >= {minimum}")
    return result


def _coerce_float(value: Any, name: str, *, positive: bool) -> float:
    if isinstance(value, bool):
        raise RelayValidationError(f"{name} must be a number")
    try:
        result = float(value)
    except (TypeError, ValueError) as exc:
        raise RelayValidationError(f"{name} must be a number") from exc
    if positive and result <= 0:
        raise RelayValidationError(f"{name} must be positive")
    return result


def _coerce_bool(value: Any, name: str) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        normalized = value.strip().lower()
        if normalized in {"1", "true", "yes", "on"}:
            return True
        if normalized in {"0", "false", "no", "off"}:
            return False
    raise RelayValidationError(f"{name} must be a bool")


def _expand_path(path: str) -> str:
    return os.path.expandvars(os.path.expanduser(path))
