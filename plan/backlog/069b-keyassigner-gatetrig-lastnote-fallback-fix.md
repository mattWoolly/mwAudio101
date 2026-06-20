<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 069b
title: Fix KeyAssigner GateTrig last-note fallback (most-recent-held, not lowest-held)
status: in-review
depends-on: [069]
component: core
estimated-size: S
stream: voice-control
tag: keyassignerfix
---

## Objective

Correct a genuine production bug in `core/voice/KeyAssigner.cpp` surfaced by the task-153
golden-trace conformance battery: in GATE+TRIG (last-note) mode, when the active key is
released with NO new key down this tick, `resolve()` falls back to `lowestHeld()` but the
NORMATIVE §5.3 rule is the **most-recently-pressed still-held key**. Production must match
the §5.3 wording and the task-152 oracle (`tests/golden/KeyAssignerReference`) exactly.

## Context (the bug, already diagnosed)

- `docs/design/04-voice-and-control.md §5.3`: "Last-note priority (GateTrig): ... if no new
  down this tick, the active note stays the **most-recently-pressed still-held key**." Also
  §5.4 K3 (last-note priority).
- `core/voice/KeyAssigner.cpp` `resolve()` GATE+TRIG branch, the `else` arm currently does
  `active = lowestHeld(held_);` — WRONG. The oracle `tests/golden/KeyAssignerReference.h`
  does `active = mostRecentHeld();` in the identical arm. The bitset has no arrival order,
  which is why the bug exists.
- Minimal repro (single-key, GateTrig): press 60, 64, 67 (active=67), then release 67 with
  no new down → production picks 60; §5.3 + oracle pick **64**. Tasks 069/152 missed it
  because their fallback fixtures only ever left ONE key held (lowestHeld == mostRecentHeld).
- The oracle's representation (independent by design, do NOT include it from production):
  `pressSerial_[128]` stamped with an increasing `nextSerial_` (from 1) on each FRESH press;
  `mostRecentHeld()` = the held key with the greatest press serial; reset clears to 0 / 1.

## Scope (surgical — match the oracle's semantics in production)

- Edit `core/voice/KeyAssigner.h`: add `#include <array>`/`#include <cstdint>`; add private
  `std::array<std::uint32_t,128> pressSerial_{};` and `std::uint32_t nextSerial_ = 1;`.
- Edit `core/voice/KeyAssigner.cpp`:
  - `reset()`: zero `pressSerial_`, set `nextSerial_ = 1`.
  - `noteOn(n)`: on a FRESH press (key not already held) stamp `pressSerial_[n] = nextSerial_++;`
    BEFORE setting the bit (re-press of an already-held key does NOT re-stamp — match oracle).
  - add an anonymous-namespace/private `mostRecentHeld()` that returns the held note with the
    max press serial, or -1 if none (bounded O(128), noexcept, no allocation).
  - change ONLY the GATE+TRIG `else` arm: `lowestHeld(held_)` -> `mostRecentHeld()`. Do NOT
    touch the Gate/Lfo lowest-note path (K1/K2/K5), the hasNewDown arm, retrigger, or clockReset.
- Keep all hot-path methods `noexcept`, allocation-free, lock-free, O(128)-bounded (§3.4, §5.3).

## Out of scope

- The full 153 conformance battery (lands separately as PR #107, the regression pin).
- Any Gate/Lfo behavior; POLY/UNISON; retrigger or clockReset logic.
- Editing tests/CMakeLists.txt, Calibration.h, the labels snapshot, or any other shared file.

## Acceptance criteria

- [ ] In GateTrig, releasing the active key with other keys held selects the most-recently-pressed still-held key (60,64,67 → release 67 → 64), and the unwind chain continues last-note (release 64 → 60) [§5.3; §5.4 K3].
- [ ] Gate/Lfo lowest-note behavior (K1/K2/K5) and all retrigger/clockReset outputs are UNCHANGED (assert a Gate-mode release still yields next-lowest) [§5.4 K2].
- [ ] New regression test under tests/unit/, case names begin with `keyassignerfix`, covering the repro + the unwind chain + a Gate-mode no-regression check; built green; full core suite still green (modulo the expected labels_snapshot regen for the new tag).
- [ ] resolve()/noteOn remain noexcept + allocation-free.

## Verification commands

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --preset default && cmake --build --preset default
ctest --preset default -R keyassignerfix --no-tests=error --output-on-failure
ctest --preset default --no-tests=error
```
