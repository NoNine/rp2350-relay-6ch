#!/usr/bin/env python3
"""Build a platform-native host CLI executable."""

from __future__ import annotations

import platform
import subprocess
import sys
from pathlib import Path

from rp2350_relay_6ch import __version__


ROOT_DIR = Path(__file__).resolve().parents[1]


def _machine() -> str:
    machine = platform.machine().lower()
    return {
        "amd64": "x64",
        "x86_64": "x64",
        "aarch64": "arm64",
    }.get(machine, machine)


def artifact_name() -> str:
    system = platform.system().lower()
    machine = _machine()
    if system == "windows":
        suffix = f"windows-{machine}"
    elif system == "darwin":
        suffix = f"macos-{machine}"
    elif system == "linux":
        suffix = f"linux-{machine}"
    else:
        suffix = f"{system}-{machine}"
    return f"rp2350_relay_6ch-{__version__}-{suffix}"


def main() -> int:
    name = artifact_name()
    spec_dir = ROOT_DIR / "build" / "pyinstaller-spec"
    spec_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            sys.executable,
            "-m",
            "PyInstaller",
            "--clean",
            "--onefile",
            "--name",
            name,
            "--specpath",
            str(spec_dir),
            "--paths",
            "host",
            "tools/rp2350_relay_cli.py",
        ],
        cwd=ROOT_DIR,
        check=True,
    )
    extension = ".exe" if platform.system().lower() == "windows" else ""
    print(f"Built dist/{name}{extension}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
