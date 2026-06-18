<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 098
title: Capability rung enums + ResolvedCapabilities POD (plugin/host/Capabilities.h)
status: done
depends-on: [001]
component: app
estimated-size: S
stream: plugin
tag: capabilities
---

## Objective

Define NoteExpressionRung, TransportRung, PluginFormat, and the ResolvedCapabilities POD exactly per §7.2/§8.2.

## Context

- `docs/design/09 §7.2` — read first
- `docs/design/09 §8.2` — read first
- `ADR-022 §Decision items 1-2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `capabilities`.

## Scope

- NoteExpressionRung {Native, MpeOverMidi, Collapsed}
- TransportRung {SampleAccurate, BlockQuantized, FreeRun}
- PluginFormat {VST3, AU, CLAP, Standalone, LV2}
- ResolvedCapabilities POD {noteExpr, transport}

## Out of scope

- Resolution logic (plugin-11)
- UI publish path (plugin-11)

## Acceptance criteria

- [ ] Enum members and order match §7.2 exactly (tag 'capabilities')
- [ ] ResolvedCapabilities is a trivially copyable POD (§7.2)
- [ ] PluginFormat covers all five wrappers (§8.2)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R capabilities --no-tests=error
```
