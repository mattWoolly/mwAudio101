<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 027
title: minBLEP residual table + per-voice applicator
status: in-review
depends-on: [001, 006, 007]
component: core
estimated-size: M
stream: core-osc
tag: minblep
---

## Objective

Build the 64x Blackman-windowed minimum-phase step residual table once off the audio thread, plus a per-voice allocation-free overlap-add applicator that schedules and pops band-limited steps.

## Context

- `01-dsp-oscillators.md §3.2` — read first
- `01-dsp-oscillators.md §3.3` — read first
- `01-dsp-oscillators.md §2.4` — read first
- `ADR-002 Contract C8, C11` — read first
- `01-dsp-oscillators.md §10 (alias-suppression oracle, real-time safety)` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `minblep`.

## Scope

- Create core/dsp/MinBlepTable.h/.cpp: build() (init-time, allocates residual_), isBuilt(), residualAt(int), length(); kOversampling=64, kZeroCrossings from Calibration.h
- Blackman window, minimum-phase band-limited step residual; read-only on the audio thread after build()
- Create MinBlepApplicator (in same files per §3.3): prepare(table, sampleRate) sizes ring; reset(); scheduleStep(amp, frac) noexcept; next() noexcept
- ring_ length >= MinBlepTable::length(); all scheduling/pop arithmetic, no alloc/locks

## Out of scope

- PolyBLEP residual (core-osc-1)
- Using the table in any oscillator (core-osc-3+)
- Quality-enum derivation (owned by param-schema/state-presets)

## Acceptance criteria

- [ ] minblep-* tests assert table built once with length 2*kZeroCrossings*kOversampling base-resolution and is read-only post-build [§3.2]
- [ ] scheduleStep then repeated next() reproduces the band-limited step shape and a step of amplitude amp settles to amp [§3.3]
- [ ] allocation/lock guard confirms scheduleStep/next/prepare-time-only-allocates: no heap alloc or locks on next()/scheduleStep [§2.4, ADR-002 C11]
- [ ] kZeroCrossings is read from Calibration.h, not duplicated [§3.2, §10]
- [ ] ctest --preset default -R minblep --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R minblep --no-tests=error
```
