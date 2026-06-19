<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 092
title: Chorus stage: Juno-style anti-phase BBD widener
status: done
depends-on: [001, 006, 007, 088, 089]
component: core
estimated-size: M
stream: fx
tag: fxchorus
---

## Objective

Implement the Chorus stage: two anti-phase LFO-modulated fractional-delay lines panned hard L/R, with Mode/Rate/Depth/Width/Mix, mixing stereo wet into L/R that already hold the dry mono.

## Context

- `07-fx-section.md §5.1` — read first
- `07-fx-section.md §5.1.3` — read first
- `07-fx-section.md §5.1.4` — read first
- `ADR-010 FX-6` — read first
- `ADR-017 L3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `fxchorus`.

## Scope

- core/dsp/fx/Chorus.h/.cpp per §5.1.2 signature; two FractionalDelayLine taps, LFO phases 0.0 and 0.5 (anti-phase) per §5.1.3
- Read offset = kChorusBaseDelayMs + depth*kChorusDepthMs*lfo(phase); Modes Off/I/II/I+II select rate/depth presets per §5.1.3/§5.1.4
- Width scaling: width=0 sums taps to center added equally L/R (mono collapse); width=1 hard-panned anti-phase per §5.1.3
- Mix crossfades wet into the existing dry-in-L/R; add PI constants (kChorusBaseDelayMs, kChorusDepthMs, kChorusModeIRateHz, kChorusModeIIRateHz, kChorusModeIDepth, kChorusModeIIDepth) to Calibration.h
- latencySamples() returns 0 (musical delay) per §5.1.3/ADR-017 L3; smoothed rate/depth/width/mix

## Out of scope

- Per-block (Mode==Off) early-out dispatch / chain wiring (owned by fx-7)
- Drive or Delay processing
- Parameter ID/range registration (owned by param-schema)

## Acceptance criteria

- [ ] fxchorus test: width=0 yields out[L]==out[R] (centered mono collapse) per §5.1.3 / ADR-010 FX-6
- [ ] fxchorus test: width=1 produces anti-phase L/R modulation (the two taps differ; LFO phases 0.5 cycle apart) per §5.1.3
- [ ] fxchorus test: latencySamples() returns 0 per §5.1.3 / ADR-017 L3
- [ ] fxchorus test: prepare/reset/process/setParams perform no heap allocation and take no locks per ADR-010 FX-10
- [ ] Verify: ctest --preset default -R fxchorus --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R fxchorus --no-tests=error
```
