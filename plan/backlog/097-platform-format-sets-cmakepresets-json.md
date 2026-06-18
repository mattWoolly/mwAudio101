<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 097
title: Per-platform format sets in CMakePresets.json
status: done
depends-on: [001, 096]
component: infra
estimated-size: S
stream: plugin
tag: presets
---

## Objective

Add per-platform format sets to CMakePresets.json: macOS = VST3+AU+CLAP+Standalone; Linux = VST3+CLAP+Standalone (+LV2 goal); Windows = VST3+CLAP+Standalone.

## Context

- `docs/design/09 §2.2 (CMakePresets bullet)` — read first
- `ADR-024 Decision (preset scoping)` — read first
- `ADR-011 §Decision platform table` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- Per-platform preset that sets MWAUDIO_FORMATS to the documented set
- MW_BUILD_LV2 enabled only in the Linux preset
- No AAX cell on any platform

## Out of scope

- Format resolution logic (plugin-2)
- Validator location (plugin-1)
- Build-skeleton base preset structure (consumed)

## Acceptance criteria

- [ ] macOS preset requests exactly VST3+AU+CLAP+Standalone (§2.2; ADR-024)
- [ ] Linux preset requests VST3+CLAP+Standalone with MW_BUILD_LV2 on (§2.2)
- [ ] Windows preset requests VST3+CLAP+Standalone with no AAX cell (§2.2)

## Verification commands

```
cmake --preset default
cmake --build --preset default
```
