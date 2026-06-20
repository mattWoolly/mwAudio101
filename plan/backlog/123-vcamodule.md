<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 123
title: VcaModule (env/gate select, level, env A/D/S/R)
status: in-review
depends-on: [020, 111, 006, 109, 117]
component: ui
estimated-size: S
stream: ui
tag: ui_vca
---

## Objective

Implement the VCA module binding envelope/gate select, level, and envelope A/D/S/R controls via APVTS attachments.

## Context

- `docs/design/10-ui.md §5.3` — read first
- `docs/design/10-ui.md §8.1` — read first
- `ADR-015 C3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_vca`.

## Scope

- ui/modules/VcaModule.h/.cpp
- Env/gate select, level, env A/D/S/R controls per §5.3 table
- APVTS attachments from ParamIds
- layoutDesignUnits()

## Out of scope

- VCF controls (ui-11)
- editor layout (ui-5)

## Acceptance criteria

- [ ] All controls bind via APVTS attachments using schema ParamIds (§8.1, ADR-015 C3)
- [ ] layoutDesignUnits positions children in design units only (§5.3)
- [ ] Editor makes no direct DSP call (§8.1, ADR-015 C3)
- [ ] Tests named ui_vca* assert control->APVTS binding round-trip; verify is 'ctest --preset default -R ui_vca --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_vca --no-tests=error
```
