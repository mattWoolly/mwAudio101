<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 057
title: ModRouting.cpp: depth scaling, velocity routing, mod-bus LPF
status: done
depends-on: [007, 006, 049, 053]
component: core
estimated-size: M
stream: core-env-lfo-vca
tag: modrouting_combine
---

## Objective

Implement the fixed-routing combine: scale the one envelope and one LFO by per-destination depths, apply the kModBusLpHz mod-bus LPF, and fold in velocity (ON by default) to VCA level and VCF cutoff amount.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §5.2` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §3.5` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §5.3` — read first
- `plan/decisions/016-owner-ratifications-2026-06-18.md` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `modrouting_combine`.

## Scope

- prepare computes ModBus.lpCoeff from kModBusLpHz; apply one-pole LPF to modulation signals (§3.5)
- Scale env/LFO values by ModDepths into per-destination contributions (§5.1)
- VCA level: vcaControl=baseAmp*lerp(1.0,velNorm,toVcaAmount*velEnabled) (§5.2)
- VCF cutoff amount: cutoffMod+=velNorm*toCutoffAmount*velEnabled (additive contribution) (§5.2)
- Velocity ON by default (ADR-016 R-2); velNorm shaped by kVelCurve; defaults from kVelToVca/kVelToCutoff (§5.2)

## Out of scope

- VCF/VCO transfer functions (other docs)
- LFO value generation (Lfo tasks)
- Envelope contour (Envelope tasks)

## Acceptance criteria

- [ ] Velocity ON routes to VCA level and VCF cutoff amount; OFF removes both contributions (faithful pole) (§5.2 / ADR-016 R-2 acceptance hook)
- [ ] Mod bus applies the fixed kModBusLpHz LPF; depth scalars are (PI) from Calibration.h, never inlined (§3.5, §5.3, ADR-020 S13)
- [ ] combine path noexcept, no alloc/locks; ctest --preset default -R modrouting_combine --no-tests=error passes; tests modrouting_combine_* incl velocity on/off pair

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R modrouting_combine --no-tests=error
```
