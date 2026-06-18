<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 121
title: SourceMixerModule (saw/pulse/sub/noise levels)
status: todo
depends-on: [020, 111, 006, 109, 117]
component: ui
estimated-size: S
stream: ui
tag: ui_mixer
---

## Objective

Implement the SOURCE MIXER module binding saw, pulse, sub, and noise level controls via APVTS attachments.

## Context

- `docs/design/10-ui.md §5.3` — read first
- `docs/design/10-ui.md §8.1` — read first
- `ADR-015 C3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_mixer`.

## Scope

- ui/modules/SourceMixerModule.h/.cpp
- Saw / pulse / sub.level / noise level sliders per §5.3 table
- APVTS SliderAttachments from ParamIds
- layoutDesignUnits()

## Out of scope

- VCO source generation controls (ui-9)
- editor layout (ui-5)

## Acceptance criteria

- [ ] All level controls bind via APVTS attachments using schema ParamIds (§8.1, ADR-015 C3)
- [ ] layoutDesignUnits positions children in design units only (§5.3)
- [ ] Editor makes no direct DSP call (§8.1, ADR-015 C3)
- [ ] Tests named ui_mixer* assert control->APVTS binding round-trip; verify is 'ctest --preset default -R ui_mixer --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_mixer --no-tests=error
```
