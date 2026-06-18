<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 036
title: Oversampler: polyphase IIR halfband up/downsampler (realtime path)
status: in-review
depends-on: [001, 006, 007]
component: core
estimated-size: M
stream: core-filter
tag: os-iir
---

## Objective

Implement the realtime polyphase IIR halfband resampler (the up + down kernels) used to enter and leave the per-voice 2x nonlinear zone, with fixed frozen coefficients, preallocated delay-line state, and no fast-math reassociation so it is bit-exact across macOS arm64 and Linux x64.

## Context

- `ADR-004 §Decision item 4` — read first
- `ADR-004 C4-C8` — read first
- `ADR-004 C10` — read first
- `ADR-004 C15` — read first
- `00-architecture-overview.md §9.1 RT-5/RT-6` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `os-iir`.

## Scope

- core/dsp/Oversampler.h/.cpp: prepare(maxBlockSize, maxFactor) preallocates all delay-line/scratch state; reset (§ADR-004 C15)
- Polyphase IIR halfband upsample (1x->2x) and downsample (2x->1x) noexcept kernels; fixed coefficients frozen as versioned constants (C4, C10, C14)
- Factor change varies active stride only, never allocates on the audio thread (C15)
- FTZ/DAZ-safe processing; anti-denormal in delay lines (RT-5)

## Out of scope

- The zone wrapper that calls a processor between up and down (core-filter-8)
- Offline linear-phase FIR resampler (core-filter-7)
- IIR order/ripple sizing target measurement (core-filter-8 alias-floor harness)

## Acceptance criteria

- [ ] os-iir test: round-trip up->down of a band-limited signal reconstructs within tolerance and the halfband stopband attenuation meets the design bound (ADR-004 C4)
- [ ] os-iir test: AudioThreadGuard confirms upsample/downsample allocate no heap/lock; prepare is the only allocator; factor change does not allocate (C15)
- [ ] os-iir test: kernel output bit-identical across repeated runs for fixed input/coeffs (frozen constants, C14)
- [ ] verify: ctest --preset default -R os-iir --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R os-iir --no-tests=error
```
