<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 053
title: ModRouting.h header: ModDepths/VelocityRouting/ModBus PODs
status: todo
depends-on: [007, 006, 001]
component: core
estimated-size: S
stream: core-env-lfo-vca
tag: modrouting_header
---

## Objective

Declare the fixed-routing combiner PODs (ModDepths, VelocityRouting, ModBus) in core/dsp/ModRouting.h exactly as §5.1.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §5.1` — read first
- `plan/decisions/020-parameter-smoothing-policy.md S14` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `modrouting_header`.

## Scope

- Declare ModDepths struct: lfoToPitch/lfoToCutoff/lfoToPw/lfoToVca/envToCutoff/envToPw/keyFollow (§5.1)
- Declare VelocityRouting struct: enabled=true, toVcaAmount, toCutoffAmount (§5.1)
- Declare ModBus struct: lpState, lpCoeff (§5.1)
- Declare combiner entry points (prepare + per-tick combine) sized in prepare
- namespace mw101::dsp; POD only

## Out of scope

- Velocity/LPF math (task 12)
- Parameter IDs (doc 06)
- Destination DSP (VCO/VCF docs)

## Acceptance criteria

- [ ] Header compiles; all three structs are POD with §5.1 fields/defaults (VelocityRouting.enabled defaults true)
- [ ] ModBus sized in prepare, no heap members (ADR-020 S14)
- [ ] ctest --preset default -R modrouting_header --no-tests=error passes; test modrouting_header_api asserts struct shape

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R modrouting_header --no-tests=error
```
