<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 025b
title: presets_roundtrip ctest — every preset round-trips schema + checksum
status: todo
depends-on: [025, 119, 040, 144b]
component: qa
estimated-size: S
stream: presets
tag: presets
---

## Objective

Add the presets_roundtrip ctest that round-trips every patch in the ~64-preset bank through the PresetFormat projection and the flat-POD bake, asserting schema and checksum survive the round-trip so no preset silently corrupts on save/load.

## Context

- `docs/design/11 §9.1` — read first
- `ADR-014 C9` — read first
- `ADR-001 C9` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `presets`.

## Scope

- presets_roundtrip ctest: for every patch in the bank, project -> serialize -> reload -> re-project and assert the schemaVersion and SHA-256 checksum (040) match end to end (§9.1; ADR-014 C9)
- Assert the reloaded POD entry is byte-identical to the baked entry (round-trip is lossless on the POD path) and that the load path never parses on the audio thread (re-affirms 144b contract)
- Carry --output-on-failure and --no-tests=error per the testPresets convention [ADR-014 C7]
- Cover INIT/baseline (144) and at least one patch per category so the gate fails if any category breaks the schema/checksum contract

## Out of scope

- The bake loader implementation itself (144b)
- Bank coverage manifest / full ~64-preset CI validation (151)
- Cross-platform bit-exactness of DSP output (139)

## Acceptance criteria

- [ ] Every patch round-trips with matching schemaVersion + SHA-256 checksum; any mismatch FAILS [docs/design/11 §9.1; ADR-014 C9]
- [ ] The reloaded POD entry is byte-identical to the baked entry [ADR-001 C9]
- [ ] Test target named presets_roundtrip is discovered and runs via 'ctest --preset default -R presets_roundtrip --no-tests=error' with --output-on-failure [ADR-014 C7]
- [ ] A deliberately corrupted checksum or bumped schema on one patch causes the ctest to FAIL (negative control) [ADR-013 C4]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presets --no-tests=error
```
