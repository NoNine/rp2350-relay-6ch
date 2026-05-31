# Indicator API design

Date: 2026-05-23

Status: Discussion. This note records the Phase 7 local-indicator API design
reasoning. It does not change the authoritative PRD, implementation plan,
phase scope, or verification status unless those documents are updated
explicitly.

## Summary

Phase 7 should expose typed, domain-specific firmware APIs for the RGB LED and
buzzer indicator module. Relay control, management RPC, and startup code should
publish facts and events they already own; the indicator module should map
those inputs to logical indicator conditions, resolve priority, handle timing,
and perform hardware I/O.

The design borrows OpenBMC's useful logical-group priority idea, but not its
generic group assertion API. The firmware has a small set of trusted in-process
producers, so a generic group registry, string IDs, runtime configuration
layer, or D-Bus-like control surface would add more misuse surface than value
for v1.

## Planned firmware API

```c
indicator_init();
indicator_set_ready(bool ready);
indicator_set_relay_state(uint8_t state_mask, uint8_t pulse_mask);
indicator_record_command(enum indicator_command_result result);
indicator_set_degraded(bool degraded);
indicator_set_fault(bool fault);
indicator_set_reboot_pending(bool pending);
```

The command result enum should represent product outcomes rather than LED
patterns, for example accepted, rejected, invalid, and busy.

## Why typed APIs

The design is intentionally typed and domain-specific, not a generic
OpenBMC-style group API. The current API still treats indicator states as
logical conditions, but each condition is asserted through the product-domain
call that owns the relevant fact.

This keeps ownership clear:

- `main` publishes readiness and startup faults.
- `relay.c` publishes successful relay and pulse state changes.
- `relay_mgmt.c` publishes command outcomes and reboot-pending state.
- The indicator module owns condition mapping, priority resolution, transient
  timing, RGB output, buzzer output, and device-error isolation.

Persistent conditions and transient events are different. Ready, degraded,
fault, reboot-pending, and relay-active state are conditions that remain true
until cleared or replaced. Command acceptance, invalid requests, and busy pulse
rejections are one-shot events that should produce bounded feedback and then
expire. Separate APIs make that distinction visible in code review and tests.

Typed APIs also reduce misuse. A generic call such as
`indicator_set_condition(id, value)` would let any producer assert any
indicator group. Domain-specific calls make incorrect ownership easier to spot:
`relay.c` should not assert command-invalid feedback, and `relay_mgmt.c` should
not directly choose a relay-active color.

Tests should read in product terms rather than presentation terms. For
example, tests can assert that `indicator_set_relay_state(0x01, 0x00)` renders
relay-active, or that
`indicator_record_command(INDICATOR_COMMAND_BUSY)` creates bounded attention
feedback. They should not need to set arbitrary group names or know RGB colors
outside the indicator module's expected output mapping.

## External design references

OpenBMC's `phosphor-led-manager` exposes logical LED groups through D-Bus and
resolves asserted groups by priority before writing physical LEDs. That model
supports the Phase 7 priority resolver: fault, reboot/update, degraded or
attention, command accepted, relay-active, ready, booting, and off. OpenBMC's
physical LED error paths also support the Phase 7 rule that indicator hardware
write failures should be logged or rate-limited without changing authoritative
controller state.

The OpenBMC API is intentionally generic because many system services can
assert named LED groups and platform JSON config maps those groups to physical
LED behavior. This relay firmware does not need that generality in v1. A
small typed API gives the same separation between logical conditions and
physical output without adding runtime group configuration.

Meshtastic's status LED and external notification modules reinforce the same
implementation boundary from the embedded side: indicator behavior should be
feature-gated, rendered from scheduled work, and time-bounded. Its buzzer mode
checks also support keeping buzzer feedback quiet by default unless explicitly
enabled.

References:

- <https://github.com/openbmc/phosphor-led-manager>
- <https://github.com/meshtastic/firmware>

## Recommended design

- Keep the Phase 7 public indicator API typed and limited to product-domain
  facts and events.
- Keep all LED color, blink, pulse, and buzzer timing decisions inside the
  indicator module.
- Resolve the effective RGB state from persistent conditions and active
  transient events using the documented priority order.
- Keep buzzer feedback behind build-time configuration by default, with bounded
  beep sequences when enabled.
- Do not change the host RPC wire format, host library API, or relay command
  semantics for local indicators.

## Later health-model refinement

The compact health model added after Phase 7 refines stable-state ownership.
The typed indicator APIs above describe the pre-health implementation boundary:
domain code publishes facts directly to the indicator, and the indicator maps
those facts to local output.

Once the health module is implemented, stable controller facts should flow first
to health. In MVC terms, health is the model, relay/startup/management code are
controllers, and the indicator is a view over `health_snapshot`. The indicator
still owns presentation behavior, transient command feedback, timing, rendering,
and hardware I/O isolation, but it should no longer own the authoritative
ready/degraded/fault/reboot/relay-active model.

## Assumptions

- `docs/phase-7-plan.md` remains the authoritative Phase 7 implementation
  plan.
- `docs/status-indicators.md` remains operator-facing and should change only
  when visible RGB LED or buzzer behavior changes.
- A generic indicator condition API can be reconsidered later only if future
  phases add enough independent indicator producers to justify the additional
  abstraction.
