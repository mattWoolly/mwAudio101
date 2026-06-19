<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 032
title: OscillatorSection owner + per-voice HQ escalation
status: done
depends-on: [001, 006, 007, 026, 027, 029, 030, 031, 028]
component: core
estimated-size: M
stream: core-osc
tag: oscsection
---

## Objective

Implement the per-voice OscillatorSection aggregating VCO+sub+noise with the load-bearing per-sample ordering, shared MinBlepTable references, and the internal >2 kHz minBLEP auto-escalation keyed off VCO fundamental.

## Context

- `01-dsp-oscillators.md §7.1-7.3` — read first
- `01-dsp-oscillators.md §2.3` — read first
- `01-dsp-oscillators.md §8` — read first
- `ADR-002 Contract C7, C9` — read first
- `ADR-018 Q6` — read first
- `01-dsp-oscillators.md §10 (HQ tier binding, auto-escalation, output contract)` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `oscsection`.

## Scope

- Create core/dsp/OscillatorSection.h/.cpp: prepare(sampleRate, hqTable), reset(noiseSeed), Controls struct, setControls(), renderSample() -> Sources{saw,pulse,sub,noise}
- Per-sample ordering: vco.renderSample() (advances master phase), compute effective AA mode, sub.renderSample(phase, wrapped, freqHz), noise.renderSample()
- Auto-escalation: switch a voice to minBLEP when vco.frequencyHz() > kHqEscalationHz(fs) (Calibration.h, optionally fs-scaled), and back below; never a parameter
- Emit four raw band-limited sources bipolar pre-level/pre-mix; base sample rate only, never oversampled

## Out of scope

- Mixer level sliders and summation (owned by mixer/param-schema; §9 context only)
- Quality-enum-to-OscAaMode/tier derivation (owned by param-schema)
- Glide, CV summation, key assignment (owned by control-core/voice)

## Acceptance criteria

- [ ] oscsection-* tests assert per-sample order: VCO advances master phase before sub reads it, sub uses vco wrap as 4013 clock [§7.3, ADR-002 C4]
- [ ] a voice with VCO fundamental > kHqEscalationHz switches to the minBLEP applicator regardless of tier and reverts below it; escalation is never a parameter [§2.3, ADR-002 C9, ADR-018 Q6]
- [ ] Eco/Standard use PolyBLEP and HQ uses minBLEP; AA mode set only in prepare/reconfiguration [§7.2, ADR-018 Q-table]
- [ ] section never reads filter oversample stride; outputs bipolar [-1,+1] (noise [-1,1)) [§8, ADR-002 C7]
- [ ] kHqEscalationHz read from Calibration.h, not duplicated [§2.3, §10]
- [ ] ctest --preset default -R oscsection --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R oscsection --no-tests=error
```
