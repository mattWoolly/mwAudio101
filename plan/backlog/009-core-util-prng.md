<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 009
title: core/util/Prng.h — seeded integer PRNG + CLASS-EXACT stream ctest
status: in-review
depends-on: [006]
component: core
estimated-size: S
stream: infra
tag: prng
---

## Objective

Implement the fixed-seed integer PRNG (64-bit LCG/PCG) in core/util/Prng.h with TDD, producing a reproducible integer stream that is bit-identical run-to-run and across platforms.

## Context

- `docs/design/11 §5.1 (CLASS-EXACT, PRNG)` — read first
- `docs/design/00 §9.2` — read first
- `ADR-013 C5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `prng`.

## Scope

- Prng type: seed(uint64_t), next integer/float draw, noexcept, no allocation
- 64-bit LCG/PCG (xorshift only with the §9.1 contested-endorsement caveat)
- paired tests: same seed => identical stream (golden first N values) AND different seed => different stream
- test names begin with the prng tag; verify via ctest --preset default -R prng --no-tests=error

## Out of scope

- per-voice drift seeding/wiring in prepare (voice stream)
- the noise-source DSP (osc-module stream)
- the CLASS-EXACT golden corpus blobs (golden-harness stream)

## Acceptance criteria

- [ ] identical seed yields a bit-identical integer stream run-to-run and across macOS/Linux (CLASS-EXACT) [docs/design/11 §5.1; ADR-013 C5]
- [ ] the negative control (different seed => different stream) is present and would fail a constant stub [docs/design/11 §4.2; ADR-013 C4]
- [ ] tests are named prng* and pass under ctest --preset default -R prng --no-tests=error [docs/design/11 §3.1, §8.3]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R prng --no-tests=error
```
