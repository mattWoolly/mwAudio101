<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 090
title: FxOversampler2x dedicated post-voice 2x halfband pair
status: in-review
depends-on: [001, 006, 007, 036]
component: core
estimated-size: M
stream: fx
tag: fxos
---

## Objective

Implement FxOversampler2x: the dedicated post-voice 2x up/down halfband pair used only by Drive, exposing a fixed measured group delay for PDC.

## Context

- `07-fx-section.md §2.1` — read first
- `07-fx-section.md §4.1` — read first
- `ADR-017 L2` — read first
- `ADR-017 §A2` — read first
- `ADR-017 L9` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `fxos`.

## Scope

- core/dsp/fx/FxOversampler2x.h/.cpp with prepare/reset, upsample-by-2 and downsample-by-2 halfband filtering, and latencySamples() query
- All filter state preallocated in prepare(); process path noexcept, no alloc, no lock
- Fixed deterministic group delay returned by latencySamples(), constant for instance lifetime
- Distinct instance from the per-voice voice oversampler (this is FX-rate only) per ADR-017 §A2/L9

## Out of scope

- The waveshaper/tilt/DC-block (owned by fx-4 Drive)
- Per-voice voice-zone oversampler (owned by oversampler stream)
- setLatencySamples host call (owned by plugin/)

## Acceptance criteria

- [ ] fxos test: up-then-down round trip of a band-limited signal reconstructs within tolerance (no gross level/aliasing error) per §4.1
- [ ] fxos test: a tone near the original Nyquist is attenuated by the halfband on downsample (anti-alias decimation) per ADR-017 L2 / research/10 §5
- [ ] fxos test: latencySamples() returns a fixed nonzero value, constant across calls and independent of input per ADR-017 L2
- [ ] fxos test: prepare/reset/process/latencySamples perform no heap allocation and take no locks per ADR-017 L10
- [ ] Verify: ctest --preset default -R fxos --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R fxos --no-tests=error
```
