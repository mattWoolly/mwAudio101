<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 018
title: renderVersion state lifecycle + opt-in flag (RenderVersionState.h/.cpp)
status: in-review
depends-on: [016, 017]
component: core
estimated-size: S
stream: params
tag: renderver
---

## Objective

Implement the state-resident renderVersion lifecycle: detect a stored renderVersion < CURRENT, surface a non-modal opt-in decision, and manage the sticky renderOptIn flag and on-save write-back.

## Context

- `docs/design/06-parameters-state-presets.md §9.2` — read first
- `§5.3` — read first
- `§5.4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `renderver`.

## Scope

- core/version/RenderVersionState.h/.cpp: a message-thread helper deciding, from a loaded tree's renderVersion vs kCurrentRenderVersion, whether to pin legacy render and raise the opt-in (§9.2)
- Sticky renderOptIn extras flag: accepting writes kCurrentRenderVersion on next save + sets renderOptIn; declining is sticky; new/blank state authors at CURRENT (§9.2)
- Returns the render-version-to-use and whether to raise the opt-in; selection is consumed at prepareToPlay, never audio-rate (§9.2; §12)
- Test: render<CURRENT pins stored render + flags opt-in; accept -> writes CURRENT + sets renderOptIn; decline sticky; new state == CURRENT and no opt-in

## Out of scope

- the actual legacy DSP constant-set selection (golden-harness / full-engine)
- the UI opt-in dialog (ui-skeleton)
- bless-tool / MANIFEST governance (golden-harness)

## Acceptance criteria

- [ ] A load with renderVersion < CURRENT pins the stored renderVersion and raises the opt-in; audio does not change without accept [§9.2 V8-V10]
- [ ] Accepting writes kCurrentRenderVersion on next save and sets sticky renderOptIn; declining is sticky [§9.2]
- [ ] New/blank state authors at kCurrentRenderVersion with no opt-in [§9.2 V9]
- [ ] Test names begin with renderver and assert pin/opt-in, accept-write-back, decline-sticky, new-at-current [§9.2]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R renderver --no-tests=error
```
