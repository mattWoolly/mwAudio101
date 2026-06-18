<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 113
title: Single juce_add_plugin target over the shared processor (plugin/CMakeLists.txt)
status: todo
depends-on: [001, 096, 111]
component: infra
estimated-size: S
stream: plugin
tag: wrappers
---

## Objective

Define the single juce_add_plugin target (VST3/AU/Standalone native + CLAP via clap-juce-extensions + LV2 via JUCE native exporter) over the one MwAudioProcessor, with formats supplied by the cmake/Formats.cmake resolution.

## Context

- `docs/design/09 §2.1` — read first
- `docs/design/09 §3.1` — read first
- `ADR-011 Decision` — read first
- `ADR-024 C1, C5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- One juce_add_plugin consuming plugin-2's resolved FORMATS list; CLAP via clap-juce-extensions on the same processor
- LV2 emitted by JUCE native exporter behind MW_BUILD_LV2; no clap-wrapper (ADR-024 C1)
- No DSP fork; every wrapper differs only in plugin/ (ADR-011 Decision)
- Link mwcore + mwplugin

## Out of scope

- Format resolution/gate (plugin-2)
- Validator location (plugin-1)
- Processor implementation (plugin-13)
- Dependency pinning (build-skeleton)

## Acceptance criteria

- [ ] All requested formats build from one juce_add_plugin over the shared MwAudioProcessor with no DSP fork (§3.1; ADR-011 Decision; ADR-024 C5)
- [ ] LV2 is emitted by the JUCE native exporter only; no clap-wrapper is vendored/invoked (ADR-024 C1)
- [ ] CLAP target is produced via clap-juce-extensions wrapping the same processor (§2.1)

## Verification commands

```
cmake --preset default
cmake --build --preset default
```
