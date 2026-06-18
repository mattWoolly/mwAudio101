<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 096
title: Configure-time format gate cmake/Formats.cmake
status: in-review
depends-on: [001, 095]
component: infra
estimated-size: M
stream: plugin
tag: formats
---

## Objective

Create cmake/Formats.cmake that resolves MWAUDIO_FORMATS per platform, maps each (format,platform) to its required validator target, hard-removes unwired formats, and FATAL_ERRORs on any forced unwired or excluded format.

## Context

- `docs/design/09 §2.2 F-1..F-8` — read first
- `ADR-011 C1-C8` — read first
- `ADR-024 C1-C6` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- Resolve requested formats against the plugin-1 wired map; hard-remove any format whose validator is unwired
- message(FATAL_ERROR) on AU off macOS (F-3), on any AAX target (F-4), and on force-adding an unwired format (F-5)
- LV2 gated behind MW_BUILD_LV2, Linux-only, non-blocking (F-6)
- Emit the resolved final format list for the juce_add_plugin target (plugin-13)

## Out of scope

- Locating validators (plugin-1)
- Preset format sets (plugin-3)
- The plugin target definition (plugin-13)

## Acceptance criteria

- [ ] Configuring AU on Linux/Windows is a configure-time error (F-3; ADR-011 C3)
- [ ] Configuring any AAX target on any platform is a configure-time error (F-4; ADR-011 C4; ADR-024 C6)
- [ ] Force-adding a format whose validator is unwired fails the configure (F-5; ADR-011 C5)
- [ ] A test (tag 'formats') drives mock platform/validator inputs and asserts resolved format lists and FATAL conditions per §2.2

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R formats --no-tests=error
```
