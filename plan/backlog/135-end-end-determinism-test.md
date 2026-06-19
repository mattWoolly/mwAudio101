<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 135
title: End-to-end determinism test (same seed + same BlockContext sequence)
status: in-review
depends-on: [006, 118, 077]
component: qa
estimated-size: S
stream: integration
tag: e2e_determinism
---

## Objective

Assert that two assembled Engine instances given the identical seed and identical BlockContext event/param sequence produce bit-identical output on integer/deterministic paths and within FP tolerance on analog stages.

## Context

- `docs/design/00 §9.1` — read first
- `docs/design/00 §9.2` — read first
- `docs/design/00 §6.2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `e2e_determinism`.

## Scope

- Run two engines with identical pre-seeded drift state and identical block sequences
- Compare integer/deterministic-path outputs bit-for-bit
- Compare FP analog-stage outputs within max abs <= 1e-6 off-reference
- Confirm fixed-order summation gives a stable FP reduction order (§6.2)

## Out of scope

- Blessing/storing the golden corpus (golden-harness)
- Cross-platform CI runner wiring
- Per-module determinism

## Acceptance criteria

- [ ] Identical seed + identical BlockContext sequence yields bit-identical integer-path output per §9.1 RT-7
- [ ] FP analog stages compare within max abs <= 1e-6 off the macOS arm64 reference per §9.1/§9.2
- [ ] Fixed voice-index summation order is exercised per §6.2
- [ ] ctest --preset default -R e2e_determinism --no-tests=error is green; test names begin with e2e_determinism

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R e2e_determinism --no-tests=error
```
