<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 131
title: Factory preset corpus + CI registry/mirror validator
status: done
depends-on: [001, 006, 025, 119]
component: core
estimated-size: M
stream: params
tag: factorypresets
---

## Objective

> **RE-SCOPED (2026-06-19).** The ~64 factory presets are already AUTHORED by the per-category
> tasks (144 INIT + 145 AcidBassLead + 146 SubBass + 147 Lead + 148 PWMStrings + 149 BlipsFX +
> 150 SeqArpRiff = 64 files, all merged + QA'd). Do NOT re-author them. The full-bank validation
> (every preset validates clean across all categories) is owned by task 151 (bank coverage
> manifest). This task is therefore re-scoped to the RESIDUAL: the **presets/ ↔ BinaryData 1:1
> mirror + CI registry** — the build-time embedding of presets/ into JUCE BinaryData and the
> guard that fails when a preset file is added/removed without a matching BinaryData entry (so
> the shipped bank can never silently diverge from presets/). Coordinate with 113's juce_add_plugin
> (the BinaryData target) and 144b (the flat-POD bake), avoiding a second source of truth.

Originally: author the ~64 presets + the CI validator. Authoring is DONE (144-150); validation is
151. Keep only the 1:1 presets/↔BinaryData mirror/registry here.

## Context

- `docs/design/06-parameters-state-presets.md §6.4` — read first
- `§6.5` — read first
- `§10.2` — read first
- `§10.3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `factorypresets`.

## Scope

- presets/ ↔ BinaryData 1:1 mirror: ensure the build embeds every presets/<Category>/*.mw101preset
  into the JUCE BinaryData target (the one consumed by 113's juce_add_plugin / the 144b bake), and
  add a registry/manifest the build/test can diff against.
- A `factorypresets`-tagged check that the embedded BinaryData set EXACTLY matches the on-disk
  presets/ tree (every file ↔ a BinaryData entry, no orphans either way), failing loudly when a
  preset is added/removed without updating the embedding.
- A `hardware-accurate` preset that sets tune.a4=442 exists somewhere in the bank (verify it is
  present; author it as part of an existing category if missing) (§10.3).

## Out of scope

- Re-authoring the 64 category presets (144-150, DONE) and the full per-file content validation
  (151 owns the full-bank manifest + per-category non-vacuity).
- The loader/validator implementation (025); the flat-POD bake mechanism itself (144b); the bank
  runtime semantics (119).

## Acceptance criteria

- [ ] The build embeds presets/ into BinaryData and a `factorypresets` check asserts the embedded set ↔ on-disk presets/ is EXACTLY 1:1 (no missing/orphan entry), failing on a divergence [§6.4; §10.2]
- [ ] A bank preset uses tune.a4=442 ('hardware-accurate' 442Hz reference) per §10.3 [§10.3]
- [ ] Test names begin with factorypresets and assert the 1:1 mirror [§6.4]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R factorypresets --no-tests=error
```
