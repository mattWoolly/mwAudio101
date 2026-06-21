<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 140
title: Host smoke matrix: headless Standalone launch + per-format validator invocation
status: done
depends-on: [113, 001, 006, 136, 137, 138]
component: qa
estimated-size: M
stream: integration
tag: host_smoke
---

## Objective

Wire the host smoke matrix that headlessly launches the Standalone build and invokes each wired per-format validator (pluginval/auval/validator/clap-validator/lv2lint+lv2_validate) as ctest gates per the platform tier.

## Context

- `docs/design/09 §2.1` — read first
- `docs/design/09 §2.2` — read first
- `plan/decisions/011 Contract C7-C8` — read first
- `plan/decisions/011 C6` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `host_smoke`.

## Scope

- Headless Standalone smoke-launch (load/prepare/process/teardown, no audio device)
- Invoke each platform's required validators as ctest gates per the §2.1 matrix
- macOS gate = VST3+AU+CLAP+Standalone green; Linux gate = VST3+CLAP+Standalone green (LV2 optional)
- LV2 validators run only when wired+green and never block the Linux gate

## Out of scope

- Declaring/locating validator targets (integration-8)
- The configure-time gate (integration-7)
- Cross-format bit-exactness (integration-9)

## Acceptance criteria

- [ ] macOS arm64 release runs auval/pluginval/validator/clap-validator + Standalone smoke as gates per §2.1 / ADR-011 C8
- [ ] Linux x64 runs the same minus AU; LV2 absence never blocks the Linux gate per ADR-011 C6/C7
- [ ] Headless Standalone smoke loads, processes, and tears down without an audio device per §2.1
- [ ] ctest --preset default -R host_smoke --no-tests=error is green; test names begin with host_smoke

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R host_smoke --no-tests=error
```
