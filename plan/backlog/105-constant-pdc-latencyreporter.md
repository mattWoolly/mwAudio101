<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 105
title: Constant PDC LatencyReporter (plugin/latency/LatencyReporter.h/.cpp)
status: done
depends-on: [001, 006, 036, 091]
component: app
estimated-size: M
stream: plugin
tag: latency
---

## Objective

Implement LatencyReporter: compute the single constant worst-case total group delay (per-voice IIR zone + FX Drive 2x OS) in prepare, preallocate padding delay lines aligning shorter configs up to worst case, never recompute on the audio thread.

## Context

- `docs/design/09 §8.3` — read first
- `docs/design/00 §7` — read first
- `ADR-017 L1-L11` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `latency`.

## Scope

- computeWorstCaseLatency(sr): per-voice oversampled-zone group delay + FX Drive 2x OS group delay; FX Delay/Chorus musical time excluded
- preparePadding(worstCaseSamples,numChannels): preallocate fixed delay lines on short paths
- Constant across FX on/off and Quality tier (1x/2x/4x); never mutated from process
- Reported value sourced from group-delay constants supplied by core (oversampler, fx-drive)

## Out of scope

- setLatencySamples call site (plugin-13)
- Oversampler / FX Drive group-delay measurement internals (oversampler, fx-drive)
- FX Delay/Chorus DSP (fx-delay/fx-chorus)

## Acceptance criteria

- [ ] Reported latency is invariant to FX bypass, Quality tier, and build-to-build (tag 'latency') [§8.3; ADR-017 L4-L5, L7-L8, L11]
- [ ] Per-voice IIR zone + FX Drive 2x OS contribute; FX Delay/Chorus musical time does not (§8.3; ADR-017 L1-L3)
- [ ] Latency sized in prepare; padding lines preallocated; computeWorstCaseLatency/padding read perform zero alloc on a representative process (AudioThreadGuard) [§8.3; ADR-017 L10]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R latency --no-tests=error
```
