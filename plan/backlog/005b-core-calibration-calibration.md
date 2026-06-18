<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 005b
title: core/calibration/Calibration.h — single cross-module (PI) constants table + per-renderVersion frozen constant-set registry
status: todo
depends-on: [005]
component: core
estimated-size: M
stream: calibration
tag: calibration
---

## Objective

Create core/calibration/Calibration.h as THE single cross-module (PI) constants table referenced by docs 00/03/08/11 and the bless MANIFEST, and establish the per-renderVersion FROZEN constant-set registry so every downstream DSP module reads its tunable defaults from one header and no call site inlines a (PI) literal.

## Context

- `docs/design/00 §8.3` — read first
- `docs/design/00 §8.1` — read first
- `docs/design/03 §2.4/§3.5/§4.3` — read first
- `docs/design/08` — read first
- `docs/design/11 §13.5` — read first
- `ADR-023 V10` — read first
- `ADR-023 V18` — read first
- `ADR-023 §Consequences` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `calibration`.

## Scope

- core/calibration/Calibration.h: the one constexpr (PI) constants table organized by subsystem namespace (vco/vcf/env/lfo/vca/vel/drift/warmup) so later tasks (033 FastTanh, 035 FilterTables, 029 VCO, 049 env/lfo/vca block) append their constants here rather than inlining literals (§9 F-15)
- Per-renderVersion FROZEN constant-set registry type keyed by renderVersion: a constexpr table associating each shipped renderVersion with its frozen constant set (tanh approximation coeffs, decimator/halfband coeffs, compensation-table source constants) per §8.3
- Each constant carries a (PI) tunable-default doc comment; no measured-spec assertion; the CURRENT renderVersion entry is authoritative and matches kCurrentRenderVersion
- Static-assert/test that exactly one CURRENT entry exists, that the registry is keyed by renderVersion, and that only shipped renderVersions are retained (§8.3; ADR-023 §Consequences)

## Out of scope

- The prepareToPlay constant-set SELECTOR logic (049c)
- The actual subsystem constant VALUES for env/lfo/vca (049), filter (033/035), osc (029) — those tasks append into this header
- kCurrentRenderVersion definition (lives in EngineVersion.h / 016) — this header references it

## Acceptance criteria

- [ ] core/calibration/Calibration.h compiles into mwcore with zero JUCE include/link (links mw_fp_discipline) [docs/design/00 §9.3; ADR-014 C11]
- [ ] A registry keyed by renderVersion exists with exactly one CURRENT entry, asserted at compile time; tagged 'cal' so the cal name-prefix discovery gate has >=1 test [docs/design/11 §13.5; ADR-013 C2]
- [ ] Every constant in the table is constexpr and carries a (PI) tunable-default comment; a guard/test confirms no measured-spec assertion is attached [docs/design/00 §8.3]
- [ ] Only shipped renderVersions are retained in the registry; adding an unshipped renderVersion entry FAILS the test [ADR-023 §Consequences]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R calibration --no-tests=error
```
