<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 041
title: GoldenKey / EngineTag types, hashing, and engine-context refusal
status: in-review
depends-on: [001, 006, 007, 040]
component: qa
estimated-size: S
stream: golden
tag: golden
---

## Objective

Define the GoldenKey/EngineTag/DeterminismClass/LadderEngine types and implement hash(GoldenKey) plus sameEngineContext() so a golden compared across a different engine tag, oversample factor, or renderVersion is refused.

## Context

- `docs/design/11 §5.3` — read first
- `ADR-013 C22` — read first
- `ADR-023 V11` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `golden`.

## Scope

- tests/golden/GoldenKey.h/.cpp with enum LadderEngine{ZDF,Huovilainen}, DeterminismClass{Exact,Fp}, struct EngineTag{ladder,oversampleFactor,renderVersion}, struct GoldenKey{renderGraphHash,engine,sampleRate,blockSize,seed,cls}
- uint64_t hash(const GoldenKey&) noexcept (SHA-256-derived, stable)
- bool sameEngineContext(const EngineTag&, const EngineTag&) noexcept
- Assert sampleRate is in the blessed set {44100,48000,88200,96000} at key construction helper

## Out of scope

- Render execution (golden-4)
- Store path derivation beyond hash (golden-5)

## Acceptance criteria

- [ ] mw101.unit.golden hash() is stable across runs and differs when any field differs [docs/design/11 §5.3]
- [ ] sameEngineContext returns false when ladder, oversampleFactor, or renderVersion differs (paired with a true case for identical tags) [ADR-023 V11; ADR-013 C22]
- [ ] A GoldenKey built with a non-blessed sample rate is rejected/flagged by the construction helper [docs/design/11 §5.2]
- [ ] verify: ctest --preset default -R golden --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R golden --no-tests=error
```
