<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 055
title: Lfo rate/phase + SmoothTri and Square cores
status: todo
depends-on: [007, 006, 049, 051]
component: core
estimated-size: M
stream: core-env-lfo-vca
tag: lfo_core
---

## Objective

Implement prepare, rate clamping/phase increment, and the continuous SmoothTri (triangle rounded toward sine) and Square waveform cores.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §3.4` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §3.5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `lfo_core`.

## Scope

- setRateHz clamps to [0.1,30] Hz; phaseInc_ = rateHz/fc where fc=sampleRate/ticksPerControl (§3.4)
- Phase accumulation in [0,1) advancing on control-rate tick (§3.4)
- SmoothTri: native triangle then out=lerp(tri,sineApprox(tri),kLfoSmoothShape), labeled rounded-toward-sine not pure sine (§3.5)
- Square: out=(phase<0.5)?+1:-1, intentionally hard-edged (§3.5)
- Use kLfoSmoothShape from Calibration.h; bipolar [-1,1] output

## Out of scope

- Random/Noise cores (task 7)
- cycleEdge wiring to envelope
- Mod-bus LPF (ModRouting)

## Acceptance criteria

- [ ] Rate clamped to [0.1,30] Hz; 0.35 Hz never enforced as minimum (§3.4 acceptance hook)
- [ ] SmoothTri yields triangle-rounded-toward-sine (not pure sine), bipolar within [-1,1] (§3.5)
- [ ] ctest --preset default -R lfo_core --no-tests=error passes; tests lfo_core_* cover clamp, phase, SmoothTri, Square

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R lfo_core --no-tests=error
```
