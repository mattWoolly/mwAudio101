<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 144b
title: presets/ flat-POD bake loader contract — deterministic build/load-time bake, never parsed on the audio thread
status: done
depends-on: [025, 040]
component: core
estimated-size: M
stream: presets
tag: presets
---

## Objective

Implement the presets/ deterministic loader that bakes the ~64 patches into a flat POD table at build/load time, with the contract that the POD table is the only thing the audio thread ever touches — JSON/preset parsing happens at build or load, never on the audio thread.

## Context

- `docs/design/11 §9.1` — read first
- `ADR-001 C3` — read first
- `ADR-001 C4` — read first
- `ADR-014 C9` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `presets`.

## Scope

- presets/ baker: project each .mw101preset (via PresetFormat 025) into a flat POD table entry at build/load time; the table is contiguous POD with no pointers/strings parsed at runtime (§9.1; ADR-014 C9)
- Each baked entry carries its schemaVersion and a checksum (SHA-256 via 040) computed at bake time so loads are verifiable without re-parsing
- Expose a load path that hands the audio thread only the POD table; any parse/projection path is fenced out of the hot path (ADR-001 C3/C4)
- Deterministic ordering and stable byte layout so the bake is reproducible across the bless/Linux/Windows boxes

## Out of scope

- The presets_roundtrip ctest assertions (025b)
- Authoring preset CONTENT / categories (131,144,145-150)
- PresetManager in-memory bank semantics (119) — this feeds it the baked POD table

## Acceptance criteria

- [ ] The audio thread receives only the flat POD table; an AudioThreadGuard-fenced test confirms no JSON/preset parse occurs on the hot path [docs/design/11 §9.1; ADR-001 C3/C4]
- [ ] Each baked entry stores schemaVersion + SHA-256 checksum computed at bake time (uses 040) [ADR-014 C9]
- [ ] The bake is deterministic: re-baking the same patch set yields a byte-identical POD table [docs/design/11 §9.1]
- [ ] Tests named presets_bake* are discovered and pass via 'ctest --preset default -R presets_bake --no-tests=error' [ADR-013 C1]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presets --no-tests=error
```
