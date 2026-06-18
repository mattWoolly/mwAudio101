<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 095
title: Validator target locator cmake/Validators.cmake
status: in-review
depends-on: [001]
component: infra
estimated-size: S
stream: plugin
tag: validators
---

## Objective

Create cmake/Validators.cmake that locates or declares each per-format validator target (pluginval, Steinberg validator, auval, clap-validator, headless Standalone smoke, lv2lint, lv2_validate) and reports per-platform availability.

## Context

- `docs/design/09 §2.1` — read first
- `docs/design/09 §2.2` — read first
- `ADR-011 §Decision validator map` — read first
- `ADR-024 Contract table` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- find_program / declare each validator target and set a cached WIRED flag per validator
- Expose a queryable map validator->wired-on-this-platform for Formats.cmake to consume
- auval/Steinberg validator wired on macOS; pluginval/clap-validator on macOS+Linux+Windows; lv2lint/lv2_validate on Linux only
- No format/target logic here (that is plugin-2)

## Out of scope

- Format resolution and the configure-time error gate (plugin-2)
- CMakePresets format sets (plugin-3)
- Actually invoking validators in CI

## Acceptance criteria

- [ ] Configuring on macOS reports auval+validator+pluginval+clap-validator wired per §2.1 table
- [ ] Configuring on Linux reports lv2lint+lv2_validate wired and auval NOT wired per §2.1
- [ ] A test (tag 'validators') asserts the wired-flag map matches the §2.1 / ADR-024 Contract table per platform

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R validators --no-tests=error
```
