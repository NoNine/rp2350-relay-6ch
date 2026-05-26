# Device-originated SMP events

Date: 2026-05-26

Status: Discussion. This note records design reasoning for future
device-originated events over the relay controller's existing USB CDC SMP
serial route. It does not change the authoritative PRD, implementation plan,
protocol, host behavior, or verification status unless those documents are
updated explicitly.

Authoritative planned protocol details live in
[Relay Management Protocol](../protocol/relay-management.md#planned-event).
Host-facing planned behavior is summarized in
[Host Library](../host-library.md#planned-device-events) and
[Host Session Mode](../host-session-mode.md#reboot-and-reconnect).

## Conversation context

This discussion was prompted by a reboot reconnect race and by prior operator
experience with an equivalent hand-written asynchronous serial mechanism. That
prior implementation used the same broad architecture: one reader owned the byte
stream, expected replies were matched to pending requests, and unsolicited
device frames were routed to an event path. This note records that experience as
design context, not as an implemented feature or a binding protocol requirement.

## Summary

The current host and firmware architecture is request/response only. The host
sends one SMP request, waits for one matching SMP response, and treats anything
else as timeout, protocol error, or transport failure. That model is simple and
appropriate for direct relay commands, but it cannot represent a true
device-originated notification such as "reset is executing now".

More device-originated events are expected later, so it is reasonable to pay for
a host demux reader once. The demux reader would own the serial read side for
the lifetime of a connection, route normal responses back to synchronous
requests, and route unsolicited event frames to an event queue or callback.

The recommended direction is best-effort unsolicited SMP event frames over the
existing USB CDC SMP serial route. Critical state must still be confirmed by
normal commands such as `status`, especially after USB reset or reconnect.

## Current architecture

Today the direct host path is intentionally narrow:

- `RelayClient` builds one SMP packet per command.
- `SerialSmpTransport.exchange()` opens or reuses the serial port, writes the
  request frames, then reads lines until one encoded SMP response decodes.
- The transport resets the input buffer at the start of each exchange.
- The session heartbeat is just another synchronous command protected by a
  lock.
- Firmware command handlers run inside Zephyr MCUmgr/SMP and return responses to
  host requests.

That design has no persistent serial reader and no event dispatch surface. An
event emitted while the host is idle can be discarded by the next input-buffer
reset. An event emitted while a command is active can be mistaken for the
command's response unless the host can demultiplex frames.

## Reboot race context

The immediate motivating case is controlled reboot. Firmware replies to
`reboot` when the reboot is scheduled, not when reset has executed. The current
firmware also delays the actual reset long enough to show local reboot-pending
feedback. During that window the host may still be able to talk to the old boot.

The existing host-only reconnect fix uses `status.uptime_ms` to reject stale
post-reboot reconnect attempts. That is still the right confirmation mechanism.
An event adds operator visibility:

```text
reboot requested
reset executing
reconnected
```

For this event, the precise semantic is "firmware is about to execute reset",
because after `sys_reboot()` begins the device cannot guarantee more USB data
will be delivered.

## Event frame concept

Zephyr SMP does not define an event opcode. The existing op space is read,
read response, write, and write response. A relay event therefore has to be a
project-level convention carried inside normal SMP serial framing.

The planned convention is:

- Relay group ID `64`.
- Reserved command ID `10`, named `event`.
- Operation `Write Response`.
- Reserved sequence number `0xff`.
- CBOR map payload.

The initial planned payload is:

```cbor
{
  "event": "reset_executing",
  "reason": "reboot_command",
  "uptime_ms": <zephyr-uptime>
}
```

The reserved command ID and sequence number are important. They let the host
classify event frames without completing any pending request. Hosts must not
send `Read` or `Write` requests to the event command ID.

## Host demux reader

The host transport would change from "read inside each exchange" to "one reader
owns the serial port". The public synchronous command API can remain compatible,
but internally requests wait for a response routed by the reader.

The reader loop would:

- Decode SMP serial frames continuously.
- Decode each SMP packet header and CBOR payload.
- Classify `GROUP_RELAY`, `CMD_EVENT`, `OP_WRITE_RSP`, `seq=0xff` packets as
  events.
- Match normal responses to pending requests by sequence, group, command, and
  expected response operation.
- Drop or log unknown unsolicited frames without completing a request.
- Wake all pending requests with a transport error when the serial link fails.

A request path would:

- Allocate the next request sequence.
- Register a pending response waiter before writing request frames.
- Write all serial frames for the request.
- Wait until the reader delivers the matching response or the timeout expires.
- Remove the pending waiter on completion, timeout, or transport close.

This design avoids two readers racing over the same serial file descriptor. It
also removes routine input-buffer resets, because resets are incompatible with
receiving events while idle.

## Event API shape

The host library should expose events first. Session mode, future daemon mode,
and direct Python consumers can then share one transport foundation.

The minimum useful API shape is:

- A typed event object containing `name`, `payload`, and optionally the decoded
  SMP metadata.
- A nonblocking drain method or event queue for consumers that poll.
- Optional callback registration for long-lived consumers such as session or
  daemon mode.
- A clear close behavior: closing the transport stops the reader and wakes
  pending request waiters.

Synchronous `RelayClient` methods such as `get_status()`, `set_relay()`, and
`reboot()` should continue to return decoded response dictionaries. Event-aware
consumers opt into the event surface.

## Firmware emitter shape

The normal MCUmgr handler path responds to requests; it does not provide a
ready-made application API for unsolicited frames. Firmware therefore needs a
small event emitter helper that can build and transmit an SMP serial-framed
packet.

The helper should:

- Build an SMP header using the relay group, event command ID, write-response
  operation, and reserved event sequence.
- Encode the event payload as a CBOR map.
- Use the same serial framing as Zephyr's SMP UART transport.
- Return success or failure without blocking relay safety behavior.

For `reset_executing`, delayed reboot work should emit the event shortly before
`sys_reboot()`. Firmware should allow a short flush delay after transmit, then
perform the existing safe path: turn relays off and execute cold reboot. If event
emission fails, firmware must still proceed with relay-off and reset.

## Delivery contract

Events should be best effort in the first implementation.

They may be missed because:

- The USB device resets before the host receives the frame.
- Host serial buffering drops data during disconnect.
- The host is running older request/response-only code.
- The firmware is older and never emits events.
- The serial link fails while an event is in flight.

Because of that, events must not be the only source of truth for critical state.
Hosts still need to query `status` after reconnect, validate fresh boot when
rebooting, and handle missing events without treating them as fatal.

Best effort is sufficient for the first event class: operator visibility and
session state cleanup. It is not sufficient for durable audit logging or
safety-critical state transitions.

## Candidate future events

The event stream should start small. Normal command responses already report
accepted relay state, validation failures, busy rejections, and reboot
scheduling. Firmware should not duplicate every normal response as an event by
default. Events are most useful when the device needs to report a state change
or lifecycle transition that can happen outside the foreground request flow.

Near-term firmware events:

| Event | Meaning |
| --- | --- |
| `reset_executing` | Firmware is about to execute a scheduled reset. |
| `boot_ready` | Firmware initialized relays, indicators, and the RPC path. |
| `fault_asserted` | Firmware entered a fault or attention state. |
| `fault_cleared` | Firmware left a fault or attention state. |
| `relay_io_error` | Relay GPIO or relay subsystem operation failed. |
| `command_rejected` | Firmware rejected a command, with a reason such as `invalid_argument` or `busy`. |
| `pulse_started` | Firmware accepted and started a relay pulse. |
| `pulse_completed` | A timed pulse ended and the relay returned off. |
| `all_off_applied` | Firmware completed an explicit or safety-driven all-off action. |

Session and connectivity events:

| Event | Meaning |
| --- | --- |
| `heartbeat_timeout` | Future supervised firmware heartbeat deadline expired. |
| `heartbeat_restored` | Future supervised firmware heartbeat recovered after timeout or degraded state. |
| `comm_loss_action_started` | Firmware started a configured communication-loss action. |
| `comm_loss_reboot_pending` | Communication-loss handling scheduled a reboot. |
| `rpc_ready_changed` | Firmware RPC transport readiness changed. |

Firmware update events:

| Event | Meaning |
| --- | --- |
| `image_upload_started` | Firmware image upload workflow started. |
| `image_upload_completed` | Firmware image upload completed. |
| `test_image_pending` | Uploaded image was marked for test boot. |
| `update_reboot_pending` | Update workflow scheduled a reboot. |
| `health_check_started` | Post-boot update health check started. |
| `health_check_passed` | Post-boot update health check passed. |
| `health_check_failed` | Post-boot update health check failed. |
| `image_confirmed` | Running image was confirmed. |
| `rollback_started` | Firmware rollback started. |
| `rollback_completed` | Firmware rollback completed. |

Operational and monitoring events:

| Event | Meaning |
| --- | --- |
| `relay_state_changed` | Commanded relay state changed. |
| `busy_rejection` | A pulse or relay operation was rejected because a relay was busy. |
| `degraded_asserted` | Non-fatal degraded or attention condition appeared. |
| `degraded_cleared` | Non-fatal degraded or attention condition cleared. |
| `indicator_fault` | RGB LED or buzzer indicator backend failed or is unavailable. |
| `counter_threshold` | Invalid requests, busy rejections, or decode errors crossed a configured threshold. |

Some events belong in host tooling rather than firmware async frames because
firmware does not know local users, daemon clients, OS reconnect policy, or
operator identity:

| Event | Better owner |
| --- | --- |
| `daemon_connected` | Host daemon. |
| `daemon_disconnected` | Host daemon. |
| `daemon_reconnected` | Host daemon. |
| `client_connected` | Host daemon or local IPC layer. |
| `client_disconnected` | Host daemon or local IPC layer. |
| `operator_command_started` | Session, CLI, or daemon audit layer. |
| `operator_command_completed` | Session, CLI, or daemon audit layer. |
| `audit_record_written` | Host audit/logging layer. |

Recommended first firmware async set:

- `reset_executing`
- `boot_ready`
- `fault_asserted`
- `fault_cleared`
- `relay_io_error`
- `pulse_completed`

That set validates the event pipe without turning the event stream into a
second copy of normal command responses.

## Alternatives considered

### USB disconnect as event

The host can infer reset execution from serial read failure, port disappearance,
or failed reopen. That requires no firmware change and is robust enough for
fallback behavior. It is not a device-originated event, and OS timing makes it
hard to distinguish reset from cable removal or driver churn.

### Text sentinel on the same port

Firmware could print a line such as `RP2350_RESET_EXECUTING` before reset. This
is easy to prototype, but it mixes logs, shell output, and SMP frames on one
stream. It also forces the host reader to parse two protocols from the same byte
stream. That is fragile and should be limited to debugging if used at all.

### Second CDC ACM interface

A second CDC interface could carry events and logs while the existing CDC
interface remains pure SMP request/response. This gives clean separation, but
it requires USB descriptor work, host discovery changes, and multi-port pairing
by USB identity. It is attractive if event traffic grows into a general log or
monitor channel, but it is heavy for the first event foundation.

### HID or vendor interrupt endpoint

A USB interrupt endpoint fits asynchronous notifications well and avoids mixing
events with SMP responses. It also adds the most host and firmware USB
complexity, including dependency and permission concerns across Windows and
Linux. It is better suited to a later productized monitor channel.

### Post-reboot identity only

Firmware can expose `boot_id`, `boot_generation`, or reset cause in `status`,
and the host can verify after reconnect that a new boot occurred. This is the
best confirmation mechanism, but it is not an asynchronous "reset executing"
notification. It should complement events rather than replace the event stream.

### Acked or persistent events

Acked events would require event IDs, acknowledgement commands, retry policy,
and firmware retention while the host is disconnected. Persistent event logs add
storage and wear policy. Those are useful for audit trails but unnecessary for a
first best-effort notification path.

## Recommended staged path

1. Keep the documented protocol contract in
   `docs/protocol/relay-management.md#planned-event`.
2. Refactor the host serial transport to support a persistent demux reader while
   preserving synchronous `RelayClient` methods.
3. Add simulated transport tests for response/event interleaving, malformed
   frames, timeout, and transport close.
4. Add a host event API and wire session mode to print `reset executing` when
   the event arrives.
5. Add firmware event encoding tests before touching the live reboot path.
6. Emit `reset_executing` from delayed reboot work, keeping event failure
   non-fatal.
7. Keep uptime-based reconnect validation after reboot.

## Open questions

- Should event callbacks run in the reader thread, or should events always be
  queued for consumer-owned processing?
- How large should the host event queue be, and what should happen on overflow?
- Should events include a monotonically increasing volatile event counter once
  more event types exist?
- Which later events justify moving from best-effort notifications to acked or
  persistent event records?
- Should future daemon mode keep an in-memory recent event log for local clients
  even while firmware events remain best effort?
- Should a second CDC or HID event channel be reconsidered if event volume grows
  beyond rare lifecycle and fault notifications?
