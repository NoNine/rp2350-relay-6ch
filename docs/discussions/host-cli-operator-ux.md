# Host CLI operator UX

This discussion explores what a good terminal interface should look like for
the relay controller host CLI. It is not an implementation contract and does
not promote new firmware, protocol, daemon, network, telemetry, persistence, or
audit-log scope.

The practical near-term direction is a better line-oriented session, not a
full-screen dashboard. The existing `rp2350-relay session` model already has
the right safety shape: explicit connection state, familiar command names,
one-based `CH1` through `CH6` channel arguments, and safe exit checks. The next
UX layer should make that session easier to operate under pressure while
preserving one-shot commands and JSON output for scripts.

## Operator personas

Different operators need different affordances, but they share one central UX
question:

Can the operator always tell which device is connected, which relays are on,
what command just ran, whether it succeeded, and whether it is safe to exit?

| Persona | Main job | UX risk | Helpful terminal behavior |
| --- | --- | --- | --- |
| Bench bring-up operator | Flash, identify, smoke test, and validate a board over USB. | Confusing ports, wrong channel, relay left on after a failed test. | Discovery list, clear port and USB serial, board-label channels, final all-off summary. |
| Remote recovery operator | Restore equipment by switching known relay outputs. | Acting on the wrong controller or ambiguous current state. | Persistent device identity, visible commanded state, conservative command transcript, explicit close refusal when unsafe. |
| Manufacturing or test operator | Repeat a fixed check many times with low variance. | Too much free-form interaction or unclear pass/fail output. | Constrained smoke workflow, terse success/failure messages, copyable errors, guaranteed teardown attempt. |
| Support or debug operator | Diagnose version, transport, firmware, and protocol problems. | Losing context across commands or mixing human output with data output. | Copyable `info`, `build-info`, and `status`; stable exit codes; JSON for scripts; human detail in session mode. |

Safety clarity should outrank speed. Shortcuts are useful only when the prompt,
output, and close behavior still make relay state unmistakable.

## Lessons from Bash

Bash is not a model for this project's visual design. It is a model for mature
terminal ergonomics. The GNU Bash manual describes interactive command-line
editing through Readline, including editable command lines, default Emacs-style
bindings, optional vi-style bindings, and programmable completion
([GNU Bash command-line editing](https://www.gnu.org/software/bash/manual/html_node/Command-Line-Editing.html)).

Useful lessons:

- Keep the command language stable and boring. Operators should learn
  `status`, `get`, `set`, `pulse`, and `off-all` once and use the same names in
  one-shot and session modes.
- Add editing affordances before adding a new UI metaphor. History, reverse
  search, cursor movement, multiline recovery, and completion often solve the
  real operator friction without a full-screen interface.
- Preserve scriptability. One-shot commands should keep clean exit codes and
  JSON output. The interactive session can be richer, but it should not make
  automation parse a conversational transcript.
- Prefer explicit state over hidden state. A shell prompt carries context in a
  compact way; the relay prompt should do the same for connection state and
  safety-relevant relay state.

The relay session does not need to become a shell. It should borrow the parts
that reduce mistakes: editing, history, completion, clear grammar, and stable
command behavior.

## Lessons from Codex and Claude Code

Modern coding-agent TUIs are useful comparison points because they are
long-lived terminal sessions with visible state, commands, transcripts, and
safety boundaries.

OpenAI's Codex CLI help describes a terminal-based coding agent with an
approval workflow and modes that trade off autonomy and safety
([OpenAI Codex CLI](https://help.openai.com/en/articles/11096431-openai-codex-cli-getting-started)).
For this relay controller, the important lesson is not agent autonomy. It is
that high-impact actions deserve visible permission and mode boundaries.

Claude Code documents slash commands such as `/status`, `/terminal-setup`, and
`/statusline` for command discovery, terminal integration, and persistent
session context
([Claude Code commands](https://code.claude.com/docs/en/commands),
[status line](https://code.claude.com/docs/en/statusline),
[terminal setup](https://docs.claude.com/en/docs/claude-code/terminal-config)).
Those features point to several relay-CLI lessons:

- A visible status area is valuable when the session is long-lived. For this
  project, a compact prompt or status line could show connected port, USB
  serial, state mask, on channels, and pulsing channels.
- Command discovery belongs inside the session. `help` should be concise, but
  it can also point to safer command forms and current connection state.
- Terminal setup matters. Windows PowerShell, Linux shells, SSH sessions, and
  IDE terminals differ. The relay CLI should avoid fragile full-screen
  assumptions until the simpler REPL is strong.
- Safety-critical actions should be explicit. The existing `exit` and
  `disconnect` refusal behavior is a good pattern; future destructive or
  ambiguous actions should follow it.

Unlike coding-agent TUIs, the relay CLI controls physical outputs. That raises
the bar: no UI flourish should obscure whether a relay is commanded on.

## Current relay CLI fit

The current host CLI already has useful separation:

- One-shot commands in `docs/cli.md` are good for scripts and diagnostics:
  `info`, `get`, `set`, `pulse`, `off-all`, `status`, `smoke`, and JSON output.
- Session behavior in `docs/host-session-mode.md` is good for direct manual
  operation: device discovery, connected and disconnected prompts, one open
  client, command serialization, heartbeat polling, and safe close checks.
- The implementation in `host/rp2350_relay_6ch/session.py` keeps the grammar
  simple with `shlex`, reuses command names, and refuses normal close when
  relay state is on, pulsing, or unknown.

The main UX gap is not command coverage. It is confidence while operating:

- The prompt currently distinguishes connected from disconnected, but it does
  not continuously summarize the selected device or relay state.
- Human output is copyable, but repeated commands require the operator to keep
  recent state in their head.
- Help is intentionally short, but it does not yet teach safe recovery habits
  such as `status`, `off-all`, then `exit`.
- There is no documented expectation for history, completion, or terminal
  editing behavior.

## REPL Plus direction

Near-term work should keep the line-oriented session and improve it in place.
This is the smallest path that helps all personas without creating a second UI
surface.

Recommended refinements:

- Add command history and basic line editing, using a Python library or standard
  integration that works on Windows and Linux.
- Add tab completion for command names, `on` and `off`, `--force`, and channels
  `1` through `6`.
- Make the connected prompt or a one-line status banner include compact device
  identity and relay state, for example port, USB serial, `on=CH1,CH6`, and
  `pulsing=none`.
- Refresh visible state after relay-changing commands and after `status`, but
  do not silently send `off-all` or hide state-query failures.
- Improve `help` so it groups commands by purpose: inspect, control, safety,
  connection, and exit.
- Keep all command results copyable plain text. Do not rely on color alone for
  state, errors, or warnings.
- Keep one-shot command output unchanged unless a separate compatibility review
  approves changes.

This direction can later support aliases or sequences in host tooling, but it
does not require them.

## Full-screen TUI caution

A full-screen TUI could eventually help manufacturing or monitoring workflows:
relay tiles, live status, menus, keyboard shortcuts, and guided smoke tests are
all plausible. It should not be the first UX refinement.

Reasons to wait:

- Terminal compatibility cost is real across Windows, Linux, SSH, and IDE
  terminals.
- A full-screen display can hide the transcript unless explicitly designed to
  preserve it.
- Relay control benefits from copyable commands and visible command history.
- Most current friction can be solved by REPL affordances first.

If a full-screen TUI is added later, it should wrap the same host library and
safety semantics. It must not bypass normal all-off checks, device identity
display, explicit command results, or one-shot CLI compatibility.

## Candidate evaluation questions

Use these questions before promoting any CLI UX idea into a phase plan:

- Does it make connected device identity clearer?
- Does it make commanded relay state clearer without claiming measured load
  feedback?
- Does it reduce wrong-channel risk for `CH1` through `CH6`?
- Does it preserve explicit `off-all` and safe-exit behavior?
- Does it keep one-shot commands scriptable?
- Does it work on Windows and Linux terminals used by operators?
- Does it belong in host tooling rather than firmware?
- Does it avoid promoting aliases, sequences, audit logs, telemetry, or network
  control into v1 scope by accident?

## Possible follow-up artifacts

This discussion can feed later work, but each item should be promoted
explicitly before implementation:

- A session UX specification for history, completion, prompt state, and help
  output.
- A transcript-style prototype showing expected session output for each
  persona.
- A host-side alias or sequence discussion that remains outside firmware.
- A manufacturing smoke-test UX note for repeatable pass/fail operation.
