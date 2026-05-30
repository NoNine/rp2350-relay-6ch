# Product config format

Date: 2026-05-30

Status: Discussion record with follow-up implementation. This note records
design reasoning about replacing shell-style product and release config files.
The follow-up implementation moved product composition files to YAML. It does
not change the authoritative PRD, release workflow, artifact requirements, or
verification status unless those documents and scripts are updated explicitly.

## Starting point

The discussion started after the product build layer gained product-scoped
staging and boardfarm-specific firmware configuration. The current release
config format uses a shell variable such as:

```sh
FIRMWARE_KCONFIG_FRAGMENTS="firmware/profiles/always_on_owner.conf firmware/profiles/display_rotated_180.conf"
```

This works, but it becomes less human-friendly as a release composition gains
more firmware Kconfig fragments. The immediate issue was the boardfarm release
config: after OLED rotation moved out of shared `firmware/prj.conf`, boardfarm
needed both the persistent-owner fragment and the display-rotation fragment.

The broader concern is that a product or release composition should remain easy
to scan and edit. Long whitespace-separated strings are brittle because they
hide ordering, make diffs noisy, and make it harder to attach comments to
individual choices.

## Current repo model

The original product build model used shell-style `.env` files:

- `products/rp2350_relay_6ch/product.env`
- `products/rp2350_relay_6ch/release_configs/standard.env`
- `products/rp2350_relay_6ch/release_configs/boardfarm.env`

`scripts/build.sh` sources the product config, then sources the selected release
config. It converts whitespace-separated values into Bash arrays with
`read -r -a`.

The current product config provides:

```sh
TARGET_PRODUCT=rp2350_relay_6ch
PRODUCT_HOST_WHEEL=1
PRODUCT_FIRMWARE_IMAGES="waveshare pico2"
```

The current release configs provide:

```sh
FIRMWARE_KCONFIG_FRAGMENTS="firmware/profiles/standard.conf"
```

or:

```sh
FIRMWARE_KCONFIG_FRAGMENTS="firmware/profiles/always_on_owner.conf firmware/profiles/display_rotated_180.conf"
```

That model was simple and dependency-free for Bash, but the config files were
also executable shell. That was more power than product metadata needed, and it
did not provide a structured representation for lists.

## Options discussed

### Bash arrays

One low-impact option is to keep `.env` files but allow Bash arrays:

```sh
FIRMWARE_KCONFIG_FRAGMENTS=(
  firmware/profiles/always_on_owner.conf
  firmware/profiles/display_rotated_180.conf
)
```

Advantages:

- Very small implementation change.
- One fragment per line.
- Preserves order naturally.
- Can be made backward-compatible with the existing string form.
- No parser dependency.

Disadvantages:

- Still executable shell, not data.
- Easy to introduce shell syntax mistakes.
- Less portable if later tooling wants to inspect product metadata outside
  Bash.

### Shell line continuations

Another minimal option is to keep the existing string value and wrap it:

```sh
FIRMWARE_KCONFIG_FRAGMENTS="\
firmware/profiles/always_on_owner.conf \
firmware/profiles/display_rotated_180.conf"
```

Advantages:

- Almost no implementation work.
- Keeps the current parser.

Disadvantages:

- Still one logical string.
- Continuation syntax is easy to damage.
- It does not provide a real structured list.
- Comments per fragment are awkward.

### Separate fragment-list files

Another option is to make release configs reference a separate plain text file:

```sh
FIRMWARE_KCONFIG_FRAGMENT_FILE=firmware/profiles/boardfarm.fragments
```

The referenced file could contain one fragment per line.

Advantages:

- Very readable fragment lists.
- Easy to comment individual entries.
- Parser can be simple.

Disadvantages:

- Adds indirection.
- Splits one release composition across more files.
- Only solves fragment lists, not other product metadata that may grow.

### TOML

TOML would turn product and release configs into structured data:

```toml
target_product = "rp2350_relay_6ch"
host_wheel = true
firmware_images = ["waveshare", "pico2"]
```

```toml
firmware_kconfig_fragments = [
  "firmware/profiles/always_on_owner.conf",
  "firmware/profiles/display_rotated_180.conf",
]
```

Advantages:

- Structured data rather than executable shell.
- Lists, booleans, strings, and comments are first-class.
- Python 3.12 includes `tomllib`, so parsing can use the standard library.
- The repo already uses TOML for `pyproject.toml`.
- The host daemon config also uses TOML.

Disadvantages:

- Bash cannot read it directly; `scripts/build.sh` needs a Python parser shim.
- The user reported VS Code detecting TOML as plain text in their environment.
- TOML is less common than YAML in Zephyr-style embedded metadata.

The VS Code issue can be mitigated with a workspace extension recommendation,
for example recommending a TOML extension. That improves editor UX but also adds
editor-specific repository metadata.

### YAML

YAML would also make release configs structured:

```yaml
firmware_kconfig_fragments:
  - firmware/profiles/always_on_owner.conf
  - firmware/profiles/display_rotated_180.conf
```

Advantages:

- Very readable for ordered lists.
- Supports comments.
- Strong precedent in embedded projects and Zephyr-adjacent tooling.
- Familiar to many developers working with manifests, tests, and hardware
  metadata.

Disadvantages:

- Python's standard library does not include a YAML parser.
- The repo would need to rely on PyYAML or another parser.
- YAML has more syntax and implicit-typing edge cases than TOML or JSON.

In the current local workspace, both `tomllib` and `yaml` are importable from
the Zephyr virtual environment. However, the repo does not currently declare
PyYAML as a project dependency, so a YAML migration should either document that
dependency as part of the Zephyr tooling environment or add an explicit project
dependency for build tooling.

### JSON

JSON would also provide structured data:

```json
{
  "firmware_kconfig_fragments": [
    "firmware/profiles/always_on_owner.conf",
    "firmware/profiles/display_rotated_180.conf"
  ]
}
```

Advantages:

- Python standard library support.
- Strict, predictable syntax.
- Easy for machines to parse.

Disadvantages:

- No comments.
- No trailing commas.
- More punctuation noise.
- Less pleasant for hand-edited release compositions.

JSON is a good interchange format, but it is not ideal for human-maintained
product and release composition files.

## Embedded ecosystem comparison

YAML has a strong precedent in well-known embedded software projects:

- Zephyr uses YAML for west manifests, devicetree bindings, and Twister test
  metadata such as `testcase.yaml`.
- ESP-IDF uses YAML for component manager manifests such as
  `idf_component.yml`.
- Linux devicetree bindings use YAML schemas.
- OpenBMC uses YAML for D-Bus interface descriptors.

TOML has a strong precedent in Rust-centered embedded workflows:

- Rust embedded projects naturally use `Cargo.toml`.
- Tools in that ecosystem may also use TOML configuration, such as cargo-based
  flashing or probing tools.

For this repository, the product and release configs describe embedded product
composition: firmware images, firmware fragments, and release variants. That
shape is closer to Zephyr manifests, test metadata, and hardware/interface
metadata than to Rust package metadata.

## Discussion outcome

The immediate problem is real: `FIRMWARE_KCONFIG_FRAGMENTS` becomes less
human-friendly as it grows. A serious config format is preferable to stretching
shell strings indefinitely.

The strongest long-term candidate is YAML because it aligns better with common
embedded project conventions for manifests, hardware metadata, and test
metadata. It also provides the clearest visual representation for ordered
fragment lists.

TOML remains a credible alternative because it has standard-library parsing in
Python 3.12 and is already used elsewhere in this repo. It is the lower
dependency option, but the user's VS Code TOML detection issue weakens the
editor-usability argument.

Follow-up implementation selected YAML for product and release composition
files:

- `products/rp2350_relay_6ch/product.yaml`
- `products/rp2350_relay_6ch/release_configs/standard.yaml`
- `products/rp2350_relay_6ch/release_configs/boardfarm.yaml`

The implementation keeps lunch targets, manifest fields, artifact names, and
fragment ordering behavior unchanged. `scripts/build.sh` parses YAML with
PyYAML through the Zephyr workspace Python and fails early if PyYAML is not
available.

## References reviewed

- Zephyr west manifests:
  <https://docs.zephyrproject.org/latest/develop/west/manifest.html>
- Zephyr devicetree bindings:
  <https://docs.zephyrproject.org/latest/build/dts/bindings.html>
- Zephyr Twister:
  <https://docs.zephyrproject.org/latest/develop/test/twister.html>
- ESP-IDF component manifest files:
  <https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html>
- Linux devicetree binding schemas:
  <https://docs.kernel.org/devicetree/bindings/writing-schema.html>
- OpenBMC phosphor D-Bus interfaces:
  <https://github.com/openbmc/phosphor-dbus-interfaces>
- probe-rs cargo-embed:
  <https://probe.rs/docs/tools/cargo-embed/>
