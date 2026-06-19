<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 079
title: Calibration-tool self-tests — planted-answer, disjoint cal/val, negative control
status: in-review
depends-on: [001, 006, 007, 076]
component: qa
estimated-size: M
stream: golden
tag: cal
---

## Objective

Implement the Layer-4 calibration self-tests using planted fixtures: recover known params within tolerance, validate held-out error on a disjoint cal/val split, and reject a deliberately-wrong fixture (negative control).

## Context

- `docs/design/11 §12` — read first
- `ADR-013 C15` — read first
- `ADR-013 C16` — read first
- `ADR-013 C17` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `cal`.

## Scope

- tests/cal/CalibrationSelfTests.cpp with PlantedFixture{knownParams,signal,seed} and CalValSplit{fitSet,valSet}
- Planted-answer: synthesize signal from known CalibrationParams, run calibrator, assert recovery within Calibration.h tolerance
- Disjoint cal/val: fit on set A, validate held-out error on disjoint set B (disjoint by construction)
- Negative control: calibrator must reject a deliberately-wrong planted fixture

## Out of scope

- The calibration model/physics (owned by calibration design doc + Calibration.h; consumed only)
- Recovery tolerance constant (lives in core/calibration/Calibration.h)

## Acceptance criteria

- [ ] mw101.cal planted-answer recovers params within tolerance; an echo-input stub FAILS (paired) [ADR-013 C15]
- [ ] Disjoint cal/val held-out error within tolerance; fit and validate seed/param sets are disjoint by construction [ADR-013 C16]
- [ ] Negative control: the calibrator REJECTS the wrong fixture; acceptance FAILS if it accepts [ADR-013 C17]
- [ ] verify: ctest --preset default -R cal --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R cal --no-tests=error
```
