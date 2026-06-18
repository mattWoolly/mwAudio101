<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 066
title: VintageMacro host-thread Age-to-target mapping
status: todo
depends-on: [001, 006, 007, 020]
component: engine
estimated-size: S
stream: vintage
tag: vintage_macro
---

## Objective

Implement VintageMacro.h/.cpp: a host-thread (non-audio) mapping from the Age macro (0-100%) through the kAgeCurve table to already-smoothed APVTS target values for the drift/variance group.

## Context

- `08-vintage-variance.md §3.2` — read first
- `08-vintage-variance.md §10` — read first
- `08-vintage-variance.md §10.1` — read first
- `08-vintage-variance.md §10.2` — read first
- `ADR-009 §Decision 7` — read first
- `ADR-009 VV-1` — read first
- `ADR-016 §Decision 4` — read first
- `ADR-016 R-4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vintage_macro`.

## Scope

- VintageMacro::apply(age01, ...) mapping via kAgeCurve from Calibration.h to scaled targets for drift.depth/rate, tune.slop, var.* group (§10.1)
- Host-thread only; writes already-smoothed param targets so audio thread cost is zero (§3.2, §Decision 7)
- Age parameter default 0 = in tune on load; macro AND targets host-visible/automatable (§10.2, VV-1)

## Out of scope

- Audio-thread DSP (vintage-4)
- INIT-patch authoring that sets Age low (consumed from state-presets per ADR-016 R-4)
- Defining param IDs/ranges (consumed from param-schema)

## Acceptance criteria

- [ ] Tests named vintage_macro* verify age=0 maps to zero added group offset (in tune on load) per §10.2, VV-1
- [ ] Tests verify monotonic kAgeCurve mapping scales each target within its schema range (§10.1)
- [ ] Tests verify apply() touches only param targets and performs no audio-thread DSP (host-thread path) per §3.2/§Decision 7
- [ ] Verify: ctest --preset default -R vintage_macro --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vintage_macro --no-tests=error
```
