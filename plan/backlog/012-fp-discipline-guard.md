<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 012
title: fp_discipline_guard — compile_commands.json forbidden-flag grep ctest
status: done
depends-on: [004, 006]
component: qa
estimated-size: S
stream: infra
tag: fp
---

## Objective

Implement the fp_discipline_guard ctest that greps compile_commands.json for forbidden FP flags on every golden/DSP target and FAILS the build if any appears.

## Context

- `docs/design/11 §13.4` — read first
- `ADR-014 C5` — read first
- `ADR-013 C20` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `fp`.

## Scope

- tests/invariants/FpDisciplineCheck registered as ctest under the fp tag
- grep compile_commands.json for -ffast-math, -Ofast, /fp:fast, -ffp-contract=fast on golden/DSP targets
- any hit FAILS; paired negative control proving a clean DSP target passes
- verify via ctest --preset default -R fp --no-tests=error

## Out of scope

- the mw_fp_discipline flag definition itself (infra-4)
- runtime FTZ/DAZ flush (engine stream)
- recording fpFlagProof into the bless MANIFEST (golden-harness stream)

## Acceptance criteria

- [ ] a forbidden FP flag (-ffast-math/-Ofast//fp:fast/-ffp-contract=fast) reaching any golden/DSP target FAILS the build mechanically via compile_commands.json grep [docs/design/11 §13.4; ADR-014 C5; ADR-013 C20]
- [ ] a clean DSP target passes (negative control) [docs/design/11 §4.2]
- [ ] the guard is named fp* and runs under ctest --preset default -R fp --no-tests=error [docs/design/11 §8.3]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R fp --no-tests=error
```
