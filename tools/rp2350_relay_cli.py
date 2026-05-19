#!/usr/bin/env python3
"""Compatibility wrapper for the packaged RP2350 relay CLI."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[1]
HOST_DIR = ROOT_DIR / "host"
if str(HOST_DIR) not in sys.path:
    sys.path.insert(0, str(HOST_DIR))

from rp2350_relay_6ch.cli import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
