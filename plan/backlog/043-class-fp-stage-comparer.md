<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 043
title: CLASS-FP two-stage comparer (scalar fingerprint + FFT/NMSE + alias floor)
status: in-review
depends-on: [001, 006, 007, 041]
component: qa
estimated-size: M
stream: golden
tag: golden
---

## Objective

Implement compareFp() with Stage 1 scalar fingerprint and Stage-1-gated Stage 2 (windowed-FFT NMSE-in-dB + alias floor), applying per-corpus tolerance from the manifest: bit-exact on arm64, banded on Linux/Windows, and refusing cross-engine/renderVersion compares.

## Context

- `docs/design/11 §6.3` — read first
- `docs/design/11 §6.1` — read first
- `ADR-013 C6` — read first
- `ADR-013 C7` — read first
- `ADR-013 C9` — read first
- `ADR-023 V11` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `golden`.

## Scope

- tests/golden/CompareFp.h/.cpp with Stage1Fingerprint, Stage2Metrics, FpTolerance, FpResult and compareFp(got,blessed,tol,full)
- Stage 1: rms, peak, maxAbsErr, envelopeErr; Stage 2 runs only on Stage-1 flag or full==true
- Stage 2: windowed-FFT NMSE in dB and alias-floor energy above the perceptual limit
- arm64: tol.maxAbsErr==0 bit-exact gate; Linux/Windows: banded; refuse if sameEngineContext is false

## Out of scope

- Tolerance values themselves (live in MANIFEST, golden-8)
- Global tolerance #define (forbidden — per-corpus only)

## Acceptance criteria

- [ ] mw101.golden.class-fp Stage 2 is SKIPPED when Stage 1 is within tolerance and full==false, and RUN on a Stage-1 flag or full==true [ADR-013 C9]
- [ ] With tol.maxAbsErr==0, a 1-ULP diff FAILS (arm64 bit-exact); with a band, the same diff PASSES inside band and FAILS outside (paired) [ADR-013 C6, C7]
- [ ] compareFp refuses (no pass) when the blessed EngineTag differs in ladder/oversample/renderVersion [ADR-013 C22; ADR-023 V11]
- [ ] verify: ctest --preset default -R class-fp --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R golden --no-tests=error
```
