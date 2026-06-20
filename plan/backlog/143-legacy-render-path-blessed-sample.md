<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 143
title: Legacy-render path + blessed sample-rate set integration test
status: done
depends-on: [006, 119, 036, 077, 118]
component: qa
estimated-size: S
stream: integration
tag: renderversion_e2e
---

## Objective

Assert the assembled engine selects the frozen constant-set for a session's stored renderVersion (no silent audio change), runs the blessed path at {44100,48000,88200,96000} Hz, and clamps oversampling to 1x above OS_CEILING_HZ.

## Context

- `docs/design/00 §8.2` — read first
- `docs/design/00 §8.3` — read first
- `docs/design/00 §8.4` — read first
- `docs/design/00 §8.5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `renderversion_e2e`.

## Scope

- Load a session with renderVersion < CURRENT and assert it renders on the legacy path without audio change
- Assert frozen constant-set selection happens at prepare, never at audio rate
- Run the engine at each blessed sample rate and confirm the per-SR table path
- Assert 2x oversampling clamps to 1x above OS_CEILING_HZ and the clamp is recorded in provenance

## Out of scope

- renderVersion state I/O serialization (state-presets)
- Frozen constant-set authoring (calibration / per-module streams)
- UI update-engine affordance (ui)

## Acceptance criteria

- [ ] renderVersion < CURRENT renders on the legacy path with no audio change without opt-in per §8.2
- [ ] Frozen constant-set is selected at prepare keyed by renderVersion, never at audio rate, per §8.3
- [ ] Goldens-path runs at {44100,48000,88200,96000} Hz and clamps to 1x above OS_CEILING_HZ per §8.4/§8.5
- [ ] ctest --preset default -R renderversion_e2e --no-tests=error is green; test names begin with renderversion_e2e

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R renderversion_e2e --no-tests=error
```
