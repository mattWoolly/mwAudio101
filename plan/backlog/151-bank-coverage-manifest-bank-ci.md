<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 151
title: Bank coverage manifest + full-bank CI validation (~64 presets)
status: in-review
depends-on: [118, 025, 145, 146, 147, 148, 149, 150]
component: qa
estimated-size: S
stream: presets
tag: presets_bank
---

## Objective

Add a bank-level coverage test that asserts the whole factory bank (~64 presets across all 6 categories) is complete, traceable, honesty-compliant, and survives the migration chain.

> **Follow-up folded in (2026-06-19):** PresetSeqArpRiffTest (task 150) ships WITHOUT a permanent in-suite negative control (the dev agent proved non-vacuity by manual on-disk corruption, not a committed TEST_CASE), unlike the sibling category tests. As part of the full-bank validation, ensure every category test (incl. SeqArpRiff) has a committed negative-control case (inject accent / out-of-range / sound_ext-mismatch -> assert reject). Loader rejection itself is already proven in PresetFormatTest (025).

## Context

- `plan/decisions/008-parameter-state-preset-schema.md C13,C14,C16,C17,C18` — read first
- `docs/research/11-cultural-influence.md §7.1` — read first
- `docs/research/11-cultural-influence.md §6,§7.3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `presets_bank`.

## Scope

- Test that enumerates presets/ recursively and asserts total preset count is ~64 and each of the 6 categories is non-empty
- Assert every preset's meta.category matches its folder and is a valid enum value (008 §C14); CI mirrors presets/ 1:1 (008 §C18)
- Assert no preset metadata anywhere contains a 'TB-303 filter' descriptor or an 'as used on track X' phrasing (008 §C16, §7.3)
- Assert every preset round-trips through the SAME migration chain as session state (008 §C17)
- Assert every preset is traceable: meta has a non-empty description/tags referencing a §7.1 idiom; sound_ext presets only use software-only features

## Out of scope

- Authoring any preset content (presets-1..7)
- The migration chain implementation (preset-format)

## Acceptance criteria

- [ ] ctest --preset default -R presets_bank --no-tests=error passes; test names begin with presets_bank
- [ ] Bank totals ~64 presets with all 6 categories populated (§7.1)
- [ ] Whole-bank scan finds zero 'TB-303 filter' / 'as used on track X' violations (008 §C16)
- [ ] Every preset migrates and loads at CURRENT schemaVersion with no per-preset edits (008 §C17)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presets_bank --no-tests=error
```
