<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 062
title: Env/LFO/VCA real-time safety and control-rate determinism suite
status: done
depends-on: [007, 006, 058, 059, 060, 057]
component: qa
estimated-size: S
stream: core-env-lfo-vca
tag: envlfovca_rtsafe
---

## Objective

Add the cross-class suite asserting Envelope/Lfo/Vca/ModRouting hot paths allocate/lock nothing and that envelope/LFO control-rate block-boundary bookkeeping is deterministic.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §2.1` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §6.2` — read first
- `plan/decisions/020-parameter-smoothing-policy.md S14` — read first
- `plan/decisions/020-parameter-smoothing-policy.md S12` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `envlfovca_rtsafe`.

## Scope

- Under AudioThreadGuard, run tick/process/processBlock/combine for all four types asserting no heap alloc and no locks (§2.1, ADR-020 S14)
- Assert hot paths are noexcept and all state sized in prepare (§2.1)
- Assert envelope/LFO advance on the control-rate tick, sample-accurate at block boundaries (§6.2)
- Assert block-boundary bookkeeping is reproducible (CLASS-EXACT integer/index path) (§6.2, S12)
- Confirm single shared Envelope feeds VCF/VCA/PWM (no separate filter/amp EG) (§2.1 acceptance hook)

## Out of scope

- macOS/Linux bless comparison run (golden-harness)
- Algorithm correctness (covered by per-core tasks)

## Acceptance criteria

- [ ] Envelope/Lfo/Vca/ModRouting perform no heap alloc and no locks in hot paths; noexcept; state sized in prepare (§2.1 acceptance hook)
- [ ] Envelope/LFO advance on control-rate tick; boundary bookkeeping deterministic (§6.2 acceptance hook, S12)
- [ ] ctest --preset default -R envlfovca_rtsafe --no-tests=error passes; tests named envlfovca_rtsafe_*

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R envlfovca_rtsafe --no-tests=error
```
