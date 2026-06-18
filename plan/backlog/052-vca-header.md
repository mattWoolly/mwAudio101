<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 052
title: Vca.h header: VcaMode enum + Vca class layout
status: done
depends-on: [007, 006, 001]
component: core
estimated-size: S
stream: core-env-lfo-vca
tag: vca_header
---

## Objective

Declare the BA662A-class VCA public API and POD state in core/dsp/Vca.h exactly as §4.2, with the ENV/GATE-only VcaMode enum.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §4.1` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §4.2` — read first
- `plan/decisions/020-parameter-smoothing-policy.md S14` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vca_header`.

## Scope

- Declare enum class VcaMode {Env=0,Gate=1} only (no HOLD) (§4.2)
- Declare Vca class: prepare/reset/setMode/setDrive/process/processBlock (§4.2)
- POD private state: mode_, drive_, offsetNull_, gateFade_, gateFadeCoeff_ (§4.2)
- namespace mw101::dsp; process/processBlock noexcept

## Out of scope

- Taper/tanh math (task 9)
- Anti-thump implementation (task 10)
- Control assembly (ModRouting)

## Acceptance criteria

- [ ] VcaMode has exactly Env/Gate; no HOLD enumerator (§4.2)
- [ ] process/processBlock noexcept; state POD (ADR-020 S14)
- [ ] ctest --preset default -R vca_header --no-tests=error passes; test vca_header_api asserts signatures and enum

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vca_header --no-tests=error
```
