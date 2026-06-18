<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 005
title: mwcore static-lib target + no-JUCE-in-core build guard
status: in-review
depends-on: [001, 004]
component: infra
estimated-size: S
stream: infra
tag: core
---

## Objective

Create the core/CMakeLists.txt defining the mwcore pure-C++20 static lib (linking mw_fp_discipline) and the build/CI guard that FAILS if any core TU includes <juce_*>/references JUCE_ or if mwcore's link closure contains a JUCE target.

## Context

- `docs/design/11 §13.6` — read first
- `ADR-014 C11` — read first
- `ADR-001 C1` — read first
- `docs/design/00 §3.3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- core/CMakeLists.txt: add_library(mwcore STATIC ...) pure C++20, links mw_fp_discipline, zero JUCE include/link
- build guard scanning core/ TUs for <juce_*> / JUCE_ and inspecting the mwcore link/include closure for JUCE targets
- guard registered so it FAILS the build (not advisory)

## Out of scope

- core DSP/voice/fx source (other streams)
- the AudioThreadGuard runtime fixture (infra-10)
- the fp-flag grep guard (infra-12)

## Acceptance criteria

- [ ] mwcore is a pure C++20 static lib with zero JUCE include/link dependency, linking mw_fp_discipline [docs/design/00 §3.3; ADR-001 C1]
- [ ] any core/ TU including <juce_*> or referencing JUCE_ FAILS the build guard [docs/design/11 §13.6; ADR-014 C11]
- [ ] the mwcore link/include closure containing any JUCE target FAILS the guard [docs/design/11 §13.6; ADR-001 C1]

## Verification commands

```
cmake --preset default
cmake --build --preset default
```
