<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 060
title: Vca anti-thump gate fade + ENV/GATE mode handling
status: todo
depends-on: [007, 006, 056]
component: core
estimated-size: S
stream: core-env-lfo-vca
tag: vca_thump
---

## Objective

Implement the short one-pole gate fade and DC-offset null at gate edges so onset/offset and the ENV<->GATE switch are click-safe.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §4.6` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §4.4` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §6.5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vca_thump`.

## Scope

- prepare computes gateFadeCoeff_ from kVcaAntiThumpMs (§4.6)
- Apply one-pole gateFade_ on the control at gate open/close edges (§4.6)
- Null residual DC offset with kVcaOffsetNull at the gate transition (§4.6)
- setMode: Env follows ADSR-shaped control; Gate holds flat full level for gate duration (§4.4)
- Share the OnePoleSmoother kind for the fade (ADR-020 S10); ENV<->GATE switch click-safe (§6.5)

## Out of scope

- LFO tremolo summing (ModRouting)
- Velocity scaling (ModRouting)
- ADR-005 crossfade implementation (control-core)

## Acceptance criteria

- [ ] No audible thump on a 0->1 gate edge: energy in the first kVcaAntiThumpMs is bounded (§4.5/§4.6 acceptance hook)
- [ ] ENV mode follows the ADSR contour; GATE mode holds a flat level for the gate duration (§4.4 acceptance hook)
- [ ] ctest --preset default -R vca_thump --no-tests=error passes; tests vca_thump_* cover fade energy bound and ENV/GATE

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vca_thump --no-tests=error
```
