<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 069
title: KeyAssigner.h/.cpp — bit-faithful note-priority/retrigger state machine
status: todo
depends-on: [001, 006, 007, 067]
component: core
estimated-size: M
stream: voice-control
tag: keyassigner
---

## Objective

Implement the allocation-free KeyAssigner: a 128-bit held-note set + prior-scan snapshot that resolves lowest/last-note priority, retrigger, and CLOCK RESET coupled to the single GateTrigMode (S7) selector, one decision per control tick.

## Context

- `docs/design/04-voice-and-control.md §5.1` — read first
- `docs/design/04-voice-and-control.md §5.2` — read first
- `docs/design/04-voice-and-control.md §5.3` — read first
- `docs/design/04-voice-and-control.md §5.4` — read first
- `ADR-006 §Contract C1-C7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `keyassigner`.

## Scope

- core/voice/KeyAssigner.h/.cpp matching the §5.2 class signature exactly
- noteOn/noteOff update std::bitset<128> held_ in arrival order; resolve() runs once per control tick and snapshots prevScan_=held_
- lowest-note priority (Gate, Lfo): low->high scan, first held wins (§5.3)
- last-note priority (GateTrig): changedDown = held_ & ~prevScan_, pick lowest of just-pressed; else keep most-recent still-held (§5.3, K4)
- retrigger and clockReset emission per the §5.4 NORMATIVE table (K1-K7); CLOCK RESET on new keypress while mode==Lfo (K6)
- reset()/prepare() clear all state; anyHeld() accessor

## Out of scope

- the golden-trace reference and the K17 conformance test (separate tasks)
- POLY allocation (POLY bypasses KeyAssigner)
- 6-bit pitch CV assembly (ControlCore §7); KeyAssigner emits activeNote as a MIDI note
- unison fan-out / detune (VoiceManager §6.3)

## Acceptance criteria

- [ ] K1/K2: Gate keeps gate asserted (no retrigger) on new key while held; active note tracks lowest still-held on release (§5.4 K1-K2; ADR-006 C1-C2)
- [ ] K3/K4: GateTrig new key retriggers; multiple downs in one tick resolve to lowest-of-just-pressed (XOR changed-down) with exactly one retrigger (§5.4 K3-K4)
- [ ] K5/K6: Lfo uses lowest-note pitch but does NOT retrigger from the key; new keypress sets clockReset=true (§5.4 K5-K6)
- [ ] K7: all keys released de-asserts gate (§5.4 K7); coupling enforced — no path yields last-note priority without retrigger (§5.4 deliberate-omission)
- [ ] resolve()/noteOn/noteOff are noexcept and the scan is bounded O(128); test names begin with the tag; verify: ctest --preset default -R keyassigner --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R keyassigner --no-tests=error
```
