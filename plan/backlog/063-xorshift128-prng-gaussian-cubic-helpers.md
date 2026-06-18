<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 063
title: Xorshift128+ PRNG with Gaussian/cubic helpers and seed derivation
status: todo
depends-on: [001, 006, 007]
component: engine
estimated-size: S
stream: vintage
tag: vintage_prng
---

## Objective

Implement the header-only POD xorshift128+ PRNG with Box-Muller Gaussian, cubic (2u-1)^3 shaping, and the deterministic splitmix64/goldenMix per-voice seed derivation used by the whole drift subsystem.

## Context

- `08-vintage-variance.md §3.1` — read first
- `08-vintage-variance.md §6` — read first
- `08-vintage-variance.md §8.2` — read first
- `08-vintage-variance.md §12.4` — read first
- `08-vintage-variance.md §12.7` — read first
- `ADR-009 §Decision 5` — read first
- `ADR-009 VV-17` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vintage_prng`.

## Scope

- core/dsp/drift/Xorshift128p.h: POD xorshift128+ state + next()/nextFloat01() returning uniform [0,1)
- gaussian() via Box-Muller returning N(0,1); cubic() returning (2u-1)^3 on [-1,1]
- splitmix64(uint64_t) and seedFromInstance(uint64_t instanceSeed, int voiceIndex) = splitmix64(instanceSeed ^ goldenMix(voiceIndex))
- constexpr-friendly, header-only, no std::random, no heap, no locks

## Out of scope

- Thermal/OU integration (vintage-2)
- Any DriftState aggregation or draw orchestration (vintage-3, vintage-4)

## Acceptance criteria

- [ ] Tests named vintage_prng* verify same seed yields bit-identical sequence across runs (§12.7, VV-17)
- [ ] Tests verify distinct voiceIndex seeds decorrelate (low cross-correlation over long sequence) per §8.2
- [ ] Tests verify gaussian() ~zero mean/unit variance over large N and cubic() stays within [-1,1] (§6)
- [ ] Verify: ctest --preset default -R vintage_prng --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vintage_prng --no-tests=error
```
