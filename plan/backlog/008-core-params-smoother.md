<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 008
title: core/params/Smoother.h — OnePoleSmoother + ctest
status: todo
depends-on: [006]
component: core
estimated-size: S
stream: infra
tag: smooth
---

## Objective

Implement the control-rate OnePoleSmoother in core/params/Smoother.h with TDD: a step change in a normalized value yields a smoothed (non-zippered) engineering value, aligned to chunk boundaries.

## Context

- `docs/design/00 §5.4` — read first
- `docs/design/00 §4.4` — read first
- `ADR-001 C7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `smooth`.

## Scope

- OnePoleSmoother type: setTarget/process/reset, one-pole (and/or linear) control-rate smoothing, noexcept
- chunk-boundary cadence alignment per §4.4 control-rate tick
- paired positive/negative property tests: a step input is smoothed (monotone approach, not instantaneous) AND a constant input is passed through unchanged
- test names begin with the smooth tag; verify via ctest --preset default -R smooth --no-tests=error

## Out of scope

- normalized->engineering mapping against Calibration.h (param-schema / engine streams)
- the param-smoothing block-boundary CLASS-EXACT golden (golden-harness stream)

## Acceptance criteria

- [ ] a step change in a normalized parameter produces a smoothed, non-zippered value; constant input is unchanged (paired positive/negative control) [docs/design/00 §5.4; ADR-001 C7]
- [ ] smoothing ticks align to chunk boundaries [docs/design/00 §4.4]
- [ ] tests are named smooth* and pass under ctest --preset default -R smooth --no-tests=error [docs/design/11 §4.1, §8.3]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R smooth --no-tests=error
```
