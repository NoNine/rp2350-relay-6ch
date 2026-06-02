# Relay Management Protocol

## Status

The relay management protocol is a Zephyr MCUmgr/SMP application-specific
management group. SMP provides the packet envelope, operation, group ID,
command ID, sequence number, and response matching. Relay payloads are CBOR maps
encoded and decoded with Zephyr zcbor helpers.

Phase 4 exposes the group over USB CDC ACM using Zephyr's SMP UART transport.
Firmware tests continue to exercise the management group handlers on
`native_sim` without requiring USB hardware.

## SMP Group

- Group ID: `64`
- Protocol version: `7`
- Command model version: `2`
- Hardware name: `Waveshare RP2350-Relay-6CH`
- Relay channel indexes: zero-based, `0` through `5`
- Relay state masks: bit `0` is `CH1`, bit `5` is `CH6`

## Commands

| ID | Name | SMP op | Purpose |
| ---: | --- | --- | --- |
| 0x00 | `identity` | Read | Return protocol, command model, hardware, and relay-count identity. |
| 0x01 | `capabilities` | Read | Return operation capabilities and pulse bounds. |
| 0x02 | `build_info` | Read | Return firmware build identity and traceability metadata. |
| 0x10 | `get` | Read | Return all relay states, or one relay when `channel` is provided. |
| 0x11 | `status` | Read | Return concise operator relay state, health, and uptime. |
| 0x12 | `health` | Read | Return detailed health state and reasons. |
| 0x13 | `transport` | Read | Return transport configuration and status. |
| 0x14 | `safety` | Read | Return communication-loss safety policy. |
| 0x15 | `watchdog` | Read | Return watchdog configuration and health. |
| 0x20 | `set` | Write | Set one relay on or off. |
| 0x21 | `set_all` | Write | Set all relay states from one mask. |
| 0x22 | `pulse` | Write | Pulse one relay for a bounded duration. |
| 0x23 | `off_all` | Write | Cancel pulses and turn every relay off. |
| 0x30 | `heartbeat` | Write | Renew the communication-loss lease and return a liveness acknowledgement. |
| 0x40 | `reboot` | Write | Request controlled reboot when Zephyr reboot support is enabled. |
| 0x7f | `event` | Device-originated `Write Response` | Reserved best-effort asynchronous event frame. |

### Role Command Model

| Role | IDs | Commands | Host-callable | Purpose |
| --- | --- | --- | --- | --- |
| Identity | `0x00`-`0x02` | `identity`, `capabilities`, `build_info` | Yes | Describe the device, supported operations, and firmware build without reading or changing relay state. |
| Observation | `0x10`-`0x15` | `get`, `status`, `health`, `transport`, `safety`, `watchdog` | Yes | Read relay state and role-specific operational details without claiming ownership or changing outputs. |
| Control | `0x20`-`0x23` | `set`, `set_all`, `pulse`, `off_all` | Yes | Change relay outputs or cancel active pulse work. |
| Ownership | `0x30` | `heartbeat` | Yes | Renew host ownership and the communication-loss lease without changing relay outputs. |
| Maintenance | `0x40` | `reboot` | Yes | Request a controlled firmware reboot when reboot support is enabled. |
| Reserved event | `0x7f` | `event` | No | Reserve a device-originated asynchronous event frame; hosts must not send requests to this command. |

Protocol version `7` groups command IDs by command-model role: identity,
observation, control, ownership, and maintenance. Control commands keep their
original CBOR request and response definitions; only their wire command IDs
move into the control range. Hosts must treat `event` as unavailable and use
normal command responses and reconnect/status checks.

## Error Codes

Relay group errors are returned with Zephyr SMP v2 group error payloads:

```cbor
{
  "err": {
    "group": 64,
    "rc": <relay-error-code>
  }
}
```

| Code | Name | Meaning |
| ---: | --- | --- |
| 0 | `ok` | Success. |
| 1 | `decode` | CBOR payload is malformed or has an invalid field type. |
| 2 | `invalid_argument` | Channel, state mask, duration, or required field is invalid. |
| 3 | `busy` | Relay is already running a pulse. |
| 4 | `relay_io` | Relay GPIO operation failed. |
| 5 | `reboot_unavailable` | Reboot command was requested without Zephyr reboot support. |
| 6 | `reboot_failed` | Reboot was supported but could not be scheduled or complete safely. |

## Request And Response Fields

### `identity`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `protocol_version` | uint | Relay protocol version. |
| `command_model_version` | uint | Protocol-independent command model version. |
| `hardware` | text | Hardware name. |
| `relay_count` | uint | Number of relays, currently `6`. |

### `capabilities`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `pulse_min_ms` | uint | Minimum pulse duration. |
| `pulse_max_ms` | uint | Maximum pulse duration. |
| `capabilities` | uint | Capability bit mask for get, set, set-all, pulse, and off-all. |

### `build_info`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `app_version` | text | Application version configured by the firmware build. |
| `zephyr_version` | text | Zephyr kernel version string available at build time. |
| `board` | text | Zephyr board target used for the build. |
| `git_commit` | text | Short source commit hash, or `unknown` when unavailable. |
| `git_dirty` | bool | Whether the source tree had uncommitted changes at CMake configure time. |
| `build_timestamp` | text | Shanghai-time ISO-8601 timestamp with `+08:00` offset from `SOURCE_DATE_EPOCH` when set, otherwise the current Git commit time when available, otherwise CMake configure time. |
| `compiler` | text | C compiler ID and version used by CMake. |

### `get`

Request:

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `channel` | uint | No | Relay channel `0` through `5`; omit to get all relays. |

All-relay response:

| Field | Type | Meaning |
| --- | --- | --- |
| `state` | uint | Current on/off relay state mask. |
| `pulsing` | uint | Current pulse-active relay mask. |

Single-relay response:

| Field | Type | Meaning |
| --- | --- | --- |
| `channel` | uint | Relay channel. |
| `on` | bool | Current relay state. |
| `pulsing` | bool | Whether a pulse is active on this relay. |

### `set`

Request:

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `channel` | uint | Yes | Relay channel `0` through `5`. |
| `on` | bool | Yes | Desired relay state. |

Response: all-relay `state` and `pulsing` masks.

### `set_all`

Request:

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `state` | uint | Yes | Six-bit relay state mask. Bits outside `0x3f` are invalid. |

Response: all-relay `state` and `pulsing` masks.

### `pulse`

Request:

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `channel` | uint | Yes | Relay channel `0` through `5`. |
| `duration_ms` | uint | Yes | Pulse duration in milliseconds. |

Response: all-relay `state` and `pulsing` masks.

### `off_all`

Request: empty CBOR map.

Response: all-relay `state` and `pulsing` masks.

### `status`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `state` | uint | Current relay state mask. |
| `pulsing` | uint | Current pulse-active relay mask. |
| `health` | text | Health state name. |
| `health_reasons` | uint | Health reason bit mask. |
| `health_primary_reason` | text | Primary health reason name. |
| `health_transitions` | uint | Health state transition count. |
| `uptime_ms` | uint | Zephyr uptime in milliseconds. |

`status` is an operator overview. It intentionally excludes transport,
safety-policy, watchdog, and command-counter fields; use the role-specific
read commands below for those details.

### `health`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `health` | text | Health state name. |
| `health_reasons` | uint | Health reason bit mask. |
| `health_primary_reason` | text | Primary health reason name. |
| `health_transitions` | uint | Health state transition count. |

### `transport`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `transport` | text | Transport name, currently `usb_cdc_acm_smp`. |
| `usb_cdc_acm` | bool | Whether USB CDC ACM serial support is compiled in. |
| `smp_uart` | bool | Whether Zephyr's SMP UART transport is compiled in. |

### `safety`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `comm_loss_policy` | text | Communication-loss policy string. |
| `comm_loss_timeout_ms` | uint | Firmware communication-loss all-off timeout. `0` means no firmware timeout. |
| `comm_loss_reboot_on_timeout` | bool | Whether the active communication-loss policy schedules firmware reboot after timeout. |
| `comm_loss_reboot_delay_ms` | uint | Delay from communication-loss all-off to autonomous reboot. `0` means no autonomous reboot is scheduled. |

Policy strings are `energized-only`, `no-comm-timeout`, and
`always-on-owner`.

### `watchdog`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `watchdog_enabled` | bool | Whether watchdog supervision is active. |
| `watchdog_healthy` | bool | Whether watchdog supervision is currently healthy. |
| `watchdog_timeout_ms` | uint | Configured watchdog timeout. |
| `watchdog_feed_interval_ms` | uint | Configured watchdog feed interval. |
| `watchdog_feeds` | uint | Successful watchdog feed count. |
| `watchdog_feed_errors` | uint | Watchdog feed error count. |
| `last_reset_watchdog` | bool | Whether the previous reset was watchdog-caused. |

### `reboot`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `ok` | bool | Present and true when reboot was scheduled. |

If Zephyr reboot support is not enabled, the command returns
`reboot_unavailable`. If reboot support is enabled but the reboot cannot be
scheduled before acceptance, the command returns `reboot_failed`. When reboot
support is enabled and scheduling succeeds, firmware shows reboot-pending local
indication immediately and performs the cold reboot after 1000 ms. If the
accepted reboot later cannot turn all relays off or the reboot call returns
unexpectedly, status reports `health="fault"` with `reboot_failed`.

### `heartbeat`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `ok` | bool | Present and true when the heartbeat request was accepted. |

In protocol `7`, `heartbeat` renews the firmware communication-loss lease when
the active policy uses a timeout. It does not change relay outputs directly,
emit events, expose heartbeat health state, or persist relay state.

## Communication-Loss Safety

Protocol `7` defines build-time communication-loss policies:

- `energized-only`: the timeout is armed or renewed by successful relay-control
  commands that leave any relay on or pulsing, and by `heartbeat` while relays
  are energized. When all relays are off, the timeout is disarmed. The standard
  product profile uses this policy with `comm_loss_timeout_ms=5000`.
- `no-comm-timeout`: firmware does not force relay state off because of
  communication loss. `comm_loss_timeout_ms` is reported as `0`.
- `always-on-owner`: the timeout starts at boot, renews on heartbeat and
  successful relay-control commands, and does not disarm just because all
  relays are off. Timeout latches a local owner-lost attention indication until
  a later successful heartbeat or relay-control command proves the owner is
  back.

When `comm_loss_timeout_ms` expires, firmware cancels active pulses, turns all
relays off, and updates local indicators. Builds with
`comm_loss_reboot_on_timeout=true` schedule a cold firmware reboot after
`comm_loss_reboot_delay_ms`. During that delay firmware first shows owner-lost
attention indication, then switches to reboot-pending indication for the final
10 seconds before autonomous reboot. A successful heartbeat or relay-control
command during that delay restores ownership and cancels the pending autonomous
reboot. Firmware does not persist state, emit events, write audit logs, or
imply mains-power SmartPDU behavior.

The `reboot` command is host-initiated maintenance, not a communication-loss
recovery action. It uses its own short controlled-reboot pending indication and
is not delayed by `comm_loss_reboot_delay_ms`.

### Planned `event`

`event` is reserved for future best-effort device-originated notifications. It
is not a host request command, and hosts must not send `Read` or `Write`
requests to command ID `0x7f`.

Event frame convention:

| SMP field | Value |
| --- | --- |
| Group | `64` |
| Command ID | `0x7f` |
| Operation | `Write Response` |
| Sequence | `0xff` |
| Payload | CBOR map |

Initial event payload:

| Field | Type | Meaning |
| --- | --- | --- |
| `event` | text | Event name, initially `reset_executing`. |
| `reason` | text | Event reason, initially `reboot_command`. |
| `uptime_ms` | uint | Best-effort Zephyr uptime when the event was emitted. |

Event delivery is advisory. Events may be missed because of USB reset, host
serial buffering, transport errors, or older firmware. Hosts must confirm
critical state through normal request/response commands such as `status` after
reconnect. The first planned event, `reset_executing`, means firmware is about
to perform the scheduled reset; it does not replace post-reboot freshness
validation.

## Relay Pulse Bounds

Pulse commands must use a duration from `10` ms through `60000` ms inclusive.
Firmware rejects pulse durations outside that range with an invalid-argument
error.

Pulse behavior:

- Relay channel indexes are zero-based in firmware internals.
- A valid pulse turns the selected relay on immediately.
- The relay turns off when the requested duration expires.
- Relay state remains queryable during and after a pulse.
- A second pulse request for a relay that is already pulsing is rejected as
  busy.
- `off all` behavior cancels any active pulses and leaves all relays off.
