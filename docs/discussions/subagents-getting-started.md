# Codex Subagents Getting Started

Subagents are separate Codex agents that can work on bounded tasks while the
main agent keeps moving. They are most useful when a task has independent parts:
one agent can inspect a subsystem, another can make a scoped edit, and the main
agent can keep the critical path moving.

Subagents are not a replacement for clear ownership. Use them when the delegated
work is concrete, independent, and easy to verify.

## When To Use A Subagent

Use a subagent for work that can run in parallel without blocking the next local
step:

- Explore a specific part of a codebase while the main agent inspects another.
- Implement a small change in files that do not overlap with other active work.
- Run focused verification while the main agent reviews or prepares follow-up
  edits.
- Compare options or gather facts for a narrow technical question.

Keep the work local when the next step depends on the answer, when the task is
tightly coupled to active edits, or when the prompt would need a long chain of
context to be safe.

## Roles

Codex subagents commonly use these roles:

- `explorer`: best for read-only codebase questions. Use it for specific
  lookups, architecture checks, or identifying likely files.
- `worker`: best for bounded production work. Give it file or module ownership,
  expected behavior, constraints, and verification expectations.
- Default agent: useful when a task does not clearly fit a specialized role.

Avoid model overrides unless the user asks for one or the task has a clear need.
The inherited model is usually the right default.

## First Subagent Workflow

1. Identify the critical-path task the main agent should do locally.
2. Identify one independent side task that will help but does not block the
   immediate next step.
3. Spawn a subagent with a narrow prompt and a concrete expected output.
4. Continue local work instead of waiting immediately.
5. Wait only when the subagent result is needed.
6. Review the result before integrating it.
7. Close subagents that are no longer needed.

Good delegation should reduce elapsed time without creating coordination
overhead.

## Writing A Good Delegation Prompt

A good subagent prompt includes:

- The exact task.
- The owned files, modules, or responsibility boundary.
- Whether the task is read-only or may edit files.
- The expected final output.
- Verification commands or checks to run, if any.
- Constraints from the current project, such as not reverting others' work.

For code-editing workers, always state that they are not alone in the codebase.
They should not revert changes they did not make, and they should adapt to
nearby changes from other agents or users.

## Prompt Templates

### Exploration

```text
Inspect the codebase and answer this specific question:
<question>

Scope:
- Read-only.
- Focus on <files/modules>.
- Do not make edits.

Return:
- Direct answer.
- Relevant file paths and line references.
- Any uncertainty or follow-up checks needed.
```

### Implementation

```text
Implement <behavior> in <owned files/modules>.

Ownership:
- You own <files/modules>.
- Other agents may be editing elsewhere. Do not revert changes you did not make.

Constraints:
- Follow existing project patterns.
- Keep the change minimal.
- Do not edit outside the ownership scope unless required, and call it out.

Verification:
- Run <commands>, or explain why they could not run.

Return:
- Files changed.
- Behavior changed.
- Verification results.
```

### Review

```text
Review <files/change> for bugs, regressions, and missing tests.

Scope:
- Read-only.
- Prioritize correctness and user-visible behavior.

Return:
- Findings first, ordered by severity.
- File and line references.
- Test gaps or residual risk.
```

### Verification

```text
Run or inspect verification for <behavior>.

Scope:
- Do not change source files.
- Use <commands/checks>.

Return:
- Commands run.
- Pass/fail result.
- Relevant output summary.
- Any blockers.
```

## Common Patterns

Parallel exploration works well when the questions are independent. For
example, one explorer can inspect host-side behavior while another inspects
firmware tests.

Split implementation works when write scopes are disjoint. For example, one
worker can update a CLI surface while another updates documentation, as long as
their files do not overlap.

Background verification works when checks can run while the main agent reviews
or prepares integration. Use it for focused test commands, build checks, or
artifact inspection.

## Common Mistakes

- Delegating the task that blocks the immediate next local step.
- Sending two agents into the same files without clear ownership.
- Giving vague prompts such as "look into this" or "fix the tests".
- Waiting immediately after spawning instead of doing useful local work.
- Accepting a worker's edits without review.
- Leaving agents running after their results are no longer needed.
- Promoting exploratory ideas into implementation without explicit user intent.

## Integration Checklist

Before using a subagent result:

- Confirm the result answers the delegated task.
- Check changed files, if any, for scope creep.
- Run or review relevant verification.
- Reconcile conflicts with current local work.
- Close the agent when done.

When working inside a repository, local instructions still apply. A subagent
inherits the project context, but the main agent remains responsible for final
integration, verification, and handoff.

## Applying Subagents In This Project

This repository has clear firmware, host, script, documentation, and release
boundaries. Subagents work best here when the main agent keeps phase ownership,
relay safety, final review, and handoff centralized while delegating narrow
side tasks.

Use `explorer` agents for read-only questions such as:

- Firmware behavior in `firmware/src/`, `firmware/include/`, board overlays,
  Kconfig, relay tests, indicator tests, or Zephyr API usage.
- Host behavior in `host/rp2350_relay_6ch/`, `host/tests/`, `tools/`, and the
  CLI/session/daemon paths.
- Release and build behavior in `scripts/`, `products/`, `docs/release.md`,
  and `docs/product-build.md`.
- Documentation consistency across the PRD, implementation plan, phase plans,
  protocol docs, CLI docs, and testing procedures.

Use `worker` agents only when the write scope is narrow and disjoint:

- A firmware worker can own one module and its focused tests.
- A host worker can own one CLI, daemon, transport, or client behavior slice and
  its focused tests.
- A docs worker can own one self-contained document or section.
- A script worker can own one wrapper script and its direct tests.

Do not send multiple workers into the same files. If a change crosses firmware,
host, docs, and scripts at the same time, keep the integration path with the
main agent and use explorers for supporting facts.

Good background verification tasks include:

- `scripts/test-host.sh`
- focused `pytest host/tests/...`
- `west build` for a specific firmware target
- `pytest firmware/tests` or a focused Zephyr test harness, when configured
- release artifact inspection after a release-style build has already produced
  outputs

Project-specific guardrails:

- Relay safety stays with the main agent: default-off behavior, teardown, and
  hardware smoke-test claims need direct final review.
- Do not promote discussion documents into firmware, host, protocol, or release
  scope without explicit user direction.
- Do not create or update phase verification reports unless the user explicitly
  asks for that report.
- Hardware checks must name the hardware actually used or state that hardware
  was not used.
- For docs-only work, do not rebuild firmware, wheels, or release artifacts
  unless the changed doc is embedded in an artifact.
- Every worker prompt should remind the worker that other changes may exist and
  that it must not revert edits it did not make.

### Firmware Explorer Prompt

```text
Inspect the firmware side of <behavior>.

Scope:
- Read-only.
- Focus on firmware/src/, firmware/include/, firmware/tests/, and relevant
  board or Kconfig files.
- Do not edit files.

Return:
- Current behavior.
- Relevant file paths and line references.
- Gaps or risks for the requested change.
- Firmware tests that should cover the behavior.
```

### Host Worker Prompt

```text
Implement <host behavior> in <owned host files/tests>.

Ownership:
- You own <specific host files/tests>.
- Other agents or users may be editing elsewhere. Do not revert changes you did
  not make.

Constraints:
- Follow the existing Python host patterns and typed exceptions.
- Keep the change minimal and avoid firmware, docs, or script edits unless they
  are explicitly listed in the ownership scope.

Verification:
- Run <focused pytest command>, or explain why it could not run.

Return:
- Files changed.
- Behavior changed.
- Verification results.
```

### Docs Worker Prompt

```text
Update <document/section> for <documentation behavior>.

Ownership:
- You own only <document/section>.
- Do not update README, AGENTS.md, phase plans, or verification reports unless
  the task explicitly says to do so.

Constraints:
- Keep documentation roles distinct.
- Cross-reference existing authoritative docs instead of duplicating rules.

Return:
- Files changed.
- Summary of the documentation change.
- Any source docs used.
```

### Verification Agent Prompt

```text
Verify <behavior> with <command/check>.

Scope:
- Do not edit source files.
- Do not claim hardware results unless hardware is actually attached and used.

Return:
- Commands run.
- Result.
- Relevant output summary.
- Hardware used, or "Not used".
- Any blockers.
```
