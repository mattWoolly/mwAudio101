<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 138
title: Locate/declare validator targets (Validators.cmake)
status: in-review
depends-on: [001, 113]
component: infra
estimated-size: M
stream: integration
tag: cmake_validators
---

## Objective

Implement cmake/Validators.cmake that locates or declares the validator targets (pluginval, Steinberg validator, auval, clap-validator, headless Standalone smoke, lv2lint, lv2_validate) and exposes wired-status so Formats.cmake can gate on them.

## Context

- `docs/design/09 §2.1` — read first
- `docs/design/09 §2.2` — read first
- `plan/decisions/011 Decision validator map` — read first
- `plan/decisions/024 C1-C2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- cmake/Validators.cmake: find/declare each validator target and a per-validator WIRED flag
- Register each as a ctest target keyed by (format,platform)
- LV2 validators (lv2lint + lv2_validate) gated behind MW_BUILD_LV2 and Linux-only
- Expose results consumed by cmake/Formats.cmake (integration-7)

## Out of scope

- The configure-time gate logic itself (integration-7)
- Running the validators in CI (Phase 6 / qa)
- Building plugin artifacts (format-wrappers)

## Acceptance criteria

- [ ] Validator targets pluginval/validator/auval/clap-validator/standalone-smoke/lv2lint/lv2_validate are declared per §2.1 map
- [ ] auval/lv2 validators are platform-scoped (auval macOS-only, lv2 Linux-only) per §2.1
- [ ] Each (format,platform) pair resolves to its required validator(s) per §2.2
- [ ] ctest --preset default -R cmake_validators --no-tests=error is green; test names begin with cmake_validators

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R cmake_validators --no-tests=error
```
