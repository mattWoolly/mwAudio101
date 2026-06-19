<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 061
title: Env/LFO param de-zipper class verification (S2/S4 paired test)
status: in-review
depends-on: [007, 006, 020, 054, 055]
component: qa
estimated-size: S
stream: core-env-lfo-vca
tag: envlfovca_dezip
---

## Objective

Add the paired positive/negative de-zipper property test: a continuous param (env time / LFO depth, S2) de-zippers a step input via OnePoleSmoother while the stepped LFO shape selector (S7) does NOT smear through wrong indices.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §6.1` — read first
- `plan/decisions/020-parameter-smoothing-policy.md S2` — read first
- `plan/decisions/020-parameter-smoothing-policy.md S7` — read first
- `plan/decisions/020-parameter-smoothing-policy.md S12` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `envlfovca_dezip`.

## Scope

- Positive: feeding a step to an env-time / LFO-depth target drives the OnePoleSmoother smoothly toward target with snap termination (§6.1, S2/S10)
- Negative: mw101.lfo.shape index changes discretely, never interpolating through intermediate shape values (S7)
- Assert de-zipper advances on the control-rate tick cadence (S11)
- Assert envelope CONTOUR and LFO VALUE are NOT de-zippered (generated signals) (§6.1)
- Block-boundary bookkeeping deterministic per S12

## Out of scope

- Defining OnePoleSmoother (core-types)
- Doc 06 smoothing-class registry entries
- Cross-platform bless run (golden-harness)

## Acceptance criteria

- [ ] Continuous param de-zippers a step input; stepped selector does NOT smear (ADR-020 S7/S12 paired test, §6.1 acceptance hook)
- [ ] Envelope contour and LFO value are confirmed not de-zippered (§6.1)
- [ ] ctest --preset default -R envlfovca_dezip --no-tests=error passes; tests named envlfovca_dezip_* (positive and negative)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R envlfovca_dezip --no-tests=error
```
