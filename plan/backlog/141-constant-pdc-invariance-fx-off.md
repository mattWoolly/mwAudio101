<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 141
title: Constant-PDC invariance + FX-off bit-exact integration test
status: todo
depends-on: [113, 036, 091, 077, 136]
component: qa
estimated-size: S
stream: integration
tag: pdc_invariant
---

## Objective

Assert the reported setLatencySamples is invariant to FX bypass, Quality tier, and build-to-build, equals the worst-case total group delay, and that FX-off output is bit-exact at the declared worst-case offset.

## Context

- `docs/design/00 §7.1` — read first
- `docs/design/00 §7.4` — read first
- `plan/decisions/017 Contract L4-L8` — read first
- `plan/decisions/017 L6` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `pdc_invariant`.

## Scope

- Assert reported latency unchanged across master/per-block FX bypass and all Quality tiers (1x/2x/4x)
- Assert reported latency equals the worst-case total group delay and is sized only in prepare
- Assert FX-off output is sample-identical to the blessed mono voice at the declared worst-case offset
- Confirm the per-voice IIR-zone group delay is inside the golden, not subtracted

## Out of scope

- LatencyReporter compute/padding internals (format-wrappers)
- FX/oversampler DSP (their streams)
- Blessing the FX-off golden (golden-harness)

## Acceptance criteria

- [ ] Reported latency is invariant to FX bypass, Quality tier, and build-to-build per ADR-017 L5/L7/L8
- [ ] Reported latency equals worst-case total group delay and is never mutated from process per §7.1 / L4/L10
- [ ] FX-off output is bit-exact at the declared worst-case offset with the IIR-zone delay inside the golden per §7.4 / L6
- [ ] ctest --preset default -R pdc_invariant --no-tests=error is green; test names begin with pdc_invariant

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R pdc_invariant --no-tests=error
```
