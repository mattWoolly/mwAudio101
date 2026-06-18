<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 033
title: FastTanh.h: shared frozen tanh approximation + OTA-knee transconductor
status: in-review
depends-on: [001, 006, 007]
component: core
estimated-size: S
stream: core-filter
tag: vcf-tanh
---

## Objective

Provide the single header-only fast tanh approximation (mw::dsp::fastTanh, fastTanhKnee) used by every filter stage and the feedback path, with frozen versioned coefficients so the engine is bit-exact.

## Context

- `02-dsp-filter.md §4` — read first
- `02-dsp-filter.md §10 F-10` — read first
- `ADR-003 F-10` — read first
- `ADR-003 F-14` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vcf-tanh`.

## Scope

- core/dsp/FastTanh.h: inline fastTanh(x) Pade-style rational x*(27+x^2)/(27+9*x^2) clamped to +/-1 beyond validity (§4.2)
- inline fastTanhKnee(x, invTwoVt) = fastTanh(x*invTwoVt) folding the OTA knee scaler (§4.2)
- Read rational coefficients and invTwoVt source semantics from Calibration.h (vcf::tanhCoeffs, vcf::invTwoVt); no inline (PI) literals (§9, F-15)
- Branch-light, noexcept, no std::tanh/std::tan/std::exp

## Out of scope

- Calibration.h definition of the constants (consumed from core-types)
- Using fastTanh inside the ladder (that is core-filter-4/5)

## Acceptance criteria

- [ ] vcf-tanh test: fastTanh is odd-symmetric and monotone on the working range, saturates to +/-1, max abs error vs std::tanh below a fixed bound (§10 F-10)
- [ ] vcf-tanh test: no std::tanh/std::tan/std::exp symbol reachable from fastTanh/fastTanhKnee (§10 F-10)
- [ ] vcf-tanh test: coefficients come from Calibration.h, no (PI) numeric literal in FastTanh.h (§10 F-15)
- [ ] verify: ctest --preset default -R vcf-tanh --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vcf-tanh --no-tests=error
```
