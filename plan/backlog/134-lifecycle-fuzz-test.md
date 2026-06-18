<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 134
title: Lifecycle/fuzz test: prepare/process/reset over random valid blocks and params
status: todo
depends-on: [006, 118]
component: qa
estimated-size: S
stream: integration
tag: lifecycle_fuzz
---

## Objective

Fuzz the assembled Engine with random valid block sizes, param snapshots, and event streams across re-prepare/reset cycles, asserting it never allocates on the hot path and never trips the guards.

## Context

- `docs/design/00 §5.5` — read first
- `docs/design/00 §10` — read first
- `docs/design/00 §9.1` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `lifecycle_fuzz`.

## Scope

- Randomized sequences of prepare (sample-rate/block-size changes) + process + reset
- Random valid ParamSnapshot values and random valid event streams
- AudioThreadGuard armed around all process/reset calls
- Assert idempotent re-prepare and clean reset-to-known-start

## Out of scope

- Determinism bit-exactness check (integration-5)
- CPU budget (golden/qa)
- Module-internal fuzzing

## Acceptance criteria

- [ ] prepare-then-process over random valid block sizes and params never allocates and never trips guards per §10 lifecycle/fuzz hook
- [ ] Re-prepare is idempotent on sample-rate/block-size change per §5.5
- [ ] reset clears to a known start with no allocation per §5.5
- [ ] ctest --preset default -R lifecycle_fuzz --no-tests=error is green; test names begin with lifecycle_fuzz

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R lifecycle_fuzz --no-tests=error
```
