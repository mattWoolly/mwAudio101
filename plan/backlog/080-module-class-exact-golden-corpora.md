<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 080
title: Per-module CLASS-EXACT golden corpora — seq/divider/PRNG/arp/param-smooth/CC
status: in-review
depends-on: [001, 006, 007, 078, 045, 032, 086, 071]
component: qa
estimated-size: M
stream: golden
tag: golden
---

## Objective

Author and bless the CLASS-EXACT golden corpora for the integer/control paths (sequencer bytes, 4013 divider OR edges, fixed-seed PRNG stream, arp ordering, param-smoothing boundaries, CC/param mapping) across the blessed rates, with hash-compare tests.

## Context

- `docs/design/11 §5.1` — read first
- `docs/design/11 §5.2` — read first
- `ADR-013 C5` — read first
- `ADR-023 V12` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `golden`.

## Scope

- tests/golden/corpus/ CLASS-EXACT blobs+sidecars+MANIFEST entries for seq, divider-OR, PRNG, arp, param-smooth, CC map
- Render via RenderHarness; SHA-256 hash-compare via CompareExact; identical on arm64 AND Linux
- Each corpus keyed by blessed sample rate where rate-relevant
- Compare tests named mw101.golden.class-exact.*

## Out of scope

- Module DSP/logic internals (owned by their streams; consumed opaque)
- FP analog-path corpora (golden-12 and module-specific FP corpora)

## Acceptance criteria

- [ ] mw101.golden.class-exact each integer/control golden SHA-256 matches bit-for-bit; a one-byte diff FAILS (paired) [ADR-013 C5]
- [ ] Corpora exist at the blessed rates where rate-relevant, keyed by sample rate [ADR-023 V12]
- [ ] PRNG-stream golden uses a fixed seed and reproduces identically across runs [docs/design/11 §5.1]
- [ ] verify: ctest --preset default -R class-exact --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R golden --no-tests=error
```
