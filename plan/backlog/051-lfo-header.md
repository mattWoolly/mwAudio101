<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 051
title: Lfo.h header: LfoShape enum + Lfo class layout
status: done
depends-on: [007, 006, 001]
component: core
estimated-size: S
stream: core-env-lfo-vca
tag: lfo_header
---

## Objective

Declare the single-LFO public API and POD state in core/dsp/Lfo.h exactly as §3.3, with the four-position LfoShape enum (no Sine/Saw).

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §3.2` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §3.3` — read first
- `plan/decisions/020-parameter-smoothing-policy.md S14` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `lfo_header`.

## Scope

- Declare enum class LfoShape {SmoothTri=0,Square=1,Random=2,Noise=3} only (§3.2)
- Declare Lfo class: prepare/reset/setRateHz/setShape/resetPhaseOnKey/tick/cycleEdge/value/setNoiseSource (§3.3)
- POD private state per §3.3 incl phase_, phaseInc_, shReg_, rngState_, noiseSample_ pointer
- namespace mw101::dsp; hot paths noexcept

## Out of scope

- Waveform DSP (.cpp tasks)
- Arp/seq clock edge logic (control-rate doc)
- Noise source ownership

## Acceptance criteria

- [ ] Header has exactly four LfoShape enumerators; no Sine or Saw symbol exists (§3.2 acceptance hook)
- [ ] tick()/setRateHz/setShape/setNoiseSource noexcept; state is POD (ADR-020 S14)
- [ ] ctest --preset default -R lfo_header --no-tests=error passes; test lfo_header_api asserts enumerator count and signatures

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R lfo_header --no-tests=error
```
