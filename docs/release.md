# Release Workflow

Use this procedure to prepare and publish a GitHub Release. Operator install
instructions live in [CLI utility](cli.md); this document is for maintainers.

## Required Artifacts

Every GitHub Release must include:

- `rp2350_relay_6ch-<version>-py3-none-any.whl`: host CLI wheel.
- `rp2350_relay_6ch-<version>-waveshare.uf2`: Waveshare firmware.
- `rp2350_relay_6ch-<version>-pico2.uf2`: Raspberry Pi Pico 2 firmware.

Additional artifacts such as a source distribution, Pico 2 W firmware, or
platform executables may be attached when useful. Firmware UF2 artifact names
should use short release qualifiers such as `waveshare`, `pico2`, or `pico2w`,
not full Zephyr board names.

## Prerequisites

- Start from a clean worktree on the release commit.
- Ensure the Zephyr workspace virtual environment is available, normally
  `${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv`.
- Ensure `git`, `gh`, `west`, and Python build tooling are available.
- Ensure the release commit is pushed and has an annotated tag named
  `v<version>`.
- Confirm the GitHub Release does not already exist:

```sh
gh release view "v<version>"
```

## Automated Release

Use the product build script for the fixed build, verification, and publish
workflow:

```sh
scripts/build.sh release <version>
```

The default mode builds and verifies the required artifacts without publishing.
After reviewing the output, publish the GitHub Release with:

```sh
scripts/build.sh release <version> --publish
```

The script verifies the working tree is clean, `HEAD` is tagged with
`v<version>`, `pyproject.toml` matches the requested version, and the GitHub
Release does not already exist before publishing. It builds the host wheel,
Waveshare UF2, and Pico 2 UF2, runs host tests, verifies wheel metadata,
prints SHA256 checksums, extracts release notes from `CHANGELOG.md`, and
verifies the uploaded GitHub Release assets.

`scripts/release-github.sh <version> [--publish]` remains as a compatibility
wrapper for this product release path.

The remaining sections document the manual workflow that the script implements.

## Version And Changelog

Before tagging the release, update all project version declarations that apply
to the release:

- `pyproject.toml`
- `host/rp2350_relay_6ch/__init__.py`
- host tests that assert the package version
- `firmware/CMakeLists.txt`
- firmware test CMake files that carry the application version

Add a top `CHANGELOG.md` section for the new version. Include highlights,
install/assets, verification, known limitations, and safety notes. The release
notes used on GitHub should come from this changelog section.

When Codex creates the release commit, use summary `Release <version>` and
include `Prompt:` and `Conversation context:` sections in the commit body,
following `AGENTS.md`.

Create and push the release tag:

```sh
git tag -a "v<version>" -m "Release <version>"
git push origin master
git push origin "v<version>"
```

## Clean Build

Remove release-relevant stale build and package outputs before building:

```sh
rm -rf build host/rp2350_relay_6ch.egg-info
rm -f "dist/rp2350_relay_6ch-<version>-py3-none-any.whl"
rm -f "dist/rp2350_relay_6ch-<version>-waveshare.uf2"
rm -f "dist/rp2350_relay_6ch-<version>-pico2.uf2"
```

The product build script performs the clean host and firmware build:

```sh
scripts/build.sh release <version>
```

Optional platform executables are built separately on the matching operating
system:

```sh
python -m pip install -e '.[release]'
scripts/build-host-executable.sh
```

On Windows, use PowerShell:

```powershell
python -m pip install -e ".[release]"
python scripts\build_host_executable.py
```

## Verification

Run the host tests:

```sh
scripts/test-host.sh
```

Verify required artifacts exist and are nonzero:

```sh
ls -l \
  "dist/rp2350_relay_6ch-<version>-py3-none-any.whl" \
  "dist/rp2350_relay_6ch-<version>-waveshare.uf2" \
  "dist/rp2350_relay_6ch-<version>-pico2.uf2"
```

Verify the wheel metadata version and any release-critical content checks:

```sh
python - <<'PY'
from pathlib import Path
import zipfile

version = "<version>"
wheel = Path(f"dist/rp2350_relay_6ch-{version}-py3-none-any.whl")
with zipfile.ZipFile(wheel) as zf:
    metadata = zf.read(f"rp2350_relay_6ch-{version}.dist-info/METADATA").decode()
for line in metadata.splitlines():
    if line.startswith(("Name:", "Version:")):
        print(line)
PY
```

Compute checksums for the final report:

```sh
sha256sum \
  "dist/rp2350_relay_6ch-<version>-py3-none-any.whl" \
  "dist/rp2350_relay_6ch-<version>-waveshare.uf2" \
  "dist/rp2350_relay_6ch-<version>-pico2.uf2"
```

Record observed build warnings in the release summary when relevant, for
example environment-level setuptools deprecation warnings or Zephyr warnings.

## Publish

Extract the matching changelog section to a release-notes file:

```sh
python - <<'PY'
from pathlib import Path

version = "<version>"
text = Path("CHANGELOG.md").read_text()
start = text.index(f"## {version} - ")
try:
    end = text.index("\n## ", start + 1)
except ValueError:
    end = len(text)
Path(f"/tmp/rp2350-relay-v{version}-notes.md").write_text(
    text[start:end].strip() + "\n"
)
PY
```

Create the GitHub Release and attach exactly the required artifacts:

```sh
gh release create "v<version>" \
  --title "v<version>" \
  --notes-file "/tmp/rp2350-relay-v<version>-notes.md" \
  "dist/rp2350_relay_6ch-<version>-py3-none-any.whl" \
  "dist/rp2350_relay_6ch-<version>-waveshare.uf2" \
  "dist/rp2350_relay_6ch-<version>-pico2.uf2"
```

Verify the published release:

```sh
gh release view "v<version>" --json tagName,name,isDraft,isPrerelease,url,assets
```

The release must not be a draft or prerelease unless explicitly intended, and
all required artifacts must be uploaded.
