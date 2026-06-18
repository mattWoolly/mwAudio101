<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 044
title: Provenance — honesty-label vocabulary and renderVersion governor
status: todo
depends-on: [001, 006, 043]
component: qa
estimated-size: S
stream: golden
tag: provenance
---

## Objective

Implement the honesty-label types (LabelKind + HonestyLabel with ledgerRef) and the renderVersion governance helper that decides when a bless must bump renderVersion.

## Context

- `docs/design/11 §7.4` — read first
- `docs/design/11 §7.6` — read first
- `ADR-013 C14` — read first
- `ADR-023 V5` — read first
- `ADR-023 V6` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `provenance`.

## Scope

- tools/bless/Provenance.h/.cpp: enum LabelKind{CloneDerived,ReverseEngineered,TheoryInference,CommunityDisassembly,ServiceManual,Disputed,SoftwareEmulationArtifact}, struct HonestyLabel{kind,ledgerRef}
- Helper: given old vs new artifact (hash for EXACT / tolerance-band move for FP), decide renderVersion-bump-required
- Map each LabelKind to its controlled ledger reference for validation

## Out of scope

- MANIFEST file I/O (golden-8)
- The bless CLI/refusal flow (golden-10)

## Acceptance criteria

- [ ] mw101.manifest a changed CLASS-EXACT hash requires a renderVersion bump; an unchanged artifact does not (paired) [ADR-023 V5]
- [ ] A CLASS-FP artifact moved outside its tolerance band requires a bump; inside-band does not [ADR-023 V5]
- [ ] Each LabelKind resolves to a valid ledger §2-§8 reference string [docs/design/11 §7.4]
- [ ] verify: ctest --preset default -R manifest --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R provenance --no-tests=error
```
