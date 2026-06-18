<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 047
title: Oversampler zone wrapper: factor selection, OS_CEILING clamp, and CI alias-floor harness
status: todo
depends-on: [001, 006, 007, 039, 036]
component: core
estimated-size: M
stream: core-filter
tag: os-zone
---

## Objective

Implement the per-voice oversampled-zone wrapper that does exactly one up + one down per voice around a supplied nonlinear processor callback, selects 1x/2x/4x stride from the quality factor, clamps to 1x above OS_CEILING_HZ, and provide the CI null/THD+alias-floor harness that sizes the IIR order.

## Context

- `ADR-004 §Decision items 1-5` — read first
- `ADR-004 C8` — read first
- `ADR-004 C9` — read first
- `ADR-004 C11` — read first
- `ADR-004 C12` — read first
- `00-architecture-overview.md §8.5 V15/V16` — read first
- `02-dsp-filter.md §10 F-09` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `os-zone`.

## Scope

- Oversampler zone API: run a caller-supplied nonlinear process between up and down, exactly 1 up + 1 down, never down-then-up (C8)
- Stride selection from oversample factor (1x bypasses resamplers, 2x, 4x); per-voice scope (C9, C11)
- Clamp factor to 1x when factor*fs would exceed OS_CEILING_HZ (192 kHz internal); record clamp for provenance (§8.5 V15/V16)
- CI alias-floor harness: measure alias products at full drive + self-osc vs a higher-oversampled reference of the same engine, target ~ -90..-100 dBFS (C12, F-09)

## Out of scope

- The quality enum / factor derivation (consumed from param-schema; ADR-018 owns mw101.quality)
- Filter/VCA/Drive DSP bodies (supplied as the processor callback by voice/env-lfo-vca/fx-drive)

## Acceptance criteria

- [ ] os-zone test: exactly one upsample and one downsample occur per voice block; 1x stride bypasses the resamplers (§10 F-09, C8)
- [ ] os-zone test: at 2x, alias products from a full-drive self-oscillating LadderFilter run sit below the C12 self-referential floor vs a higher-oversampled run of the same engine (§10 F-09, C12)
- [ ] os-zone test: factor clamps to 1x when factor*fs > OS_CEILING_HZ and the clamp is recorded (§8.5 V15/V16)
- [ ] os-zone test: AudioThreadGuard confirms stride/factor changes allocate no heap and take no lock (C15)
- [ ] verify: ctest --preset default -R os-zone --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R os-zone --no-tests=error
```
