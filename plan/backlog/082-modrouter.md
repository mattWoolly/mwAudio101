<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 082
title: ModRouter: fixed LFO/ADSR modulation routing
status: todo
depends-on: [001, 006, 007, 081]
component: core
estimated-size: S
stream: mod-arp-seq
tag: modrouter
---

## Objective

Implement ModRouter: a branch-light, allocation-free resolve() mapping one instantaneous LFO value plus the shared ADSR value into pitch/PWM/cutoff/VCA-tremolo via independent fixed depth gains and the PWM/VCA source switches.

## Context

- `docs/design/05 §3.1` — read first
- `docs/design/05 §3.2` — read first
- `docs/design/05 §3.3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `modrouter`.

## Scope

- core/control/ModRouter.h/.cpp implementing the §3.2 class signature
- resolve() fixed expression per §3.2: pitchMod, cutoffMod=lfo*lfoToCutoff+env*envToCutoff, pwmMod by PwmSource, vcaTremolo (§3.1)
- setPwmSource/setVcaSource/setDepths and prepare(sampleRate) (no alloc after prepare)

## Out of scope

- LFO core generation and ADSR segment generation (consumed as scalar inputs)
- Physical V/oct, Hz/V scaling constants (PI; live in Calibration.h)
- Param ID binding (owned by param-schema)

## Acceptance criteria

- [ ] modrouter test: same lfoValue reaches pitch/PWM/cutoff scaled by independent depths; second envelope absent (§3.1, §3.3 / C1)
- [ ] modrouter test: PWM uses ENV only when source=ENV, LFO only when =LFO, pwmManual when =MANUAL (§3.2 / C2)
- [ ] modrouter test: cutoffMod sums LFO depth + ADSR env depth; vcaTremolo = lfo*lfoToVca (§3.1 / C3)
- [ ] modrouter test: resolve() under an allocation/lock sentinel performs no heap alloc (§10)
- [ ] ctest --preset default -R modrouter --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R modrouter --no-tests=error
```
