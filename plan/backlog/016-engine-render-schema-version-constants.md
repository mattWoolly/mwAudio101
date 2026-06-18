<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 016
title: Engine/render/schema version constants (EngineVersion.h)
status: done
depends-on: [001, 006]
component: core
estimated-size: S
stream: params
tag: engineversion
---

## Objective

Define kCurrentSchemaVersion, kCurrentRenderVersion, kEngineVersion and kPluginVersion constants in the mw101::version namespace.

## Context

- `docs/design/06-parameters-state-presets.md §9.1` — read first
- `§5.1` — read first
- `§9.2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `engineversion`.

## Scope

- core/version/EngineVersion.h with the four constants exactly as §9.1 (schema=1, render=1, engineVersion="1.0.0", pluginVersion="1.0.0")
- Doc comments noting renderVersion bumps on bless-change and is orthogonal to schemaVersion (§9.2)
- Test asserting the constant values and types

## Out of scope

- the migration chain (params-9)
- renderVersion load-time opt-in logic (params-12)
- bless-tool governance (golden-harness stream)

## Acceptance criteria

- [ ] kCurrentSchemaVersion==1, kCurrentRenderVersion==1, kEngineVersion=="1.0.0", kPluginVersion=="1.0.0" [§9.1]
- [ ] engineVersion documented as informational/non-migrating; renderVersion orthogonal to schemaVersion [§9.2]
- [ ] Test names begin with engineversion and assert the four constants [§9.1]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R engineversion --no-tests=error
```
