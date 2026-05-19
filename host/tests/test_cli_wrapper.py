from __future__ import annotations

import importlib.util
from pathlib import Path

import rp2350_relay_6ch.cli as package_cli

ROOT_DIR = Path(__file__).resolve().parents[2]
CLI_PATH = ROOT_DIR / "tools" / "rp2350_relay_cli.py"
SPEC = importlib.util.spec_from_file_location("rp2350_relay_cli", CLI_PATH)
assert SPEC is not None
assert SPEC.loader is not None
wrapper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(wrapper)


def test_tools_cli_wrapper_delegates_to_packaged_cli() -> None:
    assert wrapper.main is package_cli.main
