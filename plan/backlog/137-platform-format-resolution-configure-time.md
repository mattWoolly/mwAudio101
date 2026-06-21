<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 137
title: Per-platform format resolution + configure-time validator gate (Formats.cmake)
status: done
depends-on: [001, 113]
component: infra
estimated-size: M
stream: integration
tag: cmake_formats
---

## Objective

Implement cmake/Formats.cmake resolving MWAUDIO_FORMATS per platform, mapping each (format,platform) to its required validator target, hard-removing unwired formats, and FATAL_ERROR on any forced-unwired/excluded format.

## Context

- `docs/design/09 §2.2` — read first
- `docs/design/09 §2.1` — read first
- `plan/decisions/011 Contract C1-C8` — read first
- `plan/decisions/024 Decision` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- cmake/Formats.cmake: per-platform format sets (macOS VST3+AU+CLAP+Standalone; Linux VST3+CLAP+Standalone+LV2-goal; Windows VST3+CLAP+Standalone)
- Map each (format,platform) pair to its required validator target; hard-remove unwired
- message(FATAL_ERROR ...) on AU off-macOS, any AAX, and forced unwired formats
- LV2 behind MW_BUILD_LV2; absence never blocks the Linux gate

## Out of scope

- Locating/declaring the validator targets (integration-8)
- juce_add_plugin target itself (format-wrappers)
- CMakePresets format lists (build-skeleton owns presets; this consumes them)

## Acceptance criteria

- [ ] Configuring AU on Linux/Windows is a configure-time error per §2.2 F-3 / ADR-011 C3
- [ ] Configuring any AAX target on any platform is a configure-time error per §2.2 F-4 / ADR-011 C4
- [ ] Force-adding a format whose validator is unwired for the platform fails configure per §2.2 F-5 / ADR-011 C5
- [ ] ctest --preset default -R cmake_formats --no-tests=error is green; test names begin with cmake_formats

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R cmake_formats --no-tests=error
```
