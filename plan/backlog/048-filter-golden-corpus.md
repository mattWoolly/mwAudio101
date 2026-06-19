<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 048
title: FILTER golden corpus (EARLY freeze gate) — bless + compare across blessed rates
status: in-review
depends-on: [001, 006, 047, 043, 045]
component: qa
estimated-size: M
stream: golden
tag: golden
---

## Objective

Author and bless the FILTER CLASS-FP golden corpus (ladder nonlinear path) at each blessed sample rate as the EARLY freeze gate, and register the compare tests, depending only on the filter module.

## Context

- `docs/design/11 §5.1` — read first
- `docs/design/11 §5.2` — read first
- `ADR-013 C6` — read first
- `ADR-023 V12` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `golden`.

## Scope

- tests/golden/corpus/ filter blobs+sidecars at {44100,48000,88200,96000} Hz with MANIFEST entries
- Filter stimuli (self-osc, cutoff sweep, resonance sweep) rendered via RenderHarness and compared via CompareFp
- Per-corpus tolerance recorded in MANIFEST; engine-tagged with renderVersion
- Compare tests named mw101.golden.* exercising arm64 bit-exact and banded Linux paths

## Out of scope

- Filter DSP internals (owned by filter-module; consumed opaque)
- Non-filter module corpora (golden-13)

## Acceptance criteria

- [ ] mw101.golden.class-fp a blessed filter golden exists at each of {44100,48000,88200,96000} Hz, keyed by sample rate [ADR-023 V12]
- [ ] On arm64 the blessed filter golden is bit-exact; a deliberate 1-ULP perturbation FAILS (paired) [ADR-013 C6]
- [ ] Self-osc stimulus golden encodes the k>=4 self-oscillation and k=3.9 silence distinction [docs/design/11 §4.2]
- [ ] verify: ctest --preset default -R class-fp --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R golden --no-tests=error
```
