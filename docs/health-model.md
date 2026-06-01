# Compact Health Model Contract

Date: 2026-06-01

Status: Standalone implementation contract. This compact health model is not
assigned to an implementation phase. Do not update completed phase plans, the
PRD, protocol docs, host docs, release docs, or phase verification reports for
this contract unless the user explicitly requests that broader promotion.

Design rationale lives in
[Health models in embedded control systems](discussions/health-models-embedded-control-systems.md)
and [Hardware watchdogs in MCU-based embedded systems](discussions/hardware-watchdogs-mcu-systems.md).
This document is the concise implementer-facing contract.

## Summary

The compact health model centralizes controller health as application-owned
firmware state. It records observable facts, derives a small overall state and
reason flags, exposes that state through the existing `status` command, and
lets local indicators derive their stable summary from the same source.
Indicator code may keep presentation-only state such as command flash windows,
buzzer sequencing, display write status, and pulse display timing, but it must
not maintain a second authoritative controller-health model.

This first contract is status-only. It does not enable hardware watchdog
feeding, Zephyr task watchdog, MCUboot image confirmation, new relay recovery
actions, persistent fault history, zbus, Zephyr statistics, or measured load
health.

High-level architecture:

```text
firmware fact publishers
main.c / relay.c / relay_mgmt.c
        |
        v
app-owned health module
overall state + reason flags + relay masks + counters
        |
        +--> relay_mgmt status response
        +--> indicator stable summary
        +--> future watchdog and MCUboot gates
```

## Model/View/Controller Ownership

Use MVC as an ownership analogy for this contract:

- Model: the `health` module owns stable controller truth, including overall
  state, reason flags, relay masks, readiness facts, failure facts, recovery
  pending facts, and transition count.
- Controllers: `main.c`, `relay.c`, and `relay_mgmt.c` interpret boot progress,
  relay state changes, host commands, communication-loss deadlines, and reboot
  requests, then publish facts to `health`.
- Views: the `status` response and local indicators consume `health_snapshot`.
  They format or render the model; they do not compute a second stable health
  model.

The local indicator view may keep view-local state for rendering mechanics:
accepted/rejected/busy flash windows, buzzer sequencing, OLED POST/write status,
display pulse timing, hardware availability, and last rendered output. Those
fields must not become another source of controller-health truth.

The health module is passive. It must not call consumers, publish zbus events,
write indicators, encode CBOR, drive GPIOs, schedule reboots, feed watchdogs,
or perform other side effects. Controllers publish facts to health
synchronously, and views request snapshots when they need to format or render
state. This keeps relay-control paths bounded and leaves a future event or bus
layer optional instead of built into the first contract.

## Firmware Contract

Add an application-owned `health` module under the firmware app. The module
owns health state, reason flags, transition counting, and snapshots. Other
modules publish facts into it; they do not compute their own public health
state. Local indicators consume the health-derived stable summary and layer
local presentation transients on top.

Overall health states:

- `booting`: initialization is still in progress.
- `normal`: controller is ready, no relay is commanded on, and no attention
  reason is active.
- `relay_active`: one or more relays are commanded on or pulsing.
- `degraded`: controller is usable but needs attention.
- `fault`: required health failed; affected operations are blocked or safe
  action is latched.
- `recovery_pending`: host-requested controlled reboot or autonomous
  communication-loss recovery is scheduled or in progress. Future boot/update
  rollback can map here when that domain is promoted.

First-cut domains:

- `relay_gpio`: relay GPIO subsystem initialized and commanded state is known.
- `rpc`: relay management service is registered/initialized and able to serve
  management commands. Status is the minimum observable proof of readiness.
- `comm_owner`: host ownership or heartbeat freshness for profiles that enable
  communication-loss supervision.
- `indicator`: explicitly supported local LED, buzzer, or display health. This
  domain is advisory and must not block relay behavior. Unsupported, disabled,
  or absent optional indicators are not degraded.

Reserved future domains:

- `boot_update`: boot slot, image state, and confirmation readiness.
- `watchdog_supervisor`: workers or checkpoints are fresh enough to feed a
  hardware watchdog.

If watchdog support is later promoted, start with a central health-gated feed
path using Zephyr's public hardware watchdog driver API (`wdt_*`). Defer Zephyr
task watchdog (`task_wdt_*`) until firmware has multiple independent
long-running workers or checkpoints that need separate liveness channels. This
contract remains status-only and does not enable watchdog configuration, feed
gating, or task watchdog supervision.

Reason flags:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `relay_gpio_init_failed` | Relay GPIO initialization failed. |
| 1 | `relay_io_failed` | Relay GPIO operation failed after initialization. |
| 2 | `rpc_not_ready` | Relay management service is not ready. |
| 3 | `comm_owner_timeout` | Communication-loss owner deadline expired. |
| 4 | `comm_reboot_pending` | Autonomous communication-loss reboot final warning is active. |
| 5 | `indicator_degraded` | Explicitly supported local indicator path is degraded. |
| 6 | `host_reboot_pending` | Host-requested controlled reboot is pending. |
| 7 | `reboot_failed` | Accepted host or autonomous reboot could not be scheduled or complete safely. |

Reason flags are stable once exposed. Add new flags only by appending new bits.
Do not reuse or renumber existing bits.

## Module Responsibilities

`main.c`:

- Calls `health_init()` before subsystem initialization starts.
- Leaves health in `booting` until relay initialization succeeds.
- Calls `health_set_relay_gpio_ready(true)` after `relay_init()` succeeds.
- Calls `health_record_relay_gpio_init_failed()` if relay initialization fails.
  `health_set_relay_gpio_ready(false)` means relay GPIO is not ready or
  initialization is incomplete; it must not set `relay_gpio_init_failed`.

`relay.c`:

- Publishes relay state and pulse masks so health can derive `relay_active`.
- Updates health synchronously before reporting a successful relay-control,
  pulse, communication-loss recovery-cancel, or ownership-recovery action to
  its caller. A successful relay mutation is not complete until the commanded
  relay masks and health facts have both been updated.
- Records `relay_io_failed` when GPIO operations fail.
- Publishes `comm_owner_timeout` when communication-loss timeout expires.
- Clears `comm_owner_timeout` when a successful heartbeat or relay-control
  command restores ownership.
- If autonomous communication-loss reboot is scheduled after owner timeout,
  leaves health in `comm_owner_timeout` during the owner-loss attention phase.
- Publishes `comm_reboot_pending` when the final reboot-pending warning phase
  starts. If the configured reboot delay is shorter than or equal to that
  warning window, this happens immediately.
- Clears `comm_reboot_pending` if ownership recovery cancels the pending
  reboot.
- Records latched `reboot_failed` and clears communication-loss reboot
  bookkeeping if autonomous reboot cannot be scheduled or if a reboot call
  unexpectedly returns.

`relay_mgmt.c`:

- Records when the relay management service is registered/initialized and able
  to serve management commands, then publishes `health_set_rpc_ready(true)`
  after `health_init()` has completed.
- Publishes `host_reboot_pending` when host-requested reboot is accepted, and
  clears it if the pending reboot is canceled in tests before reboot occurs.
- Records latched `reboot_failed` and clears reboot-pending health reasons if
  host-requested reboot cannot be scheduled, pre-reboot all-off fails, or a
  reboot call unexpectedly returns.
- Extends `status` with health fields.
- Keeps existing command counters as counters, not health reasons, unless a
  later contract defines thresholds.

`indicator.c`:

- Consumes stable controller summary through a health snapshot adapter such as
  `indicator_set_health_snapshot(const struct health_snapshot *snapshot)`.
- Copies health snapshot data and schedules existing asynchronous render work.
  It must not block relay-control paths on display writes, buzzer sequencing,
  hardware probing, or other indicator-side work.
- Uses health state priority and primary reason for stable RGB, buzzer, and
  display mode decisions. It must not independently re-derive ready, relay
  active, degraded, fault, owner-lost, or reboot-pending state from duplicated
  booleans.
- May keep presentation-only state for accepted/rejected/busy command attention,
  buzzer sequencing, display backend availability, OLED POST/write status,
  display pulse timing, and last rendered output.
- Remains non-authoritative. Indicator failure or absence must not block relay
  initialization, relay control, `off-all`, pulse teardown, reboot handling, or
  RPC responses.
- Does not report unsupported, disabled, absent, or merely not-detected optional
  indicators as degraded. OLED failures follow the standalone OLED contract and
  must not set `indicator_degraded` until a later explicit policy promotes OLED
  health into controller health.
- Command attention is presentation-only. It may temporarily change local
  indicator output only when no higher-priority health state is active, and it
  must not set health reason flags.

## Internal API

Expose a small internal firmware API:

```c
enum health_state {
	HEALTH_BOOTING,
	HEALTH_NORMAL,
	HEALTH_RELAY_ACTIVE,
	HEALTH_DEGRADED,
	HEALTH_FAULT,
	HEALTH_RECOVERY_PENDING,
};

enum health_reason {
	HEALTH_REASON_NONE = 0,
	HEALTH_REASON_RELAY_GPIO_INIT_FAILED = BIT(0),
	HEALTH_REASON_RELAY_IO_FAILED = BIT(1),
	HEALTH_REASON_RPC_NOT_READY = BIT(2),
	HEALTH_REASON_COMM_OWNER_TIMEOUT = BIT(3),
	HEALTH_REASON_COMM_REBOOT_PENDING = BIT(4),
	HEALTH_REASON_INDICATOR_DEGRADED = BIT(5),
	HEALTH_REASON_HOST_REBOOT_PENDING = BIT(6),
};

struct health_snapshot {
	enum health_state state;
	uint32_t reasons;
	enum health_reason primary_reason;
	uint8_t relay_state_mask;
	uint8_t pulse_mask;
	uint32_t transitions;
};
```

Required functions:

```c
void health_init(void);
void health_set_relay_gpio_ready(bool ready);
void health_set_rpc_ready(bool ready);
void health_set_relay_state(uint8_t state_mask, uint8_t pulse_mask);
void health_set_comm_owner_timed_out(bool timed_out);
void health_set_comm_reboot_pending(bool pending);
void health_set_host_reboot_pending(bool pending);
void health_set_indicator_degraded(bool degraded);
void health_record_relay_gpio_init_failed(void);
void health_record_relay_io_error(void);
void health_snapshot(struct health_snapshot *snapshot);
const char *health_state_name(enum health_state state);
const char *health_reason_name(enum health_reason reason);
```

The health module must be thread-safe for calls from work handlers and SMP
management handlers. Use a lightweight lock or atomics consistently; do not
require callers to hold unrelated relay or indicator locks.

Callers must not hold the health lock while taking relay or indicator locks,
and health must not require callers to hold those locks. Health setters may
derive state and increment transition counters, but they must remain in-memory
fact updates only.

Indicator integration should prefer one stable-summary entry point:

```c
void indicator_set_health_snapshot(const struct health_snapshot *snapshot);
```

Keep `indicator_record_command()` for short accepted, rejected, and busy
feedback because those are local presentation events, not controller health.
Relay pulse timing details used for OLED progress rendering may remain an
indicator-specific input; the authoritative pulse-active mask still comes from
the health snapshot.

## Status Response Contract

Extend the existing `status` response with additive fields only:

| Field | Type | Meaning |
| --- | --- | --- |
| `health` | text | Overall health state name. |
| `health_reasons` | uint | Active reason bitmask. |
| `health_primary_reason` | text | Highest-priority active reason, or `none`. |
| `health_transitions` | uint | Count of overall health-state changes since boot. |

Encode `state`, `pulsing`, and the health fields from one
`health_snapshot`. Do not add a runtime relay-vs-health mismatch fault in this
contract; the health snapshot is the status source of truth for commanded relay
masks. Publication correctness belongs in firmware tests that cover each relay
mutation path.

A successful relay-control, heartbeat-recovery, communication-loss
recovery-cancel, or reboot-scheduling command must update relevant health facts
before returning success. The next processed `status` command must observe the
updated health snapshot. Local indicator rendering may still be deferred.

Do not bump `RP2350_RELAY_6CH_MGMT_PROTOCOL_VERSION` for this additive status
extension unless implementation reveals a strict compatibility problem. Existing
host library decoding returns response maps and should tolerate extra fields.
The existing status response already uses most of the configured Zephyr SMP CBOR
main-map entry budget. When implementing the health fields, raise
`CONFIG_MCUMGR_SMP_CBOR_MAX_MAIN_MAP_ENTRIES` in firmware and firmware tests
high enough for all existing status fields plus the four health fields.

Human CLI `status` output should include one compact line:

```text
Health: degraded (comm_owner_timeout)
```

JSON output must pass through the firmware fields unchanged.

## State Derivation Rules

Health state priority is:

1. `recovery_pending`
2. `fault`
3. `degraded`
4. `relay_active`
5. `normal`
6. `booting`

Use that priority after evaluating facts:

- If initialization is incomplete, state is `booting` unless a higher-priority
  reason is active.
- If relay GPIO initialization or relay I/O fails, state is `fault`.
- If communication-owner timeout is active, state is `degraded` until the
  final autonomous reboot warning phase starts. Clear the timeout when
  ownership recovery succeeds.
- If autonomous communication-loss reboot or host-requested reboot is pending,
  state is `recovery_pending`.
- If no attention or fault reason is active and any relay is on or pulsing,
  state is `relay_active`.
- If relay GPIO and RPC are ready, no attention/fault/recovery reason is
  active, and all relays are off, state is `normal`.
- Indicator degradation can make state `degraded`, but it must not block relay
  behavior.

Local indicator render priority follows health state priority for stable states.
Presentation-only command transients may affect the local indication only below
`fault`, `recovery_pending`, and active health `degraded` states. They must not
change the health snapshot or status response.

Primary reason priority is:

1. `comm_reboot_pending`
2. `host_reboot_pending`
3. `reboot_failed`
4. `relay_gpio_init_failed`
5. `relay_io_failed`
6. `comm_owner_timeout`
7. `rpc_not_ready`
8. `indicator_degraded`
9. `none`

Reason lifecycle:

- Latched reasons: `relay_gpio_init_failed`, `relay_io_failed`, and
  `reboot_failed`.
- Live reasons: `rpc_not_ready`, `comm_owner_timeout`,
  `comm_reboot_pending`, `indicator_degraded`, and `host_reboot_pending`.
- Live reasons are set and cleared by their owning domain. Latched reasons
  remain active until reboot or an explicit future contract defines a safe
  clearing policy.

## Non-Goals

This contract does not add:

- hardware watchdog configuration or feed gating;
- Zephyr task watchdog;
- MCUboot image confirmation or rollback policy;
- zbus, Zephyr statistics, or persistent settings;
- asynchronous device-originated events;
- network health;
- relay contact closure, load voltage, load current, mains behavior, or
  downstream equipment health claims.

Communication-loss behavior remains the existing relay safety policy. The
health model reports and summarizes that policy; it does not change the
timeout, off-all action, reboot delay, or ownership recovery behavior.

## Test Contract

Firmware health tests must cover:

- initial snapshot is `booting`;
- relay GPIO ready and RPC ready with all relays off becomes `normal`;
- relay on or pulse active becomes `relay_active`;
- snapshots include relay state and pulse masks used by status and indicators;
- relay GPIO initialization failure becomes `fault` with
  `relay_gpio_init_failed`;
- relay I/O failure becomes `fault` with `relay_io_failed`;
- reboot failure becomes `fault` with `reboot_failed` and clears reboot-pending
  reasons;
- communication-owner timeout becomes `degraded` with `comm_owner_timeout`;
- autonomous communication-loss reboot scheduling leaves owner timeout as
  `degraded` until the final warning phase starts;
- autonomous communication-loss reboot final warning becomes
  `recovery_pending` with `comm_reboot_pending`;
- ownership recovery clears `comm_owner_timeout` and `comm_reboot_pending`;
- host-requested reboot pending becomes `recovery_pending` with
  `host_reboot_pending`;
- communication-loss reboot pending outranks host reboot pending as the primary
  reason when both are active;
- unsupported, disabled, absent, or merely not-detected optional indicators do
  not set `indicator_degraded`;
- transition counter increments only when overall state changes.
- read-after-success consistency: after a successful relay mutation,
  heartbeat recovery, communication-loss recovery-cancel, or reboot scheduling,
  the next processed `status` sees the updated health snapshot;
- `status` uses one coherent `health_snapshot` for relay masks and health
  fields instead of checking a separate relay-vs-health mismatch at runtime.

Firmware indicator tests must cover:

- representative health snapshots render as booting, ready, relay-active,
  degraded, fault, and recovery-pending local indications;
- `indicator_set_health_snapshot()` copies state and schedules render work
  without performing synchronous display, buzzer, or hardware-probe work;
- accepted, rejected, and busy command transients do not override fault,
  recovery-pending, or active degraded health;
- indicator degraded remains advisory and does not block relay state changes;
- OLED absent or not detected remains local backend state and does not become
  health degraded.

Protocol and host tests must cover:

- `status` includes the four health fields and preserves all existing fields;
- existing direct library `get_status()` returns health fields without custom
  decoding;
- CLI JSON `status` preserves health fields unchanged;
- CLI human `status` prints the compact health line;
- daemon and session status flows tolerate the additive fields.

Hardware verification, when requested later, must include confirming that local
indicators match host-visible health summary without replacing host status as
the source of truth.
