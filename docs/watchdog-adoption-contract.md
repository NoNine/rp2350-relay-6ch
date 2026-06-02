# Watchdog Adoption Contract

Date: 2026-06-01

Status: Standalone implementation contract. This runtime watchdog adoption is
not assigned to an implementation phase. Do not update completed phase plans,
phase verification reports, release docs, or host UX docs for this contract
unless the user explicitly requests that broader promotion.

## Summary

Adopt a runtime-only hardware watchdog for normal firmware builds. The first
implementation starts supervision only after runtime readiness is reached,
feeds the RP2350 hardware watchdog from one central supervisor, and stops
feeding only for fatal controller faults or watchdog supervisor failure.

Boot/update confirmation policy is future scope. Early boot supervision and
MCUboot image-confirmation supervision are intentionally not part of this first
runtime implementation.

## Key Changes

- Add a firmware watchdog supervisor module using Zephyr's public `wdt_*` API
  and the existing `watchdog0 = &wdt0` RP2350 device.
- Enable watchdog by default for normal firmware builds when the target
  provides a ready watchdog device.
- Start the watchdog only after relay GPIO and RPC readiness are published.
  Reserve early boot supervision for a later MCUboot/update contract.
- Configure defaults:
  - hardware timeout: `10000 ms`
  - supervisor feed interval: `2000 ms`
  - reset action: `WDT_FLAG_RESET_SOC`
  - setup option: `WDT_OPT_PAUSE_HALTED_BY_DBG`
- Feed only when fatal-gate checks pass:
  - relay GPIO init failure: do not feed
  - relay I/O failure: do not feed
  - watchdog setup/feed failure: record fault and do not claim healthy
    supervision
  - degraded states such as `comm_owner_timeout` or `indicator_degraded`: keep
    feeding
  - `comm_reboot_pending` / `host_reboot_pending`: keep feeding until the
    planned reboot occurs
- A watchdog reset during an accepted reboot is reported on the next boot only
  as watchdog recovery through `last_reset_watchdog`. Do not add persistent or
  retained reboot-attempt markers in this contract; live `reboot_failed` remains
  limited to failures detected before calling `sys_reboot()`.
- Keep communication-loss safety separate from watchdog behavior. Always-on
  owner timeout with reboot disabled remains `degraded`, forces relays off, and
  does not become a watchdog reset path.

## Public Interfaces

- Bump relay management protocol version from `5` to `6`.
- Append a new compact health reason bit: `watchdog_supervisor_failed`.
- Classify `watchdog_supervisor_failed` as `fault`.
- Extend the additive `status` response with watchdog fields:
  - `watchdog_enabled`: bool
  - `watchdog_healthy`: bool
  - `watchdog_timeout_ms`: uint
  - `watchdog_feed_interval_ms`: uint
  - `watchdog_feeds`: uint
  - `watchdog_feed_errors`: uint
  - `last_reset_watchdog`: bool, using Zephyr `hwinfo` reset cause where
    available
- Do not update host CLI/library formatting in the first implementation.
  Existing host code may pass through additive status fields.

## Implementation Notes

- Use a single supervisor work item or thread owned by firmware application
  code. Relay, indicator, RPC, and heartbeat paths must not feed the watchdog
  directly.
- The supervisor should snapshot health, decide whether feeding is allowed,
  call `wdt_feed()`, and increment counters.
- Watchdog setup failure should be logged and reflected through
  `watchdog_healthy=false` plus `watchdog_supervisor_failed`.
- Missing, disabled, absent, or not-detected optional OLED must not create
  `indicator_degraded` and must not block watchdog feeding.
- Do not introduce Zephyr task watchdog in this contract. Reserve it until
  there are multiple independent long-running workers or checkpoints.
- Do not add persistent fault history or flash writes for watchdog resets.

## Test Plan

- Firmware unit tests:
  - watchdog remains disabled/unstarted before runtime readiness
  - watchdog starts after relay GPIO and RPC readiness
  - normal, relay-active, degraded, owner-timeout, and reboot-pending states
    allow feed
  - relay GPIO init failure, relay I/O failure, and supervisor/feed failure
    block feed and set `watchdog_supervisor_failed` where applicable
  - status encodes all watchdog fields
  - protocol version reports `6`
- Build checks:
  - build Waveshare firmware with watchdog enabled by default
  - build relevant firmware tests without requiring real hardware watchdog
    reset
- Hardware/manual verification:
  - confirm normal boot leaves all relays off and status reports watchdog
    enabled/healthy
  - confirm debugger halt does not trigger watchdog reset when debug pause is
    active
  - optional fault-injection smoke check can intentionally stop feeding and
    verify reset plus `last_reset_watchdog=true`

## Assumptions

- Runtime watchdog is the implementation target; boot-time and MCUboot
  image-confirmation supervision are future scope.
- The RP2350 Zephyr watchdog driver supports one feed channel, no callback, no
  window mode, and SoC/CPU reset flags; this contract uses SoC reset.
- `hwinfo_clear_reset_cause()` is not required because the RP2350 hwinfo driver
  reports reset cause but does not clear it.
- No phase verification report is created unless explicitly requested.
