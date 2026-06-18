<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 026
title: PolyBLEP closed-form residual (header-only)
status: in-review
depends-on: [001, 006]
component: core
estimated-size: S
stream: core-osc
tag: polyblep
---

## Objective

Implement the stateless, header-only two-segment closed-form PolyBLEP residual exactly as the ADR-002 Contract specifies.

## Context

- `01-dsp-oscillators.md §3.1` — read first
- `01-dsp-oscillators.md §10` — read first
- `ADR-002 Contract C1-C2` — read first
- `ADR-002 Contract (PolyBLEP residual)` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `polyblep`.

## Scope

- Create core/dsp/PolyBlep.h with constexpr noexcept polyBlep(float t, float dt) in namespace mw101::dsp
- Leading segment t<dt: 2*(t/dt) - (t/dt)^2 - 1
- Trailing segment t>1-dt: ((t-1)/dt)^2 + 2*((t-1)/dt) + 1; else 0.0f
- Fully inlineable, no state, no allocation

## Out of scope

- minBLEP table/applicator (core-osc-2)
- Any oscillator waveform construction (core-osc-3+)

## Acceptance criteria

- [ ] polyblep-* tests assert leading-segment value 2*(t/dt)-(t/dt)^2-1 for t<dt to float tolerance [§3.1, §10]
- [ ] trailing-segment value ((t-1)/dt)^2+2*((t-1)/dt)+1 for t>1-dt and exactly 0.0f in the interior [§3.1, §10]
- [ ] function is constexpr and noexcept (compile-time evaluable) per the Contract [ADR-002 Contract]
- [ ] ctest --preset default -R polyblep --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R polyblep --no-tests=error
```
