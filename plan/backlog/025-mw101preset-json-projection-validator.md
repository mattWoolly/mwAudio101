<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 025
title: .mw101preset JSON projection + validator (PresetFormat.h/.cpp)
status: done
depends-on: [019, 023, 022, 007]
component: core
estimated-size: M
stream: params
tag: presetfmt
---

## Objective

Implement loadPresetJson/writePresetJson projecting the .mw101preset JSON to/from the canonical ValueTree, with the §6.4 validation rules and the §6.5 category enum.

## Context

- `docs/design/06-parameters-state-presets.md §6.1` — read first
- `§6.2` — read first
- `§6.3` — read first
- `§6.4` — read first
- `§6.5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `presetfmt`.

## Scope

- core/preset/PresetFormat.h/.cpp: PresetMeta, loadPresetJson (JSON->canonical tree, then nullopt on malformed/validation failure -> L11), writePresetJson (canonical+meta->JSON) per §6.3
- JSON schema per §6.2 (schemaVersion, meta, params, seq, arp); per-step note/gate/tie/rest only, no accent (§6.2; §6.4)
- Validation (§6.4): schemaVersion+meta.name/author/category present; category in §6.5 enum; every registry ID present and in NormalisableRange; choice index<choiceCount; sound_ext==true iff vco.range>=4 or lfo.shape==4; reject accent field
- Attribution discipline check: inspired_by null-or-reference, no 'TB-303 filter' descriptor, no 'as used on track X' phrasing (§6.4)
- Tests: a valid preset round-trips through the canonical tree; each validation rule rejects a crafted-bad preset; sound_ext is forced by software-ext indices

## Out of scope

- the in-memory bank / BinaryData embedding (params-13)
- the CI mirror of presets/ (params-13)
- session-blob (de)serialization (params-7)

## Acceptance criteria

- [ ] loadPresetJson projects valid JSON to the canonical tree and returns nullopt on malformed JSON / failed validation [§6.3; §6.4]
- [ ] Validation enforces all §6.4 rules: registry completeness + range, category enum (§6.5), sound_ext iff software-ext index used, no accent, attribution discipline [§6.4]
- [ ] writePresetJson emits §6.2-shaped JSON with note/gate/tie/rest-only steps [§6.2]
- [ ] Test names begin with presetfmt and assert valid round-trip plus a rejection per §6.4 rule [§6.4]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presetfmt --no-tests=error
```
