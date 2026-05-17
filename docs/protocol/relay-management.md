# Relay Management Protocol

## Status

Phase 3 defines the relay management protocol as a Zephyr MCUmgr/SMP
application-specific management group. SMP provides the packet envelope,
operation, group ID, command ID, sequence number, and response matching. Relay
payloads are CBOR maps encoded and decoded with Zephyr zcbor helpers.

USB CDC transport is Phase 4 scope. Phase 3 tests exercise the management group
handlers on `native_sim` without adding a host transport.

## SMP Group

- Group ID: `64`
- Protocol version: `1`
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
| `firmware_version` | text | Zephyr firmware version string available at build time. |
| `hardware` | text | Hardware name. |
| `relay_count` | uint | Number of relays, currently `6`. |
| `pulse_min_ms` | uint | Minimum pulse duration. |
| `pulse_max_ms` | uint | Maximum pulse duration. |
| `capabilities` | uint | Capability bit mask for get, set, set-all, pulse, and off-all. |

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
