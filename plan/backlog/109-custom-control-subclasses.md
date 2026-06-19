<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 109
title: Custom control subclasses (Rotary/Linear sliders, ToggleSwitch, ChoiceSelector)
status: done
depends-on: [006, 106, 108]
component: ui
estimated-size: M
stream: ui
tag: ui_controls
---

## Objective

Implement thin Slider/Button/ComboBox subclasses that carry their own label/value formatting, repaint only their own bounds, and fence sound_ext options visually via the extensionTag token.

## Context

- `docs/design/10-ui.md §6.3` — read first
- `docs/design/10-ui.md §7.3` — read first
- `ADR-008 §7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_controls`.

## Scope

- ui/controls/RotarySlider.h/.cpp, LinearSlider.h/.cpp, ToggleSwitch.h/.cpp, ChoiceSelector.h/.cpp
- Value text drawn from APVTS NormalisableRange display string
- ChoiceSelector renders software-only entries with the extensionTag token
- Per-control dirty-rect repaint (own bounds only)

## Out of scope

- APVTS attachment wiring (done in modules)
- LookAndFeel drawing (ui-2)

## Acceptance criteria

- [ ] Each control invalidates only its own bounds on value change, never whole-editor (§7.3, ADR-015 C7)
- [ ] ChoiceSelector visually marks sound_ext entries with extensionTag (§6.3, ADR-008 §7/C6/C15)
- [ ] Value text derives from the parameter's display string, not hard-coded (§6.3)
- [ ] Tests named ui_controls* assert dirty-rect scope and extension fencing; verify is 'ctest --preset default -R ui_controls --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_controls --no-tests=error
```
