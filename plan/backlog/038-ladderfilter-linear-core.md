<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 038
title: LadderFilter linear core: 4-stage Huovilainen cascade + cutoff mapping (no resonance)
status: todo
depends-on: [001, 006, 007, 033, 035]
component: core
estimated-size: M
stream: core-filter
tag: vcf-core
---

## Objective

Create the LadderFilter class (full public header) and implement prepare/reset, the cutoff setters, state layout, and the forward-Euler four-stage tanh cascade with feedback gain k forced to zero, validating topology, slope, fixed-cost, and RT-safety before resonance is wired.

## Context

- `02-dsp-filter.md §3` — read first
- `02-dsp-filter.md §5.2` — read first
- `02-dsp-filter.md §5.5` — read first
- `ADR-003 F-01` — read first
- `ADR-003 F-02` — read first
- `ADR-003 F-11` — read first
- `ADR-003 F-12` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vcf-core`.

## Scope

- core/dsp/LadderFilter.h: full public interface per §3.1 (prepare(fsOsHz,maxBlockOs), reset, setCutoffCv, setCutoffHz, setResonance stub, processSample, processBlock, makeUpGain, loopGainK) and §3.2 state layout
- LadderFilter.cpp: prepare builds/owns FilterTables (the only allocator) and resets state; reset flushes integrators to anti-denormal bias (§3.1, F-11, F-12)
- setCutoffCv/setCutoffHz map via FilterTables (table lookup, no transcendental) (§5.2, F-10)
- processSample/processBlock: four cascaded one-poles y_[i]+=g*(w_[i-1]-w_[i]) with fastTanhKnee, k=0 path; anti-denormal bias maintained (§5.5 steps 3/5, F-01/F-02)

## Out of scope

- Inverting feedback, two-sample phase comp, diode clamp, self-osc, make-up gain (all core-filter-5)
- Oversampling / running at fs_os (zone is core-filter-8; this consumes fs_os as a prepare arg)

## Acceptance criteria

- [ ] vcf-core test: at k=0 the magnitude rolls off 24 dB/oct one octave above cutoff (§10 F-01)
- [ ] vcf-core test: processSample cost/branch count is data-independent across input amplitudes; no Newton iteration (§10 F-02)
- [ ] vcf-core test: AudioThreadGuard confirms prepare/reset/processSample/processBlock allocate no heap and take no lock; FilterTables::build is the only allocator (§10 F-11)
- [ ] vcf-core test: drive-to-silence produces no subnormals in any integrator state or output with FTZ/DAZ + bias (§10 F-12)
- [ ] verify: ctest --preset default -R vcf-core --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vcf-core --no-tests=error
```
