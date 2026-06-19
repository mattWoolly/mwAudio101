<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 071
title: ControlCore driver — control-tick advance, VINTAGE/MODERN poles, jitter, auto-engage, crossfade
status: done
depends-on: [001, 006, 007, 067, 069, 070]
component: core
estimated-size: M
stream: voice-control
tag: controlcore
---

## Objective

Implement the ControlCore advance() driver: a processBlock sample-counter that fires control ticks (VINTAGE fixed ~2 ms / optional seeded jitter / MODERN clean sub-block tick), applies effective-pole auto-engage, and crossfades CV when the macro is automated, clocking the KeyAssigner via the VoiceManager.

## Context

- `docs/design/04-voice-and-control.md §7.1` — read first
- `docs/design/04-voice-and-control.md §7.4` — read first
- `docs/design/04-voice-and-control.md §7.5` — read first
- `docs/design/04-voice-and-control.md §7.6` — read first
- `docs/design/04-voice-and-control.md §7.7` — read first
- `docs/design/04-voice-and-control.md §7.9` — read first
- `ADR-005 §Contract CC1-CC7` — read first
- `ADR-016 §R-1` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `controlcore`.

## Scope

- core/voice/ControlCore.{h,cpp} adding prepare/setPole/setJitterEnabled/advance(numSamples,VoiceManager&)/effectivePole per §7.8
- advance() driven by sampleCounter_ inside processBlock (no wall-clock/thread); fires ticks at VINTAGE ~2 ms (Calibration.h) or MODERN clean sub-block tick (PI 16-32 smp, Calibration.h) (§7.4-§7.5)
- loop-time jitter via seeded XorShift32 over 1.5-3.5 ms envelope, OFF by default; jitter-OFF fixed-tick VINTAGE is the bit-exact reference (§7.4; CC1/CC2)
- effectivePole(mode,mpeActive,pitchAutomated): MODERN auto-engages when mode!=Mono OR mpeActive OR pitchAutomated, even if macro=Vintage (§7.6; CC4-CC6)
- macro-automation VINTAGE<->MODERN: precompute both CV branches and crossfade, branchless, no allocation (§7.7; CC7)
- default macroPole_=Modern, jitterOn_=false (ADR-016 R-1)

## Out of scope

- assemblePitchCounts/countsToVolts (voice-control-7 provides them)
- the detailed arp/seq clock edges (ADR-007/022); only CLOCK RESET emission flows through KeyAssigner
- the Vintage Control / jitter parameter IDs (doc 06)
- VoiceManager internals — advance only calls its controlTick

## Acceptance criteria

- [ ] CC1: Mono+VINTAGE+jitter-OFF fires a fixed ~2 ms tick driven by the sample counter, deterministic/bit-exact reference config (§7.4, §7.9 CC1; ADR-005 CC1)
- [ ] CC3 default: out-of-box runs MODERN-SMOOTH clean sub-block tick with jitter OFF (§7.5, §7.9 CC3; ADR-016 R-1)
- [ ] CC4-CC6: effectivePole returns Modern when mode!=Mono OR mpeActive OR pitchAutomated even with macro=Vintage (§7.6, §7.9; ADR-005 CC4-CC6)
- [ ] CC2 jitter: enabling jitter varies the tick within the 1.5-3.5 ms envelope deterministically from a seeded PRNG; jitter-off path is unchanged (§7.4, §7.9 CC2)
- [ ] CC7: automating the macro crossfades both CV branches with no zipper and zero allocation; advance() is noexcept/alloc-free/lock-free (§7.7, §7.9 CC7; ADR-005 invariants)
- [ ] test names begin with the tag; verify: ctest --preset default -R controlcore --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R controlcore --no-tests=error
```
