<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 065
title: DriftState POD struct and Tier-1/Tier-3/variance draw helpers
status: in-review
depends-on: [001, 006, 007, 063]
component: engine
estimated-size: M
stream: vintage
tag: vintage_draws
---

## Objective

Define the per-voice DriftState POD (core/dsp/drift/DriftState.h) plus the CalibrationDraw and NoteOnOffsets structs and the pure draw helpers drawCalibration, drawSlopCents, and drawNoteOn.

## Context

- `08-vintage-variance.md §4.1` — read first
- `08-vintage-variance.md §4.2` — read first
- `08-vintage-variance.md §6` — read first
- `08-vintage-variance.md §7.1` — read first
- `08-vintage-variance.md §7.2` — read first
- `08-vintage-variance.md §8.1` — read first
- `ADR-009 §Decision 1` — read first
- `ADR-009 §Decision 3` — read first
- `ADR-009 §Decision 4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vintage_draws`.

## Scope

- DriftState per §8.1 aggregating rng, cal, thermal, noteOn, the five mw::dsp::OnePoleSmoother fields, active flag
- CalibrationDraw + drawCalibration(rng, spread01): tuneCents (VR-7/9+VR-2, additive), vcfWidthScale (VR-8, multiplicative), cutoffOffset (widest band) using Calibration.h bands
- NoteOnOffsets + drawNoteOn(...) for the four frozen variance spreads: cutoff/PW additive, env-time/glide multiplicative (1+spread*band); drawSlopCents(rng, slopCents) per kSlopShape
- All band constants referenced from Calibration.h; helpers pure/noexcept, no alloc

## Out of scope

- DriftModel orchestration/processBlock (vintage-4)
- ThermalState internals (vintage-2)
- Defining the OnePoleSmoother (consumed from core-types)

## Acceptance criteria

- [ ] Tests named vintage_draws* verify VR-8 produces a scale on vcfWidthScale not an offset, independent of cutoffOffset path (§4.1)
- [ ] Tests verify cutoff variance/offset band is the widest of the variance set for equal spread (§4.1, §7.1)
- [ ] Tests verify env-time and glide apply as multiplicative (1+spread*band) and cutoff/PW as additive (§7.1)
- [ ] Tests verify cal_spread=0 yields zero perturbation and draws are deterministic for fixed seed (§4.2, §8.1)
- [ ] Verify: ctest --preset default -R vintage_draws --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vintage_draws --no-tests=error
```
