<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 001
title: Top-level CMakeLists + project skeleton + options
status: in-review
depends-on: []
component: infra
estimated-size: S
stream: infra
tag: build
---

## Objective

Create the top-level CMakeLists.txt establishing project(mwAudio101 CXX), C++20, CMAKE_CXX_EXTENSIONS OFF, the CMake>=3.25 floor with a clear failure message, GPL SPDX header, and the build options/subdir wiring.

## Context

- `docs/design/11 §9.1` — read first
- `docs/design/11 §2.1` — read first
- `ADR-014 Decision (Tree/CMake structure)` — read first
- `ADR-014 C3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- project(mwAudio101 CXX), CMAKE_CXX_STANDARD 20, CMAKE_CXX_EXTENSIONS OFF
- cmake_minimum_required(VERSION 3.25) with a clear floor message on older CMake
- options MW101_TESTS, MW_BUILD_CLAP, MW_BUILD_LV2 plus sanitizer toggles wired via presets (no bare cache edits)
- add_subdirectory stubs for core, plugin, ui, tests (gated on MW101_TESTS), presets — each may be an empty placeholder CMakeLists this task creates
- set CMAKE_EXPORT_COMPILE_COMMANDS ON (for the fp-discipline grep guard)

## Out of scope

- CPM/dependency fetching (infra-3)
- the mw_fp_discipline target definition (infra-4)
- actual core/plugin/ui/tests source content (other streams / later tasks)

## Acceptance criteria

- [ ] Configuring with CMake < 3.25 FAILS with a documented floor message [docs/design/11 §9.1; ADR-014 C3]
- [ ] project is CXX-only, C++20, extensions OFF [docs/design/11 §9.1]
- [ ] options MW101_TESTS / MW_BUILD_CLAP / MW_BUILD_LV2 + sanitizer toggles exist and route through presets, not bare cache edits [docs/design/11 §9.1]
- [ ] the five subdirectories (core, plugin, ui, tests, presets) are wired per the §2.1 tree [docs/design/11 §2.1]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R build --no-tests=error
```
