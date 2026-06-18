<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 028
title: White-noise source (xorshift32)
status: in-review
depends-on: [001, 006, 007]
component: core
estimated-size: S
stream: core-osc
tag: noise
---

## Objective

Implement NoiseSource as a flat white xorshift32 PRNG scaled to half-open [-1,1) with per-voice nonzero reseed and an off-by-default single-pole HF rolloff.

## Context

- `01-dsp-oscillators.md §6.1-6.4` — read first
- `01-dsp-oscillators.md §10 (noise whiteness/range, reseed)` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `noise`.

## Scope

- Create core/dsp/NoiseSource.h/.cpp: prepare(sampleRate), reset(seed) (nonzero required), renderSample()
- xorshift32 step x^=x<<13; x^=x>>17; x^=x<<5; scale (float)x*(2/2^32)-1 to [-1,1)
- Per-voice nonzero reseed; reject/avoid zero seed (xorshift cannot escape 0)
- Optional single-pole HF rolloff at kNoiseHfRolloffHz (Calibration.h), default OFF (output white); generator isolated behind NoiseSource for swappability

## Out of scope

- Noise LEVEL scaling/mixing (owned by mixer/param-schema)
- Pink shaping (explicitly not modeled)

## Acceptance criteria

- [ ] noise-* tests assert output is in half-open [-1,1) and spectrum is flat (white, no pinking) within tolerance over a long block with HF rolloff OFF [§6.3, §6.4, §10]
- [ ] distinct per-voice seeds produce decorrelated streams; a zero seed is rejected/avoided [§6.3, §10]
- [ ] kNoiseHfRolloffHz read from Calibration.h, not duplicated [§6.4, §10]
- [ ] renderSample is noexcept with no heap alloc or locks (guard) [§2.4]
- [ ] ctest --preset default -R noise --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R noise --no-tests=error
```
