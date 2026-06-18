<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 064
title: ThermalState OU/pink/warm-up shared thermal integrator
status: todo
depends-on: [001, 006, 007, 063]
component: engine
estimated-size: M
stream: vintage
tag: vintage_thermal
---

## Objective

Implement the shared scalar ThermalState in core/dsp/drift/ThermalState.h: a bounded Ornstein-Uhlenbeck integrator with optional fixed-coefficient Voss-McCartney pink and optional exponential warm-up transient, advanced once per block.

## Context

- `08-vintage-variance.md §5.1` — read first
- `08-vintage-variance.md §5.3` — read first
- `08-vintage-variance.md §5.4` — read first
- `08-vintage-variance.md §12.6` — read first
- `ADR-009 §Decision 2` — read first
- `ADR-009 VV-13` — read first
- `ADR-009 VV-5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vintage_thermal`.

## Scope

- ThermalState struct per §5.4 (T, pinkState[7], warmupSec) with reset(bool cold) and tick(rng, rate01, dtBlock, usePink, useWarmup, warmupTimeMin)
- OU update T += -k*T*dt + sigma*sqrt(dt)*N(0,1) with k mapped from rate01, clamped to +/-kDriftClampCents
- Optional Voss-McCartney pink summed in (off by default); warm-up Twarm = kWarmupCents*exp(-t/tau) added to T
- Explicit denormal flush on integrator state; value() accessor reading shared T

## Out of scope

- PRNG implementation (consumes vintage-1)
- Mapping T to pitch/cutoff cents (vintage-4)
- Per-voice ownership/decorrelation orchestration (vintage-4)

## Acceptance criteria

- [ ] Tests named vintage_thermal* verify T stays within +/-kDriftClampCents over arbitrarily long runs, no runaway (§5.1, §12.6)
- [ ] Tests verify no denormals/NaN after long silence with FTZ/DAZ guard (§12.6)
- [ ] Tests verify warm-up off by default and, when on, decays toward zero over warmupTimeMin (§5.3, VV-5)
- [ ] Tests verify tick advances state exactly once per call (block-rate), deterministic for fixed seed (§5.4)
- [ ] Verify: ctest --preset default -R vintage_thermal --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vintage_thermal --no-tests=error
```
