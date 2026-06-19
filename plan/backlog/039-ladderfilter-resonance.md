<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 039
title: LadderFilter resonance: inverting feedback + phase comp + diode clamp + self-osc + make-up Q
status: in-review
depends-on: [001, 006, 007, 033, 034, 035, 038]
component: core
estimated-size: M
stream: core-filter
tag: vcf-reso
---

## Objective

Complete LadderFilter resonance: implement setResonance (reso01->k via calibrated curve, kMax=4), the +0.5-sample two-sample-average feedback-phase comp, the feedback-path diode clamp as amplitude governor, the output-side make-up Q scalar, and cross-check the linear regime against the TPT oracle.

## Context

- `02-dsp-filter.md §5.1` — read first
- `02-dsp-filter.md §5.3` — read first
- `02-dsp-filter.md §5.4` — read first
- `02-dsp-filter.md §5.5` — read first
- `02-dsp-filter.md §5.6` — read first
- `ADR-003 F-03` — read first
- `ADR-003 F-04` — read first
- `ADR-003 F-05` — read first
- `ADR-003 F-06` — read first
- `ADR-003 F-13` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vcf-reso`.

## Scope

- Implement setResonance: k = kMax*resonanceCurve(reso01) with kResoCurveExp from Calibration.h; compute makeUpGain_ = 1+makeUpDepth*resonanceCurve(reso01) (§5.1, §5.3, F-05/F-06)
- Add diodeClamp(fb, vClamp) (feedback path) reading vClamp from Calibration.h (§5.4, F-04)
- Wire processSample feedback: fbComp=0.5*(y_[3]+fbPrev_), fb=diodeClamp(k_*fbComp,vClamp), in0=fastTanhKnee(x-fb,invTwoVt), update fbPrev_ (§5.5 steps 1-4, F-03)
- Apply resoTuningComp from FilterTables to k/g for the half-sample residual error (§7.3)

## Out of scope

- Applying makeUpGain (downstream VCA via env-lfo-vca consumes makeUpGain())
- Oversampling-floor / alias verification at 2x (core-filter-8, F-09)

## Acceptance criteria

- [ ] vcf-reso test: disabling the diode clamp leaves the loop unbounded/rail-clipping; enabling it yields a self-osc fixed-point amplitude insensitive to small k perturbations and coefficient rounding (§10 F-04/F-05)
- [ ] vcf-reso test: at reso01=1 with zero input, output is a near-pure sine at ~cutoff with THD below a fixed bound and frequency tracks cutoff (§10 F-05)
- [ ] vcf-reso test: resonant peak tracks cutoff within the <10%-at-2x bound; removing the two-sample average measurably worsens peak detune (§10 F-03)
- [ ] vcf-reso test: makeUpGain() rises monotonically with reso01, is NOT applied inside processSample, and input scaling is invariant to resonance; low-reso magnitude/phase matches LadderReferenceTPT within tolerance (§10 F-06/F-13)
- [ ] verify: ctest --preset default -R vcf-reso --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vcf-reso --no-tests=error
```
