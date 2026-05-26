from pathlib import Path
import tomllib

from rp2350_relay_6ch import __version__


def test_package_imports() -> None:
    pyproject = tomllib.loads(
        (Path(__file__).resolve().parents[2] / "pyproject.toml").read_text()
    )
    assert __version__ == pyproject["project"]["version"]
