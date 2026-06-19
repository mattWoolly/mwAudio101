<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 132
title: Engine no-alloc / no-lock / noexcept hot-path guard tests
status: done
depends-on: [006, 118]
component: qa
estimated-size: S
stream: integration
tag: engine_rtsafe
---

## Objective

Add AudioThreadGuard-wrapped tests proving a representative Engine::process performs zero heap allocations, takes zero locks, and that process/reset are noexcept with an injected throw caught as a crash.

## Context

- `docs/design/00 §9.1` — read first
- `docs/design/00 §10` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `engine_rtsafe`.

## Scope

- tests covering RT-1/RT-2/RT-4 against the assembled Engine
- Wrap a representative process() call (max voices, FX on) in AudioThreadGuard
- Assert process and reset are noexcept-qualified
- Exercise prepare-then-process so allocation happens only in prepare

## Out of scope

- Defining AudioThreadGuard (test-harness)
- CPU-budget regression (separate concern, owned by golden/qa streams)
- Per-module RT tests (each module's own stream)

## Acceptance criteria

- [ ] AudioThreadGuard-wrapped process performs zero heap allocations and acquires zero locks per §9.1 RT-1/RT-2
- [ ] process and reset are noexcept; an injected throw is caught as a crash not unwound per §9.1 RT-4
- [ ] Allocation occurs only in prepare per §9.1 RT-6
- [ ] ctest --preset default -R engine_rtsafe --no-tests=error is green; test names begin with engine_rtsafe

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R engine_rtsafe --no-tests=error
```
