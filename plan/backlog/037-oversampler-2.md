<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 037
title: Oversampler: offline linear-phase FIR halfband + reported latency (render tier)
status: todo
depends-on: [001, 006, 007, 036]
component: core
estimated-size: S
stream: core-filter
tag: os-fir
---

## Objective

Implement the offline linear-phase FIR halfband resampler used for the bounce/render tier and for any future in-zone summing of correlated sources, exposing its fixed group-delay so host PDC can be reported (never silent).

## Context

- `ADR-004 §Decision item 4` — read first
- `ADR-004 C7` — read first
- `ADR-004 C14` — read first
- `ADR-004 C16` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `os-fir`.

## Scope

- Add linear-phase FIR halfband up/downsample kernels to the Oversampler (offline render path) with frozen versioned taps (C7, C14)
- Expose firLatencySamples() so callers can declare latency via setLatencySamples (C14)
- Intent-equivalent to the IIR path (same 2x ratio) but phase-linear; documented as audibly phase-divergent from realtime (C7, C16)

## Out of scope

- Calling setLatencySamples on the host (plugin-processor consumes firLatencySamples())
- Choosing realtime-vs-render path at runtime (core-filter-8 zone wrapper)

## Acceptance criteria

- [ ] os-fir test: FIR halfband is linear-phase (symmetric taps) and round-trip reconstructs a band-limited signal within tolerance (ADR-004 C7)
- [ ] os-fir test: firLatencySamples() returns the exact constant group delay matching the tap count (C14)
- [ ] os-fir test: taps are frozen versioned constants; output bit-identical across runs (C14)
- [ ] verify: ctest --preset default -R os-fir --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R os-fir --no-tests=error
```
