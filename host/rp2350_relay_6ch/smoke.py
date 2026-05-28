"""Shared hardware smoke sequence helpers."""

from __future__ import annotations

import time
from collections.abc import Callable
from typing import Any, Protocol

from .constants import RELAY_COUNT


class SmokeRelayClient(Protocol):
    def get_info(self) -> dict[str, Any]: ...

    def get_status(self) -> dict[str, Any]: ...

    def pulse_relay(self, channel: int, duration_ms: int) -> dict[str, Any]: ...

    def off_all(self) -> dict[str, Any]: ...


def run_smoke_sequence(
    client: SmokeRelayClient,
    *,
    pulse_ms: int,
    sleep: Callable[[float], None] | None = None,
) -> dict[str, Any]:
    """Pulse every relay once and always attempt final all-off teardown."""
    sleep_fn = sleep or time.sleep
    results: list[dict[str, Any]] = []
    final: dict[str, Any] | None = None
    try:
        results.append({"command": "info", "response": client.get_info()})
        results.append({"command": "status", "response": client.get_status()})
        for channel in range(RELAY_COUNT):
            label = f"CH{channel + 1}"
            results.append(
                {
                    "command": "pulse",
                    "channel": label,
                    "response": client.pulse_relay(channel, pulse_ms),
                }
            )
            sleep_fn(pulse_ms / 1000.0)
        final = client.off_all()
        return {"ok": True, "results": results, "final": final}
    finally:
        if final is None:
            client.off_all()
