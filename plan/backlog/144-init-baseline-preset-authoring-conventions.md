<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 144
title: INIT/baseline preset + authoring conventions for the ~64-preset bank
status: in-review
depends-on: [118, 025]
component: docs
estimated-size: S
stream: presets
tag: presets_baseline
---

## Objective

Author the factory INIT/.mw101preset baseline and a short authoring convention note that every category task follows, so all presets are built against the ratified modern-default poles and the honesty discipline.

## Context

- `plan/decisions/016-owner-ratifications-2026-06-18.md R-1..R-4` — read first
- `plan/decisions/008-parameter-state-preset-schema.md C13,C14,C15,C16` — read first
- `docs/research/11-cultural-influence.md §7.1,§7.3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- presets/INIT.mw101preset projecting the canonical ValueTree: MODERN-SMOOTH control, velocity ON->VCA/VCF, MONO voice mode, subtle analog drift (Age low) per ADR-016 R-1..R-4
- FX engine-default OFF in INIT (ADR-016 accepted-without-veto)
- meta block populated: name, author, category, tags, description, inspired_by:null, sound_ext:false
- A concise authoring conventions doc (presets/AUTHORING.md) listing the 6 valid categories, the inspired-by/disputed rule, the no-'TB-303 filter' rule, and the sound_ext rule
- Test asserting INIT loads through the loader, every param in range, and matches the R-1..R-4 default poles

## Out of scope

- The loader/validator/schema themselves (preset-format)
- Any category preset content

## Acceptance criteria

- [ ] ctest --preset default -R presets_baseline --no-tests=error passes; test names begin with presets_baseline
- [ ] INIT.mw101preset validates against the registry: all IDs present, values in range, choice indices valid (008 §C18)
- [ ] INIT selects MODERN control, velocity ON, MONO, drift Age-low, FX OFF (016 R-1..R-4 + accepted-without-veto)
- [ ] meta uses a valid category enum value and sound_ext:false (008 §C14/§C15)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presets_baseline --no-tests=error
```
