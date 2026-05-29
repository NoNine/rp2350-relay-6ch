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
- Protocol version: `5`
- Hardware name: `Waveshare RP2350-Relay-6CH`
- Relay channel indexes: zero-based, `0` through `5`
- Relay state masks: bit `0` is `CH1`, bit `5` is `CH6`

## Commands

| ID | Name | SMP op | Purpose |
| ---: | --- | --- | --- |
| 0 | `info` | Read | Return protocol, firmware, hardware, and capability metadata. |
| 1 | `get` | Read | Return all relay states, or one relay when `channel` is provided. |
| 2 | `set` | Write | Set one relay on or off. |
| 3 | `set_all` | Write | Set all relay states from one mask. |
| 4 | `pulse` | Write | Pulse one relay for a bounded duration. |
| 5 | `off_all` | Write | Cancel pulses and turn every relay off. |
| 6 | `status` | Read | Return relay state, uptime, and protocol counters. |
| 7 | `reboot` | Write | Request controlled reboot when Zephyr reboot support is enabled. |
| 8 | `build_info` | Read | Return firmware build identity and traceability metadata. |
| 9 | `heartbeat` | Write | Renew the communication-loss lease and return a liveness acknowledgement. |
| 10 | `event` | Device-originated `Write Response` | Reserved best-effort asynchronous event frame. |

Protocol version `5` is request/response only and adds communication-loss
policy fields and lease behavior. Hosts must treat `event` as unavailable and
use normal command responses and reconnect/status checks.

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

## Request And Response Fields

### `info`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `protocol_version` | uint | Relay protocol version. |
| `hardware` | text | Hardware name. |
| `relay_count` | uint | Number of relays, currently `6`. |
| `pulse_min_ms` | uint | Minimum pulse duration. |
| `pulse_max_ms` | uint | Maximum pulse duration. |
| `comm_loss_policy` | text | Communication-loss policy string. |
| `comm_loss_timeout_ms` | uint | Firmware communication-loss timeout. `0` means no firmware timeout. |
| `capabilities` | uint | Capability bit mask for get, set, set-all, pulse, and off-all. |

Policy strings are `energized-only`, `no-comm-timeout`, and
`always-on-owner`.

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
| `transport` | text | Transport name, currently `usb_cdc_acm_smp`. |
| `usb_cdc_acm` | bool | Whether USB CDC ACM serial support is compiled in. |
| `smp_uart` | bool | Whether Zephyr's SMP UART transport is compiled in. |
| `comm_loss_policy` | text | Communication-loss policy string. |
| `comm_loss_timeout_ms` | uint | Firmware communication-loss timeout. `0` means no firmware timeout. |
| `uptime_ms` | uint | Zephyr uptime in milliseconds. |
| `received` | uint | Commands received, including this status command. |
| `succeeded` | uint | Commands completed before this status response is encoded. |
| `decode_errors` | uint | Decode error count. |
| `invalid_args` | uint | Invalid argument error count. |
| `busy` | uint | Busy response count. |

### `reboot`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `ok` | bool | Present and true when reboot was scheduled. |

If Zephyr reboot support is not enabled, the command returns
`reboot_unavailable`.

### `heartbeat`

Request: empty CBOR map.

Response:

| Field | Type | Meaning |
| --- | --- | --- |
| `ok` | bool | Present and true when the heartbeat request was accepted. |
| `comm_loss_policy` | text | Communication-loss policy string. |
| `comm_loss_timeout_ms` | uint | Firmware communication-loss timeout. `0` means no firmware timeout. |

In protocol `5`, `heartbeat` renews the firmware communication-loss lease when
the active policy uses a timeout. It does not change relay outputs directly,
emit events, expose heartbeat health state, persist relay state, or schedule a
reboot.

## Communication-Loss Safety

Protocol `5` defines build-time communication-loss policies:

- `energized-only`: the timeout is armed or renewed by successful relay-control
  commands that leave any relay on or pulsing, and by `heartbeat` while relays
  are energized. When all relays are off, the timeout is disarmed. The standard
  product profile uses this policy with `comm_loss_timeout_ms=5000`.
- `no-comm-timeout`: firmware does not force relay state off because of
  communication loss. `comm_loss_timeout_ms` is reported as `0`.
- `always-on-owner`: the timeout starts at boot, renews on heartbeat and
  successful relay-control commands, and does not disarm just because all
  relays are off.

When a timeout expires, firmware cancels active pulses, turns all relays off,
and updates local indicators. It does not reboot, persist state, emit events,
write audit logs, or imply mains-power SmartPDU behavior.

### Planned `event`

`event` is reserved for future best-effort device-originated notifications. It
is not a host request command, and hosts must not send `Read` or `Write`
requests to command ID `10`.

Event frame convention:

| SMP field | Value |
| --- | --- |
| Group | `64` |
| Command ID | `10` |
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
