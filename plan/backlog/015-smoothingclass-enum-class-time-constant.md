<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 015
title: SmoothingClass enum + per-class time-constant accessor (SmoothingClass.h)
status: todo
depends-on: [001, 006]
component: core
estimated-size: S
stream: params
tag: smoothclass
---

## Objective

Define the SmoothingClass enum and a constexpr accessor returning each class's de-zipper time-constant by reading the centralized calibration table.

## Context

- `docs/design/06-parameters-state-presets.md §3.9` — read first
- `§3.10` — read first
- `§2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `smoothclass`.

## Scope

- core/params/SmoothingClass.h: enum class SmoothingClass : uint8_t { NoSmooth=0, Pitch, Fast, PulseWidth, Level, Glide } (§3.9)
- Accessor mapping each class to its (PI) time constant pulled from Calibration.h (S1~2ms, S2~10ms, S3~5ms, S4~15ms, S5~20ms) — referenced, never inlined (§3.10)
- NoSmooth maps to no/zero time constant (the default class)
- Test asserting the enum order/values and that each non-NoSmooth class returns the named calibration constant

## Out of scope

- the one-pole de-zipper DSP itself (core-types OnePoleSmoother)
- per-param class assignment (params-3)
- calibration constant numeric values (owned by core-types Calibration.h)

## Acceptance criteria

- [ ] Enum has exactly the six classes in the §3.9 order with NoSmooth==0 [§3.9]
- [ ] Accessor returns the calibration-table constant per class, no inlined literal [§3.10]
- [ ] Test names begin with smoothclass and verify the class->constant mapping reads Calibration.h [§3.9; §3.10]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R smoothclass --no-tests=error
```
