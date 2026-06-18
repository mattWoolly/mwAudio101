<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 049c
title: core/calibration: prepareToPlay constant-set SELECTOR keyed by renderVersion (legacy-render path)
status: todo
depends-on: [005b, 033, 035, 029]
component: core
estimated-size: M
stream: calibration
tag: calibration
---

## Objective

Implement the constant-set selector that, at prepareToPlay (never at audio rate), chooses the frozen constant-set (tanh approximation, decimator/halfband coeffs, compensation-table source) for the session's stored renderVersion off the Calibration.h registry, realizing the legacy-render path so a pinned old renderVersion reproduces that version's audio.

## Context

- `docs/design/00 §8.2` — read first
- `docs/design/00 §8.3` — read first
- `ADR-023 V10` — read first
- `ADR-023 V18` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `calibration`.

## Scope

- core/calibration constant-set selector: select(renderVersion) -> const reference to the frozen constant-set entry in the Calibration.h registry, called only from prepare/prepareToPlay (§8.3; ADR-023 V10, V18)
- Wire the selected set to consumers: tanh coeffs feeding FastTanh (033), decimator/halfband coeffs feeding the oversampler, compensation-table source constants feeding FilterTables (035), and the VCO pitch/drift constants (029) — all read at prepare, frozen for the block-processing lifetime
- Selecting CURRENT yields the blessed path; selecting a shipped legacy renderVersion yields that version's frozen set; an unknown/unshipped renderVersion is refused (no silent fallback to CURRENT) (§8.2)
- No transcendental or table rebuild at audio rate; selection is a prepare-time pointer/reference bind (§9.1 RT-6)

## Out of scope

- The renderVersion state lifecycle / opt-in affordance (018)
- Building the per-sample-rate compensation tables themselves (035 owns the build; this selects which constant source feeds it)
- UI 'update engine (audio will change)' affordance (UI stream)

## Acceptance criteria

- [ ] select(renderVersion) runs only at prepare; an AudioThreadGuard-fenced test confirms zero alloc/lock and no selection call on the hot path [docs/design/00 §8.3; ADR-023 V18]
- [ ] Selecting CURRENT reproduces the blessed constant set; selecting a shipped legacy renderVersion binds that version's frozen tanh/decimator/comp constants (tagged 'cal') [ADR-023 V10]
- [ ] An unknown or unshipped renderVersion is REFUSED rather than silently falling back to CURRENT [docs/design/00 §8.2/§8.3]
- [ ] FastTanh, FilterTables, and the oversampler observe the selected constant set, verified by a prepare-time wiring test [docs/design/00 §8.3]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R calibration --no-tests=error
```
