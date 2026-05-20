# Deferred Remaining Features Review

Date: 2026-05-20

Status: Deferred. This note preserves review findings for future planning. It
does not change the current authoritative implementation plan, phase scope, or
verification status.

## Critical Missing Or Deferred Features

- Release artifact matrix for first install and in-field upgrade flows,
  including UF2 images, signed upload images, the host wheel, optional sdist,
  checksums, and release naming.
- Signing and key-management policy, including development keys, release keys,
  storage expectations, rotation, repository exclusion rules, and build-script
  behavior.
- Concrete MCUboot partition and swap design for each supported target,
  including slot sizes, bootloader size, trailer/status storage, and whether a
  scratch partition is required.
- Firmware upgrade state model exposed consistently through device info,
  status, host API responses, CLI human output, and CLI JSON output.
- Recovery procedure for failed updates, including UF2 bootloader recovery,
  SWD/debug recovery, bad key recovery, failed upload cleanup, and operator
  checks for current slot/version.
- End-to-end upgrade tests for valid upload, interrupted upload or power loss,
  invalid signature rejection, no-confirm rollback, unhealthy-image rollback,
  and relay default-off behavior after each boot path.
- Ownership decision for optional buzzer and WS2812 RGB LED status outputs,
  which are listed as planned but are not assigned to a remaining phase.

## Ambiguities To Resolve Before Implementation

- Whether Phase 7 and Phase 8 apply only to the Waveshare board targets or also
  to Pico 2 and Pico 2 W DIY relay targets.
- Which firmware artifact format is uploaded by the host workflow: signed
  binary, unsigned binary plus signing step, HEX, UF2, or multiple formats.
- Whether image confirmation is fully automatic after firmware health checks,
  host-triggered through CLI/API, or supports both modes.
- Whether Zephyr standard MCUmgr image management is exposed on the same USB CDC
  SMP route alongside the custom relay management group, and how host errors
  from that standard group map into existing typed exceptions.
- How upload interruption is handled by the host library: restart-only,
  resumable upload, cleanup before retry, progress reporting, chunk sizing,
  MTU handling, timeout, and retry behavior.
- How the documentation separates factory or first-install UF2 flashing from
  in-field firmware upgrade after MCUboot support exists.

## Source Context

- [Implementation plan](implementation-plan.md) currently defines Phase 7 as
  firmware upgrade foundation and Phase 8 as host firmware upload and rollback.
- [Product requirements](prd.md) require A/B firmware upgrade, signed images,
  inactive-slot upload, test boot, confirmation, rollback, and minimum health
  checks before confirmation.
- [README](../README.md) lists firmware update support, host image upload
  workflows, release helper scripts, and optional status outputs as planned.
- [CLI documentation](cli.md) currently documents release UF2 flashing and wheel
  installation, but not in-field upload commands.
