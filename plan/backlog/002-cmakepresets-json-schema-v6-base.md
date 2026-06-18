<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 002
title: CMakePresets.json schema-v6 base + sanitizer + per-platform presets
status: todo
depends-on: [001]
component: infra
estimated-size: M
stream: infra
tag: build
---

## Objective

Author CMakePresets.json (schema v6) as the sole build entrypoint: hidden base + inheriting configure/build/test presets paired 1:1, sanitizer presets, and per-platform presets carrying only toolchain/generator + format scope.

## Context

- `docs/design/11 §9.2` — read first
- `docs/design/11 §9.3` — read first
- `ADR-014 C1` — read first
- `ADR-014 C6` — read first
- `ADR-014 C7` — read first
- `ADR-014 C8` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- configurePresets: default (RelWithDebInfo), debug, release, asan, ubsan, tsan, macos-arm64, linux-x64, windows-x64 via hidden-base inheritance
- buildPresets and testPresets sharing the SAME names 1:1
- testPresets carry --output-on-failure and --no-tests=error
- per-platform format scoping: macOS VST3+AU+CLAP+Standalone; Linux VST3+CLAP+Standalone(+LV2 goal); Windows VST3+CLAP+Standalone via MW_BUILD_CLAP/MW_BUILD_LV2 toggles
- per-platform presets add ONLY toolchain/generator + format scope (no build/test logic)

## Out of scope

- the ctest registration/discovery wiring inside tests/ (infra-9)
- docs/BUILDING.md mapping (infra-13)
- CI YAML (Phase 6 / out of stream)

## Acceptance criteria

- [ ] cmake --preset <X> is the only configure entrypoint; schema is v6 [docs/design/11 §9.2; ADR-014 C1]
- [ ] configurePresets/buildPresets/testPresets names pair 1:1; per-platform presets inherit a hidden base and add only toolchain/generator + format scope [ADR-014 C8]
- [ ] testPresets carry --output-on-failure and --no-tests=error [docs/design/11 §9.2; ADR-014 C7]
- [ ] format scoping matches the §9.3 per-platform table; no format is built where no validator is wired [docs/design/11 §9.3; ADR-014 C6]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R build --no-tests=error
```
