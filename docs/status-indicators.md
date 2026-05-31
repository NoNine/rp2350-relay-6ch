# Status Indicators

This page is the operator manual for the RP2350-Relay-6CH local RGB LED and
buzzer behavior.

Current firmware enables local status indication when the target devicetree
provides the WS2812 RGB LED or passive buzzer devices. The default behavior is
quiet enough for lab and server-room use: routine heartbeat, idle-ready, and
relay-toggle activity is visual only. Firmware may enable bounded buzzer
notifications for boot-ready, rejected commands, owner-lost faults, and
reboot-pending transitions.

The indicators are local diagnostics. They help an operator answer whether the
controller is alive, ready, handling a command, degraded, updating, or faulted.
They do not replace `rp2350-relay status`, logs, or host-side error reporting.

## Hardware

| Indicator | Hardware | Firmware signal |
| --- | --- | --- |
| RGB LED | On-board WS2812B-0807 | `RGB_CTRL` / GPIO36 |
| Buzzer | 3.3 V passive buzzer through transistor drive | `BUZZER` / GPIO23 PWM |

The six relay channels also have relay-side indicator LEDs. Those relay LEDs
show relay-side drive behavior and are separate from the controller RGB LED.

## RGB LED Meanings

The RGB LED shows controller-level state, not per-channel relay state. Because
there is only one RGB LED, it cannot truthfully show six independent relay
states at the same time.

Firmware-update-specific indications are reserved until the firmware upgrade
workflows are implemented. Phase 9 owns MCUboot and firmware upgrade
foundation work, and Phase 10 owns host upload, test-image, confirmation, and
rollback workflows.

| Pattern | Meaning | Operator action |
| --- | --- | --- |
| Off | Firmware has not taken control of the LED, the board is unpowered, or the indicator feature is disabled. | Check power, USB connection, and firmware version if an LED indication is expected. |
| Dim white | Firmware is booting or initializing. Relay outputs are expected to remain off. | Wait for ready or fault indication. |
| Solid green | Controller is ready, RPC is available, and no relay is commanded on. | Normal idle state. |
| Brief green blink | A valid host command was accepted. | No action unless the host reports an error. |
| Solid cyan or blue | One or more relays are commanded on, or a pulse is active. | Confirm this matches the intended operation. Use `rp2350-relay status` for the commanded state mask. |
| Yellow pulse | Degraded or attention state, such as RPC not ready, repeated invalid requests, busy pulse rejection, or the first phase after an `always-on-owner` communication-loss timeout. | Query `rp2350-relay status`; if owner loss is suspected, restore the session or daemon heartbeat owner and verify relays are in the intended state. |
| Purple or blue animation | Controlled reboot is pending, the final 10 seconds of autonomous communication-loss reboot recovery are pending, or reserved Phase 9/10 firmware upgrade support is active. | For communication-loss recovery, restore the session or daemon heartbeat owner before the reboot delay expires. For explicit reboot or upgrade, do not remove power unless following a documented recovery procedure. |
| Red blink | Firmware or hardware fault requiring attention. Relays should be off unless the fault occurred after an explicit command state. | Run `rp2350-relay status` if reachable, then force `off-all` before inspecting wiring or logs. |

The cyan or blue relay-active indication means commanded relay state only. It
does not prove contact closure, load voltage, current flow, or external
equipment state.

## Buzzer Meanings

The buzzer is a passive device driven by a PWM tone. It is for local attention
and deliberate operator feedback, and must not be used as a continuous
background heartbeat.

| Pattern | Meaning | Operator action |
| --- | --- | --- |
| Silent | Buzzer disabled, unsupported, idle, or PWM output off/zero duty. | No action. |
| One long beep | General notification: controller boot completed and firmware reached ready state. | Begin normal operation, or run `rp2350-relay info` if host communication is not available. |
| Two short beeps | Command rejected or validation error. | Check the CLI error, command arguments, channel number, pulse duration, and current relay pulse state. |
| Three fast fault pulses | `always-on-owner` communication-loss timeout latched owner-lost fault requiring operator action. Each pulse is 250 ms on / 250 ms off. | Restore the heartbeat owner and verify relays are in the intended state. |
| Three short beeps | Controlled reboot scheduled, autonomous communication-loss reboot recovery pending, or reserved Phase 9/10 firmware upgrade support. | Restore the heartbeat owner if communication-loss recovery was unintended; otherwise wait for the board to return and verify `info` or `status`. |

The firmware must not generate a continuous alarm without a timeout or silence
policy. Normal heartbeat, idle ready state, and routine relay toggles must not
produce audible feedback by default. A future host/API configuration can add
quiet mode, buzzer enable, brightness, and lamp/buzzer test controls without
changing these quiet defaults.

## Troubleshooting

| Symptom | Likely meaning | Checks |
| --- | --- | --- |
| No RGB LED after boot | Indicator feature disabled, unsupported firmware, no power, or LED fault. | Run `rp2350-relay info`; check firmware version and board power. |
| RGB LED is yellow | Controller is reachable but needs attention, or a command was rejected. | Run `rp2350-relay status`; inspect invalid-argument and busy counters. |
| RGB LED is red | Firmware or hardware fault. | Disconnect hazardous relay loads, run `off-all` if reachable, then inspect logs and wiring. |
| RGB LED is cyan/blue but equipment is off | Relay is commanded on but load power/contact state is not measured by this controller. | Verify relay wiring, load supply, contacts, and external equipment. |
| Buzzer chirps repeatedly | Fault or local attention state. | Follow site silence/recovery procedure and query status before continuing. |

## Operator Commands

Use the CLI for authoritative state:

```sh
rp2350-relay --port <serial-port> info
rp2350-relay --port <serial-port> status
rp2350-relay --port <serial-port> get
rp2350-relay --port <serial-port> off-all
```

The RGB LED and buzzer are local aids. Host-visible command responses and
status output remain the source of truth for scripts and troubleshooting.
