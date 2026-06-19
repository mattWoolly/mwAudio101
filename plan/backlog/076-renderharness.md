<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 076
title: RenderHarness — deterministic offline render
status: done
depends-on: [001, 006, 007, 042, 086]
component: qa
estimated-size: M
stream: golden
tag: golden
---

> **Follow-up (QA MEDIUM):** the assembled Engine hardcodes its drift seed and exposes none on prepare(), so the GoldenKey seed reaches output via note-transpose/onset dither, not the §9.2 per-voice drift PRNG. Bless corpora encode note-per-seed, not drift-per-seed, until the Engine accepts a seed on prepare() (deferred Engine-API task).

## Objective

Implement RenderHarness::render(patch, stim, key) producing a deterministic RenderResult: identical (patch,stimulus,key) yields identical bytes on the same platform, and CLASS-EXACT identical on arm64 and Linux.

## Context

- `docs/design/11 §5.4` — read first
- `docs/design/11 §2.2` — read first
- `ADR-013 Layer 2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `golden`.

## Scope

- tests/golden/RenderHarness.h/.cpp with struct RenderResult{samples,sampleRate,engine} and class RenderHarness
- Pin SR, block size, seed, engine tag, oversample factor, renderVersion from the GoldenKey
- Drive the engine offline block-by-block over the stimulus; collect mono/interleaved f32
- Select the frozen constant-set tagged by renderVersion at setup (prepareToPlay analogue), never at sample rate

## Out of scope

- Comparison logic (golden-6/golden-7)
- Blob persistence (golden-5)
- Engine DSP internals (owned by full-engine; consumed opaque)

## Acceptance criteria

- [ ] mw101.unit.golden the same (patch,stimulus,key) renders byte-identical output twice [docs/design/11 §5.4]
- [ ] Negative control: changing the seed in the GoldenKey changes the rendered bytes [docs/design/11 §5.4]
- [ ] renderVersion in the EngineTag selects the matching frozen constant-set at setup, not at audio rate [ADR-023 V10; docs/design/11 §5.3]
- [ ] verify: ctest --preset default -R golden --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R golden --no-tests=error
```
