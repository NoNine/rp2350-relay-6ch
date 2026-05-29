# Product composition build lunch

Date: 2026-05-29

Status: Discussion. This note records design reasoning for a future
product-composition build layer inspired by Android's `lunch` model. It does
not change the authoritative PRD, implementation plan, build scripts, release
workflow, artifact requirements, or verification status unless those documents
and scripts are updated explicitly.

## Conversation context

The discussion started from a narrower idea: add firmware profile selection so
the build could choose product-level firmware Kconfig fragments without editing
`firmware/prj.conf`. That was too narrow once target, board, build directory,
relay overlay, release artifacts, and the Python host wheel were considered.

The host wheel and firmware images are released and installed together. They
share protocol compatibility, versioning, operator documentation, and release
verification. Treating firmware config as a standalone `FIRMWARE_PROFILE`
misses that the product is a composition of host tooling and one or more
firmware images.

The agreed direction is to model the build as a product composition with one
user-facing build entrypoint. Android's AOSP `lunch` model is useful as
inspiration, but the project should adapt it pragmatically rather than copy the
entire Android build environment.

## AOSP model reviewed

AOSP exposes a shell function named `lunch`. Current usage accepts an explicit
three-part form:

```sh
lunch TARGET_PRODUCT TARGET_RELEASE TARGET_BUILD_VARIANT
```

It also retains a legacy combined form:

```sh
lunch TARGET_PRODUCT-TARGET_RELEASE-TARGET_BUILD_VARIANT
```

After selection, the build environment exposes concrete variables such as
`TARGET_PRODUCT`, `TARGET_RELEASE`, `TARGET_BUILD_VARIANT`, and
`TARGET_BUILD_TYPE`. AOSP also has list and completion helpers for products,
releases, and variants.

Important lessons for this project:

- The top-level selector is not just a firmware profile.
- Product, release config, and build variant are separate axes.
- The build variant is a suffix in the lunch target, not part of the product
  or release config filename.
- A release config can map to multiple lower-level config fragments.
- User-facing build commands should hide lower-level build mechanics.

References:

- <https://source.android.com/docs/setup/build/building>
- <https://android.googlesource.com/platform/build/+/master/envsetup.sh>

## Product composition

The top-level product is:

```text
TARGET_PRODUCT=rp2350_relay_6ch
```

This name intentionally represents the complete six-channel relay controller
stack: Python host tooling plus compatible RP2350 firmware images. Individual
firmware outputs such as `waveshare` and `pico2` are image targets inside that
product composition, not separate top-level products for this build layer.

The default development lunch target should be:

```text
rp2350_relay_6ch-standard-userdebug
```

The release publish path should default to:

```text
rp2350_relay_6ch-standard-user
```

The resolved variables are:

```text
TARGET_PRODUCT=rp2350_relay_6ch
TARGET_RELEASE=standard
TARGET_BUILD_VARIANT=userdebug
```

or, for publishable release builds:

```text
TARGET_PRODUCT=rp2350_relay_6ch
TARGET_RELEASE=standard
TARGET_BUILD_VARIANT=user
```

Recognized build variants should initially be:

- `user`
- `userdebug`
- `eng`

For the first implementation, the variant should affect build directories and
manifest metadata only. It should not add Kconfig fragments, change firmware
behavior, change host behavior, or alter artifact names.

## User-facing entrypoint

The preferred user-facing entrypoint is:

```sh
scripts/build.sh
```

With no arguments, it should build the default development product composition:

```text
rp2350_relay_6ch-standard-userdebug
```

It should also support an explicit lunch target:

```sh
scripts/build.sh --lunch rp2350_relay_6ch-standard-userdebug
```

and an environment equivalent for CI or scripted use:

```sh
LUNCH=rp2350_relay_6ch-standard-userdebug scripts/build.sh
```

Release publishing should be reachable through the same front door:

```sh
scripts/build.sh release <version>
scripts/build.sh release <version> --publish
```

The release subcommand should default to the `user` variant. Publishing a
non-`user` variant should require an explicit override flag, for example:

```sh
scripts/build.sh release <version> --publish --allow-non-user-publish
```

Existing lower-level scripts should remain available:

- `scripts/build-firmware.sh`
- `scripts/test-host.sh`
- `scripts/release-github.sh`

They should be treated as developer or implementation helpers in documentation.
Normal build and release documentation should point users to `scripts/build.sh`
once this design is implemented.

## Receipt matching

The selected parsing model is receipt matching.

For a combined lunch target:

```text
rp2350_relay_6ch-standard-userdebug
```

the build script should:

1. Validate the final suffix against known variants: `user`, `userdebug`, and
   `eng`.
2. Strip the variant suffix.
3. Resolve the remaining product-release receipt:

   ```text
   products/lunch/rp2350_relay_6ch-standard.env
   ```

4. Load that receipt.
5. Verify it declares the expected product and release:

   ```sh
   TARGET_PRODUCT=rp2350_relay_6ch
   TARGET_RELEASE=standard
   ```

This avoids clever hyphen parsing. The receipt file is the authority for the
product and release values. The final variant suffix remains AOSP-like, but the
project does not need to infer product and release from arbitrary string
splitting.

## Config file layout

Product-side files should use `.env` because they are shell-style build data,
not Zephyr Kconfig fragments.

Firmware Kconfig fragments should remain `.conf` files under `firmware/`.

Proposed layout:

```text
products/
  lunch/
    rp2350_relay_6ch-standard.env
  release_configs/
    standard.env
firmware/
  profiles/
    standard.conf
```

The lunch receipt should describe the product composition: host artifacts,
firmware image targets, and the release config name.

The release config should map `TARGET_RELEASE` to an ordered list of firmware
Kconfig fragments. For the initial `standard` release:

```sh
FIRMWARE_KCONFIG_FRAGMENTS="firmware/profiles/standard.conf"
```

Later release configs can map to multiple fragments:

```sh
FIRMWARE_KCONFIG_FRAGMENTS="firmware/profiles/standard.conf firmware/profiles/comm-loss-energized.conf"
```

`firmware/profiles/standard.conf` should be behavior-neutral at first.
`firmware/prj.conf` remains the shared base for all builds.

## Firmware image targets

The product composition should initially include the same release-relevant
firmware images that the project already builds:

- `waveshare`
- `pico2`

The composition should resolve the existing build inputs for each image:

```text
waveshare:
  TARGET=waveshare
  BOARD=waveshare_rp2350_relay_6ch/rp2350b/m33
  RELAY_OVERLAY=

pico2:
  TARGET=pico2
  BOARD=rpi_pico2/rp2350a/m33
  RELAY_OVERLAY=firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay
```

Build directories should include the build variant so local outputs do not
collide unnecessarily, for example:

```text
build/product/rp2350_relay_6ch-standard-userdebug/waveshare
build/product/rp2350_relay_6ch-standard-userdebug/pico2
```

Artifact filenames should remain unchanged for now:

```text
dist/rp2350_relay_6ch-<version>-py3-none-any.whl
dist/rp2350_relay_6ch-<version>-waveshare.uf2
dist/rp2350_relay_6ch-<version>-pico2.uf2
```

Variant suffixes in artifact filenames were discussed and rejected for this
first step. Non-`user` variants should instead be identified through build
directories, console output, and the build manifest.

## Host wheel

The host wheel is part of the complete product composition.

Python wheel filenames and metadata are governed by Python packaging rules.
Adding a variant suffix to the wheel artifact without changing package metadata
would produce a misleading or invalid artifact. True variant wheels would
require package-version support such as a PEP 440 local version:

```text
0.8.8+userdebug
```

That is intentionally deferred. For now, `user`, `userdebug`, and `eng` builds
should all produce the normal wheel for the declared project version. If a
future variant changes host behavior, the packaging/versioning model should be
revisited explicitly.

## Build manifest

The build should write a local JSON manifest under `dist/`.

The manifest is for traceability. Because artifact names do not encode the
variant, the manifest records which lunch target produced the current outputs.

Example shape:

```json
{
  "lunch": "rp2350_relay_6ch-standard-userdebug",
  "target_product": "rp2350_relay_6ch",
  "target_release": "standard",
  "target_build_variant": "userdebug",
  "version": "0.8.8",
  "host_wheel": "dist/rp2350_relay_6ch-0.8.8-py3-none-any.whl",
  "firmware": [
    {
      "image": "waveshare",
      "board": "waveshare_rp2350_relay_6ch/rp2350b/m33",
      "artifact": "dist/rp2350_relay_6ch-0.8.8-waveshare.uf2",
      "kconfig_fragments": ["firmware/profiles/standard.conf"]
    }
  ]
}
```

The manifest should not become a release artifact unless that is promoted
explicitly in release docs and verification rules.

## Override policy

`scripts/build.sh` should reject direct overrides for firmware-specific inputs:

- `TARGET`
- `BOARD`
- `BUILD_DIR`
- `RELAY_OVERLAY`

The product composition should be deterministic. Custom firmware experiments can
still use lower-level scripts directly.

Lower-level scripts should keep their existing flexibility. The new front door
should not remove developer escape hatches; it should keep them out of the
normal product build path.

## Communication-loss safety

Communication-loss safety was the original example that motivated firmware
profile fragments. It is not part of this narrowed build-composition step.

This discussion does not approve or implement:

- communication-loss Kconfig symbols
- heartbeat lease behavior
- timeout actions
- protocol version changes
- host heartbeat cadence changes
- additional release artifacts

When communication-loss safety is implemented, it can add new Kconfig fragments
under `firmware/profiles/` and new `TARGET_RELEASE` mappings under
`products/release_configs/`.

## Open implementation outline

A future implementation can proceed in this order:

1. Add product `.env` receipts and behavior-neutral firmware profile fragments.
2. Add `scripts/build.sh` as the single user-facing front door.
3. Teach lower-level firmware build handling to accept ordered Zephyr
   `EXTRA_CONF_FILE` fragments.
4. Update release flow to consume the same product composition data.
5. Add dry-run tests for lunch resolution, release config fragment order,
   publish safety, override rejection, and manifest contents.
6. Update user-facing docs to prefer `scripts/build.sh`.

## Deferred decisions

- Whether variant-specific host behavior should ever produce PEP 440 local
  version wheels.
- Whether `eng` should eventually map to debug Kconfig fragments.
- Whether release manifests should be published as GitHub Release assets.
- Whether future release configs should include communication-loss safety
  fragments by default.
- Whether old lower-level script names should eventually be deprecated or kept
  indefinitely as developer tools.
