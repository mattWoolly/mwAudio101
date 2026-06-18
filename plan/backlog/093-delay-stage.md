<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 093
title: Delay stage: tempo-synced mono-core stereo delay with damped feedback
status: in-review
depends-on: [001, 006, 007, 088, 089]
component: core
estimated-size: M
stream: fx
tag: fxdelay
---

## Objective

Implement the Delay stage: single mono delay core to stereo output with fractional read, feedback-path damping LPF + gentle saturation, clamped feedback, tempo sync, optional ping-pong, and click-free time glide.

## Context

- `07-fx-section.md §5.2` — read first
- `07-fx-section.md §5.2.3` — read first
- `07-fx-section.md §5.2.4` — read first
- `07-fx-section.md §5.2.5` — read first
- `07-fx-section.md §5.2.6` — read first
- `ADR-010 FX-7` — read first
- `ADR-010 FX-8` — read first
- `ADR-010 FX-11` — read first
- `ADR-017 L3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `fxdelay`.

## Scope

- core/dsp/fx/Delay.h/.cpp per §5.2.2 signature; mono-sum into one FractionalDelayLine core, read to stereo out per §5.2.1
- Tempo sync: delayMs=(60000/bpm)*beatsPerDivision over {1/4,1/8,1/8.,1/8T,1/16,1/16T}; conversion cached, recomputed only on tempo/division change per §5.2.3
- Feedback clamped to [0,kDelayMaxFeedback] (<1.0); one-pole damping LPF + gentle tanh saturation + denormal flush in feedback path per §5.2.4
- Ping-pong alternating tap routing; Width (0=centered mono); Mix; pointer-glide SmoothedValue for click-free time changes per §5.2.4/§5.2.5
- Add PI constants (kDelayMaxMs, kDelayMaxFeedback, kDelayDampHzMin/Max, kDelaySatDrive, kDelayTimeGlideMs) to Calibration.h; latencySamples()==0 per ADR-017 L3

## Out of scope

- Per-block (delay.on) early-out dispatch / chain wiring (owned by fx-7)
- Sourcing host BPM from AudioPlayHead (plugin/ supplies hostBpm via FxParams)
- Parameter ID/range registration (owned by param-schema)

## Acceptance criteria

- [ ] fxdelay test: with sync=ON, realized delay equals (60000/bpm)*beatsPerDivision within one sample, and conversion recomputes only on tempo/division change per §5.2.3 / ADR-010 FX-7
- [ ] fxdelay test: requesting feedback=1.0 applies a clamped value < kDelayMaxFeedback and a long impulse-fed run stays bounded per §5.2.4 / ADR-010 FX-8
- [ ] fxdelay test: width=0 yields out[L]==out[R] (centered mono) per §5.2.4 / ADR-010 FX-8
- [ ] fxdelay test: stepping delay time/division at full feedback produces no sample discontinuity above threshold (pointer-glide) per §5.2.5 / ADR-010 FX-11
- [ ] fxdelay test: latencySamples()==0 and process/setParams/prepare/reset perform no heap alloc and no locks (feedback flushes denormals) per ADR-017 L3 / ADR-010 FX-10
- [ ] Verify: ctest --preset default -R fxdelay --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R fxdelay --no-tests=error
```
