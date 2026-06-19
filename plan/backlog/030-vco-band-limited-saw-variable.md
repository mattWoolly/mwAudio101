<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 030
title: VCO band-limited saw + variable PWM pulse
status: done
depends-on: [001, 006, 007, 026, 027, 029]
component: core
estimated-size: M
stream: core-osc
tag: vcoshape
---

## Objective

Complete Oscillator::renderSample to emit band-limited saw and variable-width pulse using PolyBLEP (default) or minBLEP (HQ/escalated), with the PWM width map and the duty/dt overlap clamp.

## Context

- `01-dsp-oscillators.md §4.5` — read first
- `01-dsp-oscillators.md §4.6` — read first
- `01-dsp-oscillators.md §2.2-2.3` — read first
- `ADR-002 Contract C1-C3, C7, C9` — read first
- `01-dsp-oscillators.md §10 (saw construction, PWM, overlap clamp, escalation)` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vcoshape`.

## Scope

- Saw (C1): (2*t-1) - polyBlep(t,dt) in PolyBLEP mode; in HQ/escalated schedule minBLEP step of -2 at wrap fraction and add sawBlep_.next()
- Pulse/PWM (C2): two independent BLEPs/period (+ at rising edge phase 0, - at duty phase); HQ schedules +2/-2 into pulseBlep_
- PWM map (§4.6): duty = kPwmDutyMax - pwmCvNorm*(kPwmDutyMax-kPwmDutyMin); constants from Calibration.h
- Overlap clamp (C3): effective duty = max(kPwmDutyMin, dt) with dutyClamped>=dt and (1-dutyClamped)>=dt
- AA mode selection: aaMode_ set in prepare/setControls only; effective minBLEP when escalated (escalation decision passed in / read from frequencyHz)

## Out of scope

- Per-voice escalation threshold ownership and OscillatorSection wiring (core-osc-7)
- Sub/noise (core-osc-5, core-osc-6)
- Quality-enum-to-OscAaMode derivation (owned by param-schema)

## Acceptance criteria

- [ ] vcoshape-* tests assert band-limited saw equals (2*t-1)-polyBlep(t,dt) sample-for-sample at fixed freq [§4.5, ADR-002 C1]
- [ ] pulse has exactly two corrected transitions/period and DC mean tracks 2*duty-1 within tolerance across a 0.05-0.5 sweep [§4.5, §10]
- [ ] effective duty never below max(kPwmDutyMin, dt) and the two BLEP windows never overlap at the 5%/high-pitch extreme [§4.6, ADR-002 C3]
- [ ] aaMode set only in prepare/setControls, never per-sample on the audio thread [§2.2, ADR-018 Q5]
- [ ] ctest --preset default -R vcoshape --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vcoshape --no-tests=error
```
