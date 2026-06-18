<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 131
title: Factory preset corpus + CI registry/mirror validator
status: todo
depends-on: [001, 006, 025, 119]
component: core
estimated-size: M
stream: params
tag: factorypresets
---

## Objective

Author the ~64 factory .mw101preset files organized by §6.5 category and the CI validator that asserts every preset passes registry/category/sound_ext/no-accent/attribution validation and that presets/ mirrors 1:1 into BinaryData.

## Context

- `docs/design/06-parameters-state-presets.md §6.4` — read first
- `§6.5` — read first
- `§10.2` — read first
- `§10.3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `factorypresets`.

## Scope

- presets/<Category>/*.mw101preset across the six §6.5 categories (~64 total), each declaring one category and a complete params block (§6.5; §10.3)
- SeqArpRiff presets capture a stored <seq> note/rest/tie/gate pattern (no accent); FX engaged only where research prescribes (PWMStrings); a 'hardware-accurate' preset sets tune.a4=442 (§10.3)
- CI validator (tools/ or test) running loadPresetJson validation over every file (params-11) and asserting presets/ is mirrored 1:1 into the BinaryData manifest (§6.4; §10.2)
- Validator greps for forbidden 'TB-303 filter' descriptor / 'as used on track X' attribution and rejects any per-step accent (§6.4)
- Tests: every shipped preset validates clean; the mirror check fails when a file is added without a BinaryData entry

## Out of scope

- the loader/validator implementation itself (params-11)
- the bank runtime (params-13)
- DSP voicing accuracy of the patches (subsystem docs)

## Acceptance criteria

- [ ] Every .mw101preset validates against the registry (all IDs present + in range, valid choice index), declares one §6.5 category, sets sound_ext iff a software-only feature is used, carries no accent, contains no 'TB-303 filter' / 'as used on track X' text [§6.4; §6.5]
- [ ] SeqArpRiff presets carry a stored <seq> note/rest/tie/gate pattern; the hardware-accurate preset uses tune.a4=442 [§10.3]
- [ ] CI mirrors presets/ 1:1 into BinaryData and fails on a missing mirror entry [§6.4; §10.2]
- [ ] Test names begin with factorypresets and assert per-file validation + the 1:1 mirror [§6.4]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R factorypresets --no-tests=error
```
