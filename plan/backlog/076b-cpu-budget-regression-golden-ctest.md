<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 076b
title: CPU-budget regression golden ctest — measureWorstCaseBlockMicros HARD gate at max poly+unison @2x
status: in-review
depends-on: [118, 076, 046]
component: qa
estimated-size: M
stream: golden
tag: golden
---

## Objective

Implement the CPU-budget regression golden: render a worst-case patch (full poly + full unison + 2x oversampling + Newton-iterated ladder) via the assembled engine and assert median per-block wall-time stays under the committed ceilingMicrosPerBlock, pinned in MANIFEST alongside engine + oversample factor, as a HARD gate.

## Context

- `docs/design/11 §13.5` — read first
- `ADR-013 C21` — read first
- `ADR-019 VT-05` — read first
- `research/10 §3.6` — read first
- `research/10 §3.7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `golden`.

## Scope

- mw::test::CpuBudgetSpec struct + double measureWorstCaseBlockMicros(const CpuBudgetSpec&) returning the median of N runs (§13.5)
- Drive the full assembled engine (Engine::process from 118) through the offline RenderHarness (076) at full poly, full unison, 2x oversample, Newton-iterated ladder (§13.5)
- Read ceilingMicrosPerBlock and the pinned engine + oversample factor from MANIFEST (parsed via 046); the ceiling is (PI), host-relative, re-derived per reference host id (§13.5)
- ctest asserts worst-case median per-block micros < committed ceiling and FAILS (hard gate) when exceeded [ADR-013 C21; ADR-019 VT-05]

## Out of scope

- Deriving/committing the ceiling value itself (a tuning/MANIFEST authoring action)
- CLASS-EXACT/CLASS-FP audio-output comparison (other golden tasks)
- Per-format wall-time differences (host smoke matrix 140)

## Acceptance criteria

- [ ] A worst-case patch (full poly + unison + 2x OS + Newton ladder) whose per-block wall-time exceeds the committed ceiling => ctest FAILS [docs/design/11 §13.5; ADR-013 C21; ADR-019 VT-05]
- [ ] ceilingMicrosPerBlock, engine tag, and oversample factor are read from MANIFEST.toml (via 046), not hard-coded in the test [docs/design/11 §13.5]
- [ ] measureWorstCaseBlockMicros returns the median of N runs; the test is tagged 'cal'/golden so the name-prefix discovery gate sees it [ADR-013 C2]
- [ ] An injected fidelity change that blows the RT budget is caught as a regression by this gate, demonstrated against a stub that exceeds the ceiling [docs/design/11 §13.5]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R golden --no-tests=error
```
