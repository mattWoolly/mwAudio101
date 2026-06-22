<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 180
title: LFO multi-destination routing fix — apply all three legs simultaneously (ADR-029 / QA-154-3)
status: in-review
depends-on: [162, 154]
component: dsp
estimated-size: S
stream: dsp
tag: dispatch_lfo
---

## Objective

Fix the confirmed LFO routing regression (QA-154-3, HIGH): the as-built dispatch gates
lfo.depth_{pitch,pwm,cutoff} behind a single lfo.dest switch (applies ONE leg per tick), contradicting
the ratified ADR-007 always-active three-destination topology. Restore simultaneous multi-destination
routing per ADR-029.

## Context

- `plan/decisions/029-lfo-multidest-routing-correction.md` — the decision (read first).
- `docs/QA-REPORT.md §4.5` (QA-154-3) — the evidence-cited verdict.
- `plan/decisions/007-*.md` §Decision item 1 — the ratified topology (one LFO, three fixed
  destinations, per-destination depths, NO mux).
- `docs/design/05 §3.3` (routing table, "always"), `docs/design/03 §3.6` (fixed per-dest depths),
  `docs/design/06 ~L381` (lfo.dest "emphasizes").
- `core/voice/Voice.cpp ~L240-270` — the fix site: the `switch (c.lfoDest)` at ~L252-256 (delete it;
  apply all three legs), and the correct always-on `vcf.lfo_mod` leg at ~L264 (the template).
- `core/voice/ControlDispatchLfoConstants.h L26-31` + `Voice.cpp L247` — the mis-citation of §3.2 to
  remove/correct (§3.2 is about the LFO WAVEFORM, not destinations).
- `core/params/ParamDefs.h` (lfo.dest, lfo.depth_pitch/pwm/cutoff) — param decls (do NOT change the
  91-param count; lfo.dest stays a live param).
- `core/calibration/Calibration.h` — home for any (PI) emphasis-gain constant.

## Scope

- Delete the single-leg `switch (c.lfoDest)` in Voice.cpp; apply ALL THREE legs every control tick:
  VCO pitch (× lfo.depth_pitch), PWM (× lfo.depth_pwm), VCF cutoff (× lfo.depth_cutoff), each by its
  own depth — mirroring how the sibling vcf.lfo_mod leg is already applied unconditionally.
- Reinterpret lfo.dest as a (PI) EMPHASIS gain, NOT a mux: by default non-destructive (all three
  active, selected = unselected = ×1.0) unless a deliberate emphasis multiplier is introduced — and
  if so it MUST be a documented (PI) constant in Calibration.h (provenance comment). The control must
  never again zero a non-selected destination.
- Correct/remove the §3.2 mis-citation comment (cite ADR-007 / §3.3 / §3.6 instead).
- core/voice/Voice.{h,cpp}, core/voice/ControlDispatchLfoConstants.h, core/calibration/Calibration.h
  (only if a PI emphasis constant is added), and the new test.

## Out of scope

- The seq/arp integration (separate epic, ADR-030 / tasks 181+).
- Preset re-validation (downstream; a separate task).
- Any param add/remove or count change.

## TDD — failing test first (tests/, case names begin 'dispatch_lfo', tag dispatch_lfo)

- A SIMULTANEITY test: with the LFO running and lfo.depth_pitch>0 AND lfo.depth_pwm>0 AND
  lfo.depth_cutoff>0 set at once, MEASURE that ALL THREE destinations move in the rendered output
  (Goertzel at the LFO rate / measured pitch wobble + PWM duty variance + cutoff sweep) — and that
  this FAILS against the old single-leg switch (note the mutation). Non-vacuous: each leg's effect is
  individually detectable.
- Confirm lfo.dest selection does NOT zero the other two legs (set dest=pitch, assert pwm + cutoff
  legs STILL modulate).
- Keep any existing single-dest dispatch test updated to the new contract (it must no longer assert
  exclusive routing).

## Acceptance criteria

- [ ] All three LFO depth legs apply simultaneously each tick, each scaled by its own depth (ADR-007 / §3.3 / §3.6)
- [ ] lfo.dest no longer gates/zeroes non-selected destinations; reinterpreted as a non-destructive (PI) emphasis (any emphasis constant centralizes in Calibration.h with provenance)
- [ ] dispatch_lfo simultaneity test proves all three move at once + is non-vacuous (fails vs the old switch); the §3.2 mis-citation is corrected
- [ ] Full core suite green; verify is 'ctest --preset default -R dispatch_lfo --no-tests=error' + the full suite as the merge gate

## Verification commands

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --preset default && cmake --build --preset default
ctest --preset default -R dispatch_lfo --no-tests=error --output-on-failure
ctest --preset default --no-tests=error
```
