# REPL Plus CLI UX contract

This document is the implementation contract for the first REPL Plus host CLI
UX improvement. It refines the operator-facing terminal interface while keeping
the existing host library, firmware protocol, safety behavior, and one-shot CLI
semantics intact.

Use [CLI utility](cli.md) for operator usage, [Host session mode](host-session-mode.md)
for existing session connection and safety semantics, and
[Host CLI operator UX](discussions/host-cli-operator-ux.md) for design
background.

## Scope

This contract covers:

- Session prompt shape.
- Session `help` output.
- Session startup and disconnected prompt examples.
- One-shot human output expectations.
- Parser ergonomics for session startup options.
- Session command tab completion.

This contract does not add firmware behavior, protocol fields, daemon behavior,
network control, telemetry, aliases, sequences, audit logs, persistent
relay-on state, full-screen TUI behavior, command history, or full line-editing
features beyond command tab completion.

Line editing beyond the completion support provided by the prompt library and
command history remain future REPL Plus work.

## Motivation

Native CLI evaluation found four near-term UX issues:

- `rp2350-relay session --help` is too sparse to teach session use.
- `rp2350-relay session --port COM7` fails because global options must appear
  before the `session` subcommand.
- In-session `help` is complete but too dense as a single long line.
- The previous one-line startup banner was hard to scan in copied terminal
  transcripts.
- The disconnected prompt is clear, but prompt style should be consistent with
  the connected prompt.

The improvement should keep the interface line-oriented. It should make the
current session easier to scan and safer to operate without introducing a
full-screen terminal UI.

## Readability principles

Interactive output should read well both live in a terminal and after being
copied into a ticket, chat, or verification report.

- Prefer grouped sections, aligned labels, and blank lines between unrelated
  groups when output carries multiple kinds of information.
- Use boxed panels only for high-context session events such as startup
  connection summaries. Routine command results should stay plain grouped text.
- Keep safety-critical state visible as text. Color may help live terminals,
  but the plain transcript must remain understandable without it.
- Keep prompts visually distinct from command output, but do not encode relay
  state in the prompt.
- Keep one-shot command output compact, result-focused, and script-friendly.

## Prompt contract

The session prompt identifies connection state only. Relay state must not be
encoded in the prompt.

Connected prompt:

```text
rp2350-relay[COM7]$
```

Disconnected prompt:

```text
rp2350-relay[disconnected]$
```

Rules:

- Use the active serial port inside brackets while connected.
- Use `disconnected` inside brackets while disconnected.
- When attached to a real terminal, the prompt may render `rp2350-relay` in
  green and the bracketed port or state in blue. Redirected output and test
  streams must remain plain text with no ANSI escape sequences.
- Do not include `on=`, `state=`, `pulsing=`, hardware, USB serial, or protocol
  version in the prompt.
- Continue to report relay state in startup banners, `status`, relay command
  results, and unsafe-close messages.
- Do not rely on color for safety-relevant information.

## Session startup and status output

On successful session connection, keep a `/status`-style boxed startup summary
with device identity and current relay state:

```text
╭──────────────────────────────────────────────────────────────╮
│   RP2350 Relay Session                                       │
│                                                              │
│   Connection:   connected                                    │
│   Port:         COM7                                         │
│   Serial:       e6614c311f4b8b2f                             │
│   Hardware:     RP2350-Relay-6CH                             │
│   Protocol:     3                                            │
│   Relay count:  6                                            │
│   State:        0x3f                                         │
│   On:           CH1, CH2, CH3, CH4, CH5, CH6                 │
│   Pulsing:      none                                         │
╰──────────────────────────────────────────────────────────────╯

rp2350-relay[COM7]$
```

`status` output should be grouped and scannable. For all six channels on:

```text
rp2350-relay[COM7]$ status

relays:
  state: 0x3f
  on: CH1, CH2, CH3, CH4, CH5, CH6
  pulsing: none

transport:
  uptime_ms: 214508
  received: 58
  failed: 0
  last_error: 0

rp2350-relay[COM7]$
```

The exact transport keys should match the firmware response fields available
at implementation time. Do not invent telemetry. If a key is absent from the
device response, omit it instead of printing speculative values.

USB discovery, candidate matching, exact-port metadata attachment, and reconnect
behavior are specified in [Host session mode](host-session-mode.md). This REPL
Plus contract owns only the terminal UX around prompts, help, parser ergonomics,
and human output formatting.

Interactive discovery output should use the candidate wording from the session
contract before returning to the prompt style defined here.

## Session help output

In-session `help` must be grouped by operator task:

```text
rp2350-relay[COM7]$ help

Inspect:
  status                       show relay state, transport counters, and last error
  get                          show all relay states
  get <channel>                show one relay state
  info                         show controller hardware and protocol information
  build-info                   show firmware build details

Control:
  set <channel> <on|off>       set one relay
  set-all <mask>               set all relays from a six-bit mask
  pulse <channel> <duration>   pulse one relay for duration in ms
  off-all                      turn every relay off and cancel pulses

Connection:
  connect                      connect using saved selector or discovery
  connect --port <port>        connect to a serial port
  connect --serial <serial>    connect by USB serial number
  disconnect                   close only when relays are confirmed off
  disconnect --force           close without confirmed all-off state

Exit:
  exit                         exit only when relays are confirmed off
  quit                         same as exit
  exit --force                 exit without confirmed all-off state
  quit --force                 same as exit --force

Notes:
  Channels are board labels: 1 is CH1, 6 is CH6.
  Run off-all before disconnecting or exiting.
  Use --force only when you intentionally accept unknown or active relay state.
```

The help text must stay plain and copyable. It may be updated for exact field
names or spacing during implementation, but it must preserve the same command
groups and safety messages.

## Session tab completion

Interactive session mode should complete command names, supported options,
relay channel labels `1` through `6`, and `set` states `on` and `off`.
Completion must be state-aware: disconnected sessions complete only `connect`,
`help`, `exit`, and `quit`. Completion must not perform USB discovery or change
command validation semantics.

## Safe exit output

Normal `exit`, `quit`, and `disconnect` must keep the existing safe-close
behavior. When all six channels are on, `exit` should refuse to close:

```text
rp2350-relay[COM7]$ exit

refusing to close: on=CH1, CH2, CH3, CH4, CH5, CH6 pulsing=none
run 'off-all' first or use --force

rp2350-relay[COM7]$
```

After `off-all`, output should remain compact:

```text
rp2350-relay[COM7]$ off-all

state: 0x00
on: none
pulsing: none

rp2350-relay[COM7]$ exit
```

Forced close behavior remains unchanged: it closes without changing relay
state and prints a warning that all-off state was not confirmed.

## One-shot human output

One-shot commands must remain compact, result-focused, and script-friendly.
They must not print a session prompt or connection banner. `--output json`
must remain unchanged.

One-shot `status`:

```text
$ rp2350-relay --port COM7 status

relays:
  state: 0x3f
  on: CH1, CH2, CH3, CH4, CH5, CH6
  pulsing: none

transport:
  uptime_ms: 214508
  received: 58
  failed: 0
  last_error: 0
```

One-shot relay set:

```text
$ rp2350-relay --port COM7 set 1 on

state: 0x01
on: CH1
pulsing: none
```

One-shot single-channel get:

```text
$ rp2350-relay --port COM7 get 1

channel: CH1
on: true
pulsing: false
```

One-shot all-off:

```text
$ rp2350-relay --port COM7 off-all

state: 0x00
on: none
pulsing: none
```

Errors remain terse and stderr-oriented:

```text
$ rp2350-relay --port COM7 get 7

argument error: channel must be 1..6
```

## Parser ergonomics

The implementation should accept both session startup forms:

```sh
rp2350-relay --port COM7 session
rp2350-relay session --port COM7
```

It should also accept session-local forms for `--serial`, `--baud`,
`--timeout`, and `--retries`:

```sh
rp2350-relay session --serial e6614c311f4b8b2f
rp2350-relay session --port COM7 --baud 115200 --timeout 2.0 --retries 1
```

If the same option is supplied before and after `session`, the session-local
value wins for the session command.

`rp2350-relay session --help` must show session-specific examples and mention
safe exit behavior. The top-level help must continue to list `session` as an
available command.

## Acceptance checks

Automated tests should cover:

- `rp2350-relay --port COM7 session` still parses.
- `rp2350-relay session --port COM7` parses.
- Session-local `--serial`, `--baud`, `--timeout`, and `--retries` parse.
- `rp2350-relay session --help` includes examples and safe-exit language.
- Connected prompts use `rp2350-relay[<port>]$`.
- Disconnected prompts use `rp2350-relay[disconnected]$`.
- In-session `help` prints the required groups and safety notes.
- Safe close still refuses when relay state is on, pulsing, or unknown.
- One-shot JSON output remains unchanged.
- One-shot human output remains plain, compact, and copyable.

Manual no-hardware checks:

```sh
rp2350-relay session
```

Then run:

```text
help
status
exit
```

Expected result: the session enters disconnected mode when no relay controller
is found, prints `rp2350-relay[disconnected]$`, shows grouped help, rejects
`status` with a clear not-connected message, and exits with status `0`.

Manual parser checks:

```sh
rp2350-relay session --help
rp2350-relay --port COM7 session --help
rp2350-relay session --port COM7 --help
```

Expected result: help is understandable and option ordering does not surprise
operators.
