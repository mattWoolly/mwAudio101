<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 034
title: LadderReferenceTPT: offline TPT/ZDF linear 4-pole oracle
status: todo
depends-on: [001, 006]
component: core
estimated-size: M
stream: core-filter
tag: vcf-tpt
---

## Objective

Implement the offline-only Zavalishin TPT/ZDF 4-pole ladder reference (double precision, delay-free global feedback) used as the linear oracle for cross-checking the shipping engine; never compiled into the audio hot path.

## Context

- `02-dsp-filter.md §8` — read first
- `ADR-003 F-13` — read first
- `ADR-003 F-07` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vcf-tpt`.

## Scope

- core/dsp/LadderReferenceTPT.h/.cpp: prepare(fsHz), reset, setCutoffHz (g = tan(pi*fc/fs)), setResonanceK (k in [0,4)), processSample(double) (§8.2)
- Four trapezoidal one-poles + single delay-free ZDF global feedback solved instantaneously, linear (no embedded tanh) (§8.1)
- Reproduce analytic H(0)=1/(1+k) bass droop and k=4 self-osc threshold (§8.2)

## Out of scope

- Any audio-thread / RT-safety guarantees (this is offline test/calibration only)
- Comparison logic against LadderFilter (lives in core-filter-5 cross-check test)

## Acceptance criteria

- [ ] vcf-tpt test: measured DC gain equals 1/(1+k) within tolerance across a k sweep (§8.2, F-07)
- [ ] vcf-tpt test: low-resonance magnitude rolls off at 24 dB/oct one octave above cutoff (§8.2)
- [ ] vcf-tpt test: as k -> 4 the model self-oscillates near cutoff frequency (§8.2)
- [ ] verify: ctest --preset default -R vcf-tpt --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vcf-tpt --no-tests=error
```
