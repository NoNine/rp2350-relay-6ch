# Product Build Contract

Date: 2026-05-29

Status: Standalone implementation contract. This product build contract is not
assigned to an implementation phase. Do not update completed phase plans, the
PRD, release docs, or phase verification reports for this contract unless the
user explicitly requests that broader promotion.

Design rationale lives in
[Product composition build lunch](discussions/product-composition-build-lunch.md).
This document is the concise implementer-facing contract.

## Summary

The product build is the host-plus-firmware composition for the relay
controller. A complete product build contains the Python host wheel and the
compatible firmware UF2 images for the supported product image targets.

The user-facing entrypoint is:

```sh
scripts/build.sh
```

Lunch targets use the AOSP-style human-facing shape:

```text
product_name-release_config-build_variant
```

For this repo, those fields resolve to internal `TARGET_*` variables with
initial values:

- `TARGET_PRODUCT=rp2350_relay_6ch`
- `TARGET_RELEASE=standard`
- additional release config: `boardfarm`
- variants: `user`, `userdebug`, `eng`
- default build lunch: `rp2350_relay_6ch-standard-userdebug`
- default release lunch: `rp2350_relay_6ch-standard-user`

`TARGET_BUILD_VARIANT` affects build
directories, publish safety, and manifest metadata only. It must not change
firmware behavior, host behavior, Kconfig fragments, protocol fields, or
artifact names.

Release configs are for supported product or release compositions, not every
firmware feature option. Build variants do not change firmware behavior, host
behavior, Kconfig fragments, protocol fields, or artifact names.

## Public Interface

No-argument build:

```sh
scripts/build.sh
```

Explicit lunch:

```sh
scripts/build.sh --lunch rp2350_relay_6ch-standard-userdebug
scripts/build.sh --lunch rp2350_relay_6ch-boardfarm-userdebug
LUNCH=rp2350_relay_6ch-standard-userdebug scripts/build.sh
```

Temporary firmware fragment:

```sh
scripts/build.sh --extra-conf-file firmware/profiles/no_comm_timeout.conf
```

Flash product firmware outputs after a build:

```sh
scripts/flash.sh
scripts/flash.sh --target pico2
scripts/flash.sh --lunch rp2350_relay_6ch-standard-userdebug
```

Release path:

```sh
scripts/build.sh release <version>
scripts/build.sh release <version> --publish
scripts/build.sh release <version> --lunch rp2350_relay_6ch-standard-user
```

Publishing a non-`user` variant must fail unless an explicit override flag is
present:

```sh
scripts/build.sh release <version> --publish \
  --lunch rp2350_relay_6ch-standard-userdebug \
  --allow-non-user-publish
```

`--lunch` and `LUNCH` must not conflict. If both are set to the same value, the
explicit `--lunch` value may be used.

Existing lower-level scripts remain developer helpers:

- `scripts/build-firmware.sh`
- `scripts/test-host.sh`
- `scripts/release-github.sh`

`scripts/build.sh` must reject direct `TARGET`, `BOARD`, `BUILD_DIR`, and
`RELAY_OVERLAY` overrides. Use lower-level scripts for custom firmware
experiments.

For temporary local firmware profile experiments, `--extra-conf-file` appends a
repo-relative Kconfig fragment after the selected release config fragments. It
is rejected by the `release` command.

## Composition Rules

The build resolves `product_name-release_config-build_variant` directly. For
`rp2350_relay_6ch-standard-userdebug`, strip the known variant suffix and load:

```text
products/rp2350_relay_6ch/product.yaml
products/rp2350_relay_6ch/release_configs/standard.yaml
```

`scripts/build.sh` parses these YAML files with PyYAML through the selected
Zephyr workspace Python interpreter.

Required initial layout:

```text
products/
  rp2350_relay_6ch/
    product.yaml
    release_configs/
      standard.yaml
      boardfarm.yaml
firmware/
  profiles/
    standard.conf
```

The product config declares at least:

```yaml
target_product: rp2350_relay_6ch
host_wheel: true
firmware_images:
  - waveshare
  - pico2
```

`TARGET_RELEASE` is selected in the context of `target_product` from the lunch
target. Directory nesting defines valid combinations.

Release configs map `TARGET_RELEASE` to an ordered list of firmware Kconfig
fragments. The standard profile uses energized-only communication-loss safety
with a 5 s timeout:

```yaml
firmware_kconfig_fragments:
  - firmware/profiles/standard.conf
```

The fragment order is significant and must be preserved when passed to Zephyr
`EXTRA_CONF_FILE`. `firmware/prj.conf` remains the shared base.
`firmware/profiles/standard.conf` selects the standard energized-only
communication-loss policy.

The `boardfarm` release config selects `firmware/profiles/always_on_owner.conf`
for board-farm and lab compositions that need a persistent owner lease. That
profile also enables reboot-on-timeout recovery: after a 5 s owner timeout,
firmware turns all relays off, shows local owner-lost attention first, then
shows reboot-pending indication for the final 10 s. If no heartbeat or
relay-control command restores ownership during the 60 s reboot delay, firmware
cold reboots to recover the application and USB communication path. The release
config also selects
`firmware/profiles/display_rotated_180.conf` for the boardfarm OLED mounting
orientation.

Initial firmware image targets:

- `waveshare`: `waveshare_rp2350_relay_6ch/rp2350b/m33`
- `pico2`: `rpi_pico2/rp2350a/m33` with
  `firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay`

Build directories include the lunch target, for example:

```text
build/product/rp2350_relay_6ch-standard-userdebug/host-wheel
build/product/rp2350_relay_6ch-standard-userdebug/waveshare
build/product/rp2350_relay_6ch-standard-userdebug/pico2
```

Artifact filenames stay unchanged:

```text
dist/rp2350_relay_6ch-<version>-py3-none-any.whl
dist/rp2350_relay_6ch-<version>-waveshare.uf2
dist/rp2350_relay_6ch-<version>-pico2.uf2
```

`scripts/build.sh` writes a local JSON manifest under `dist/` recording lunch,
product, release, variant, version, host wheel, host wheel build dir, firmware
images, boards, build dirs, overlays, artifacts, and ordered Kconfig fragments.
The manifest is not a GitHub Release artifact unless release docs are
explicitly updated later.

## Failure Rules

`scripts/build.sh` must fail before building anything when:

- the lunch target is malformed or uses an unknown variant
- the product config or product-scoped release config is missing
- the product config's declared product does not match the lunch target
- any ordered Kconfig fragment is missing
- required firmware image metadata is missing
- forbidden firmware overrides are set
- `--lunch` and `LUNCH` conflict
- `--publish` targets a non-`user` variant without the override flag

Errors should name the failing input and expected action. Never silently fall
back to a different product, release, variant, board, or overlay.

## Test Contract

Add focused dry-run tests for the product build layer:

- default build resolves `rp2350_relay_6ch-standard-userdebug`
- release build resolves `rp2350_relay_6ch-standard-user`
- explicit `--lunch` and `LUNCH` behavior
- temporary `--extra-conf-file` ordering and release-command rejection
- unknown variants, missing product or release configs, missing fragments, and forbidden
  overrides fail before build commands run
- non-`user` publish guard requires `--allow-non-user-publish`
- manifest content includes lunch, variant, artifacts, image metadata, and
  ordered fragments
- flash dry-run resolves the default Waveshare product build directory and
  explicit Pico 2 target directory

Minimum verification after implementation:

```sh
bash -n scripts/build.sh scripts/build-firmware.sh scripts/flash.sh scripts/release-github.sh
${ZEPHYR_VENV:-${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/.venv}/bin/python \
  -m pytest <focused-product-build-tests>
scripts/build.sh
```

Implementation handoff must state which artifacts were rebuilt and why.
