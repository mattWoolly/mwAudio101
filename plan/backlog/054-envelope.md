<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 054
title: Envelope.cpp: ADSR one-pole segment curve + stage machine
status: done
depends-on: [007, 006, 049, 050]
component: core
estimated-size: M
stream: core-env-lfo-vca
tag: env_curve
---

## Objective

Implement prepare/setParams coefficient precompute and the Attack/Decay/Sustain/Release one-pole contour advancing on the control-rate tick, returning normalized level in [0,1].

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §2.4` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §2.3` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §6.2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `env_curve`.

## Scope

- prepare(sampleRate,controlRateDivider): compute fc, store ticksPerControl (§6.2)
- setParams: per-stage coeff = exp(-1/(max(T,kEnvTimeMin)*fc*kEnvTimeScale)) for A/D/R; clamp sustain to [0,1] (§2.4)
- tick(): one-pole approach level += (target-level)*(1-coeff); Attack target=kEnvAttackOvershoot, advance to Decay at level>=1.0 (clamped); Decay target=sustain; Sustain holds; Release target=0 to Idle (§2.4)
- Snap/advance at kEnvSnapThreshold deterministically (§2.4, ADR-020 S10/S12)
- Use Calibration.h constants; no inlined literals (ADR-020 S13)

## Out of scope

- Trigger-mode retrigger policy (task 4)
- Velocity/depth scaling (ModRouting)
- Upsampling/hold across block (caller)

## Acceptance criteria

- [ ] TDD: Attack rises monotonically to clamped 1.0, Decay falls to sustain, Sustain holds, Release falls to 0 reaching Idle within snap threshold of labeled time (§2.4 acceptance hook)
- [ ] Coefficients computed in prepare/setParams only; tick() does no alloc/locks and is noexcept (§2.1)
- [ ] ctest --preset default -R env_curve --no-tests=error passes; tests named env_curve_* cover each stage

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R env_curve --no-tests=error
```
