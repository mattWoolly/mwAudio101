<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 059
title: Lfo Random S/H, Noise source, and cycleEdge flag
status: in-review
depends-on: [007, 006, 055]
component: core
estimated-size: M
stream: core-env-lfo-vca
tag: lfo_sh
---

## Objective

Implement the digital uniform sample/hold Random core (seeded deterministic PRNG), the injected shared-noise Noise core, and the H->L cycle-edge flag for envelope/arp consumers.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §3.5` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §3.6` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §3.4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `lfo_sh`.

## Scope

- cycleEdge(): true for one tick on the phase wrap (H->L) boundary (§3.6)
- Random: reload shReg_ with uniform pseudo-random [-1,1] on each cycle edge using seeded POD rngState_ (deterministic) (§3.5)
- Noise: return the injected shared white-noise sample via setNoiseSource; never a private generator (§3.5)
- reset(): phase->0 and S/H reload; resetPhaseOnKey() hook (§3.3)
- value() reflects last tick output

## Out of scope

- kModBusLpHz LPF application (ModRouting task)
- Arp edge-advance logic (control-rate doc)
- Defining the white-noise generator

## Acceptance criteria

- [ ] Random value changes only on cycle edges, is uniform, and is deterministic for a fixed seed (§3.5 acceptance hook, golden-reproducible)
- [ ] Noise uses the injected source, not a private generator (§3.5)
- [ ] cycleEdge() pulses exactly one tick per LFO cycle; ctest --preset default -R lfo_sh --no-tests=error passes; tests lfo_sh_*

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R lfo_sh --no-tests=error
```
