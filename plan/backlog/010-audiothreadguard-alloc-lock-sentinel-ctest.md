<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 010
title: AudioThreadGuard alloc/lock sentinel + ctest
status: done
depends-on: [006]
component: qa
estimated-size: M
stream: infra
tag: rt
---

## Objective

Implement tests/invariants/AudioThreadGuard.{h,cpp}: a processBlock-scope sentinel overriding global new/malloc/free + mutex hooks that records heap allocs/locks and FAILS, with a documented one-time warm-up carve-out, plus self-tests.

## Context

- `docs/design/11 §13.1` — read first
- `docs/design/00 §9.1` — read first
- `ADR-013 C19` — read first
- `ADR-001 C3` — read first
- `ADR-001 C4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `rt`.

## Scope

- AudioThreadGuard API: arm()/disarm()/violated()/violations()/allowWarmUpOnce() per §13.1
- override global operator new/malloc/free and pthread mutex hooks (or RT-safety harness) scoped so it never ships in release
- self-tests with the rt tag: an armed scope that allocates/locks trips a violation (positive); a clean scope does not (negative control); the one-time warm-up carve-out is exercised
- verify via ctest --preset default -R rt --no-tests=error

## Out of scope

- wrapping the real Engine::process stress sweep (full-engine/voice stream consumes this guard)
- RT-safety of renderVersion legacy-path selection (full-engine stream, §13.3)

## Acceptance criteria

- [ ] any heap alloc or lock during an armed scope is recorded and FAILS, outside the one-time warm-up carve-out [docs/design/11 §13.1; ADR-013 C19; ADR-001 C3, C4]
- [ ] a clean armed scope reports no violation (negative control) and the carve-out is non-blanket [docs/design/11 §13.1]
- [ ] the global-new override is scoped so it never ships in release [docs/design/11 §13.1; ADR-001 Consequences]
- [ ] tests are named rt* and pass under ctest --preset default -R rt --no-tests=error [docs/design/11 §8.3]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R rt --no-tests=error
```
