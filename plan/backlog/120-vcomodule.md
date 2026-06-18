<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 120
title: VcoModule (range, waveform mix, PWM, pitch, sub, noise)
status: todo
depends-on: [020, 111, 006, 109, 117]
component: ui
estimated-size: M
stream: ui
tag: ui_vco
---

## Objective

Implement the VCO module binding range, waveform mix, PWM, pitch, sub mode, and noise controls via APVTS attachments, fencing 32'/64' registers as software extensions.

## Context

- `docs/design/10-ui.md §5.3` — read first
- `docs/design/10-ui.md §8.1` — read first
- `ADR-015 C3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_vco`.

## Scope

- ui/modules/VcoModule.h/.cpp
- Controls for range, waveform mix, PWM, pitch, sub mode, noise per §5.3 table
- APVTS attachments from ParamIds
- 32'/64' register choices marked as sound_ext via ChoiceSelector
- layoutDesignUnits()

## Out of scope

- source levels (ui-10)
- editor layout (ui-5)

## Acceptance criteria

- [ ] All controls bind via APVTS attachments using schema ParamIds (§8.1, ADR-015 C3)
- [ ] 32'/64' registers visually fenced as extensions, never as hardware behavior (§5.3, ADR-008 §7/C6/C15)
- [ ] layoutDesignUnits positions children in design units only (§5.3)
- [ ] Tests named ui_vco* assert control->APVTS binding round-trip; verify is 'ctest --preset default -R ui_vco --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_vco --no-tests=error
```
