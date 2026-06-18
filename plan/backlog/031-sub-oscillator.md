<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 031
title: Sub-oscillator: 4013 divider + diode-OR 25% pulse
status: todo
depends-on: [001, 006, 007, 026, 027]
component: core
estimated-size: M
stream: core-osc
tag: sub
---

## Objective

Implement SubOscillator as an exact integer divider of the VCO phase (4013 model) with 3-way shape select and the diode-OR (Q1 OR Q2) 25% pulse, band-limiting every resulting edge in temporal order.

## Context

- `01-dsp-oscillators.md §5.1-5.6` — read first
- `ADR-002 Contract C4-C6` — read first
- `01-dsp-oscillators.md §10 (sub phase-lock, 25% duty/diode-OR)` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `sub`.

## Scope

- Create core/dsp/SubOscillator.h/.cpp: SubShape enum, prepare(sampleRate, hqTable), reset(), setShape(), setAaMode(), renderSample(masterPhase, wrapped, freqHz)
- Division: toggle q1_ on each wrapped==true (VCO/2, -1 oct); toggle q2_ on q1_ rising edge (VCO/4, -2 oct)
- Shape outputs: OctDownSquare=Q1, TwoOctDownSquare=Q2, TwoOctDown25Pulse=(Q1||Q2) high 75%/low 25%
- Band-limit every edge (PolyBLEP default, minBLEP HQ/escalated), no BLAMP; derive each edge fraction from the master accumulator and schedule in temporal order
- Output bipolar [-1,+1] pre-level (level applied by mixer, not here)

## Out of scope

- Sub LEVEL scaling and mixing (owned by mixer/param-schema)
- VCO phase/wrap generation (core-osc-3)
- Section wiring/escalation (core-osc-7)

## Acceptance criteria

- [ ] sub-* tests assert sub fundamental is exactly VCO/2 (-1 oct) and VCO/4 (-2 oct) at every footage with zero phase drift over a long run [§5.3, ADR-002 C4]
- [ ] 25% pulse output is high 75%/low 25% of its -2 oct period and its 2nd harmonic is the strongest harmonic [§5.4, ADR-002 C5, §10]
- [ ] sub edges are sample-aligned to the saw wrap and edges scheduled in temporal order (duty/harmonic-ratio assertion) [§5.5, §10]
- [ ] no BLAMP path is exercised; all edges treated as level steps [ADR-002 C6]
- [ ] ctest --preset default -R sub --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R sub --no-tests=error
```
