<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 073b
title: Wire the pitch CV to the VCO so different notes actually play different pitches (CRITICAL)
status: todo
depends-on: [070, 071, 073, 074, 118, 118c]
component: core
estimated-size: L
stream: voice-control
tag: voice_pitch
---

## Objective

CRITICAL DSP GAP (found by audit): the played note's pitch is COMPUTED but NEVER reaches the
oscillator frequency, so distinct MIDI notes render bit-identically — the synth cannot play pitch.
`core/voice/Voice.cpp` Voice::render calls `glide_.nextValue()` and DISCARDS the result; `osc_`'s
frequency is never updated from any pitch CV. `core/control/ControlCore.cpp` computes the
circuit-accurate `blendedPitchVolts` (the VINTAGE 6-bit DAC-count authority, §7.3/ADR-005) but never
applies it to a voice. Wire the pitch CV through to the VCO so note 48 and note 72 render ~4 octaves
apart. Every smoke/determinism/golden test missed this because NONE asserts "different note ->
different pitch" (all use single-note / mono-on-the-same-note).

## Architectural decision (made by orchestrator from the design — implement THIS, do not re-litigate)

ControlCore (tasks 070/071, ADR-005) is the circuit-accurate VCO PITCH AUTHORITY: it assembles the
pitch in the integer DAC-count domain (assemblePitchCounts), where glide/portamento genuinely
stair-steps through 6-bit counts, and converts to volts at the S/H boundary (countsToVolts /
blendedPitchVolts: VINTAGE 6-bit S/H + MODERN continuous, crossfaded by the macro pole). THAT CV must
drive the VCO. The per-Voice `glide_` (Voice §5.5, Hz-domain) is a parallel path that currently also
goes nowhere; in MONO/UNISON, ControlCore's count-domain glide is the authority — reconcile so glide
is applied ONCE (prefer ControlCore's count-domain pipeline; do not double-glide). Document the
reconciliation; if POLY needs per-voice glide, note it but keep MONO correct + circuit-accurate.

## Context (verified by audit — read these first)

- `core/voice/Voice.cpp` Voice::render (the `glide_.nextValue()` discard + `osc_.renderSample()` with
  a stale frequency) and `core/voice/Voice.h` (setGlideTarget, osc_ control surface).
- `core/control/ControlCore.{h,cpp}` — assemblePitchCounts/countsToVolts/blendedPitchVolts (the CV to
  apply) + advance(numSamples, voices) (the control-tick driver that already has the voices).
- `core/dsp/Oscillator.h` — OscillatorSection::setControls / the OscControls.pitchCvVolts field (how
  the VCO frequency is set; 1 V/oct). Confirm the exact setter + units.
- `core/voice/VoiceManager.{h,cpp}` — applyDecisionToVoice / controlTick (where the KeyAssigner's
  resolved active note reaches the voice; the note must drive the pitch-count assembly).
- `docs/design/04 §7.2/§7.3/§7.8` (pitch pipeline), `ADR-005 C1-C7` (VINTAGE/MODERN pitch), `ADR-006`
  (KeyAssigner = sole MONO/UNISON note authority).

## Scope

- Wire the resolved active note -> ControlCore pitch-count assembly -> blendedPitchVolts -> the voice
  VCO each CONTROL TICK (the assembled CV is applied to osc_ via setControls/pitchCvVolts; the VCO
  frequency then tracks the played note, with VINTAGE 6-bit quantization + MODERN continuous + glide).
  This likely lives in ControlCore::advance (which has the voices) and/or VoiceManager::controlTick
  (which has the KeyAssigner decision); pick the seam the design supports and keep it RT-safe
  (noexcept, no heap/lock; the CV apply is a few arithmetic ops + a setter).
- Reconcile the duplicate glide (ControlCore count-domain vs Voice Hz-domain) so glide is applied once.
- Tests (tag `voice_pitch`) — the assertions that were MISSING:
  * Voice/Engine level: note 48 vs note 72 produce DIFFERENT oscillator pitch with the correct ratio
    (assert via zero-crossing rate or FFT fundamental ~ within tolerance of the 1V/oct ratio).
  * A small sweep (e.g. C2/C3/C4/C5) renders monotonically increasing fundamental.
  * VINTAGE: pitch stair-steps on 6-bit counts (quantized); MODERN: continuous. Glide slews between
    two notes (not an instant jump) when portamento is on.
  * Determinism preserved (same note+seed -> same output).

## Out of scope

- The sequencer/arp wiring (118c, done); non-pitch control CVs (cutoff/env-amount/LFO-depth reaching
  their DSP — a SEPARATE systemic audit/follow-up); FX.
- Re-deriving the VINTAGE pitch math (070 owns it); the oscillator anti-alias internals (own tasks).

## Acceptance criteria

- [ ] Playing different MIDI notes produces different VCO fundamental frequencies at the correct 1V/oct ratio (note 48 vs 72 ~ 4 octaves / ~16x), asserted by a real frequency measurement [§7.3; ADR-005]
- [ ] ControlCore's assembled pitch CV (VINTAGE 6-bit S/H + MODERN continuous) drives the VCO; glide is applied exactly once (no double-glide); MONO/UNISON pitch flows from the single KeyAssigner-resolved note [ADR-005; ADR-006]
- [ ] RT-safe: the per-control-tick CV apply adds no heap/lock (AudioThreadGuard clean); hot paths noexcept
- [ ] `voice_pitch` tests cover note->pitch tracking, a monotonic sweep, VINTAGE quantization vs MODERN continuous, and glide slew; full core suite green
- [ ] PR documents the glide reconciliation + the exact seam chosen

## Verification commands

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --preset default && cmake --build --preset default
ctest --preset default -R voice_pitch --no-tests=error --output-on-failure
ctest --preset default --no-tests=error
```
