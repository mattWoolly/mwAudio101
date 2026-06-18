<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 089
title: FractionalDelayLine header-only ring buffer
status: done
depends-on: [001, 006, 007]
component: core
estimated-size: S
stream: fx
tag: fracdelay
---

## Objective

Implement the shared header-only FractionalDelayLine: a preallocated ring buffer with interpolated fractional read, used by Chorus, Delay, and the dry-pad alignment line.

## Context

- `07-fx-section.md §5.3` — read first
- `07-fx-section.md §3.2` — read first
- `ADR-010 FX-10` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `fracdelay`.

## Scope

- core/dsp/fx/FractionalDelayLine.h with prepare(maxLengthSamples)/reset/write/read(delaySamples)/processBlock per §5.3 signature
- Preallocate buffer once in prepare(); write/read/processBlock perform no allocation
- At-least-linear interpolation on read (cubic/Lagrange permitted; interpolation-order constant is PI in Calibration.h)
- processBlock integer-delay helper for fixed alignment (dry pad) use

## Out of scope

- Chorus/Delay-specific modulation or feedback logic
- Sizing decisions made by callers (callers pass max length)

## Acceptance criteria

- [ ] fracdelay test: integer-delay read of D samples returns input delayed by exactly D (impulse in, impulse out at index D) per §5.3
- [ ] fracdelay test: fractional read at non-integer delay interpolates monotonically between bracketing integer taps (e.g. ramp signal) per §5.3
- [ ] fracdelay test: after prepare(), write/read/processBlock perform no heap allocation (alloc-tracking) per ADR-010 FX-10
- [ ] Verify: ctest --preset default -R fracdelay --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R fracdelay --no-tests=error
```
