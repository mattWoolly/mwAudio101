<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 117
title: ModuleBase + ModulatorModule (LFO/S&H + mod depth) with APVTS attachments
status: todo
depends-on: [020, 111, 006, 106, 108, 109]
component: ui
estimated-size: M
stream: ui
tag: ui_modulator
---

## Objective

Provide the ModuleBase abstraction (layoutDesignUnits + APVTS ref) and the ModulatorModule binding LFO rate/shape and mod-depth controls via APVTS attachments using schema-owned ParamIds.

## Context

- `docs/design/10-ui.md §5.3` — read first
- `docs/design/10-ui.md §8.1` — read first
- `ADR-015 C3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_modulator`.

## Scope

- ui/modules/ModuleBase.h (shared base per §5.3)
- ui/modules/ModulatorModule.h/.cpp with LFO rate/shape, S&H, mod-depth controls
- APVTS SliderAttachment/ComboBoxAttachment created in construction from ParamIds (no raw mw101 literals)
- Sine LFO shape marked as sound_ext via ChoiceSelector
- layoutDesignUnits() in design units

## Out of scope

- editor root layout (ui-5)
- minting parameter IDs (schema-owned)

## Acceptance criteria

- [ ] Every control binds via an APVTS attachment using schema ParamIds, no direct DSP call (§8.1, ADR-015 C3)
- [ ] Sine LFO shape is visually fenced as a software extension (§5.3, ADR-008 §7/C6/C15)
- [ ] layoutDesignUnits positions children in design units only (§5.3)
- [ ] Tests named ui_modulator* assert control->APVTS write and host->control move; verify is 'ctest --preset default -R ui_modulator --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_modulator --no-tests=error
```
