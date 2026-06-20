<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 122
title: VcfModule (cutoff, resonance, env amount, kbd track, mod)
status: in-review
depends-on: [020, 111, 006, 109, 117]
component: ui
estimated-size: S
stream: ui
tag: ui_vcf
---

## Objective

Implement the VCF module binding cutoff, resonance, envelope amount, keyboard tracking, and modulation controls via APVTS attachments.

## Context

- `docs/design/10-ui.md §5.3` — read first
- `docs/design/10-ui.md §8.1` — read first
- `ADR-015 C3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_vcf`.

## Scope

- ui/modules/VcfModule.h/.cpp
- Cutoff, resonance, env amount, kbd track, mod controls per §5.3 table
- APVTS attachments from ParamIds
- layoutDesignUnits()

## Out of scope

- VCA/envelope controls (ui-12)
- editor layout (ui-5)

## Acceptance criteria

- [ ] All controls bind via APVTS attachments using schema ParamIds (§8.1, ADR-015 C3)
- [ ] layoutDesignUnits positions children in design units only (§5.3)
- [ ] Host automation moves controls via attachment only, no engine polling (§8.2, ADR-015 C4)
- [ ] Tests named ui_vcf* assert control->APVTS and host->control round-trip; verify is 'ctest --preset default -R ui_vcf --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_vcf --no-tests=error
```
