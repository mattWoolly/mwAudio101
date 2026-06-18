<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 004
title: mw_fp_discipline INTERFACE target (CompilerFlags.cmake)
status: done
depends-on: [001]
component: infra
estimated-size: S
stream: infra
tag: build
---

## Objective

Define the single cmake/CompilerFlags.cmake mw_fp_discipline INTERFACE target carrying the FROZEN per-toolchain FP flags, and link it from mwcore.

## Context

- `docs/design/11 §11` — read first
- `ADR-014 C4` — read first
- `ADR-001 C12` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- cmake/CompilerFlags.cmake defining INTERFACE target mw_fp_discipline
- GCC/Clang/AppleClang: -fno-fast-math -ffp-contract=off -fno-finite-math-only -fno-associative-math -fno-reciprocal-math -fexcess-precision=standard -fdenormal-fp-math=ieee; never -ffast-math/-Ofast
- MSVC: /fp:precise /fp:contract-; never /fp:fast
- link mw_fp_discipline from mwcore (and expose for plugin DSP TUs + tests)

## Out of scope

- the runtime/grep fp_discipline_guard ctest (infra-12)
- FTZ/DAZ runtime flush in process (engine/voice stream)
- DSP source files (other streams)

## Acceptance criteria

- [ ] one INTERFACE target mw_fp_discipline carries exactly the §11 frozen flags per toolchain [docs/design/11 §11; ADR-014 C4]
- [ ] mwcore links mw_fp_discipline [docs/design/11 §9.1; ADR-014 C4]
- [ ] no -ffast-math/-Ofast (GCC/Clang) or /fp:fast (MSVC) is ever added [docs/design/11 §11; ADR-001 C12]

## Verification commands

```
cmake --preset default
cmake --build --preset default
```
