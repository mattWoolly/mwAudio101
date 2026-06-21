<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR-028 — ParamSnapshot → DSP control-dispatch seam (the "make the synth respond to its controls" layer)

Status: ACCEPTED (2026-06-20). Context: an integration audit found the Engine receives
`BlockContext::params` (the full 91-param `mw::ParamSnapshot`) but NEVER reads it — `ctx.params`
is dereferenced nowhere in core/. So ~78 of 91 parameters have no audible effect; the DSP blocks
(Oscillator/Filter/Env/Lfo/Vca/Glide/FxChain) are correct and expose per-block setters, but no layer
reads the snapshot and calls those setters. The design docs (00 §4.x) under-specify this seam.

## Decision

1. **The Engine owns control dispatch.** Each CONTROL TICK (the existing VINTAGE ~2 ms / seeded-jitter
   / MODERN sub-block cadence driven by ControlCore), the Engine reads `BlockContext::params`
   (`mw::ParamSnapshot`, registry-indexed) and distributes the values to the active voice(s)' DSP
   block setters. No JUCE crosses the seam (ParamSnapshot is a JUCE-free POD, ADR-001).
2. **Per-voice apply.** The Engine builds/updates a per-voice control state and calls the existing DSP
   setters (osc_.setControls, vcf_.setCutoffCv/setResonance, env_.setParams, lfo_.setRateHz/shape,
   vca_/vcaGate_ level+mode, glide_.setTimeSeconds+mode). The source mixer (saw/pulse/sub/noise levels)
   is applied where the engine sums the oscillator sources (today only `src.saw` is mixed — a bug).
3. **Modulation routing is aggregated per control tick:** the CVs handed to the VCO pitch and VCF
   cutoff are the SUM of their base param + the routed modulators — env→cutoff (env_mod), LFO→
   {pitch,pwm,cutoff} per lfo.dest×depth, keyboard tracking (kbd_track × note), velocity→{VCA,VCF},
   pitch-bend→{VCO,VCF} per bend_dest, mod-wheel→LFO. The circuit-accurate VCO pitch authority remains
   ControlCore's count-domain CV (ADR-005); the dispatch applies it to the oscillator.
4. **RT-safe:** dispatch reads a POD snapshot + does arithmetic + calls setters — NO heap alloc, NO
   lock on the audio thread (AudioThreadGuard-clean). Structural params (quality/voice.mode/voice.count/
   unison.count/control.vintage) stay off-thread (existing setters), NOT read per-block.
5. **FX params** (fx.bypass + drive/chorus/delay enables+params) are decoded from the snapshot into
   `FxParams` and applied via `fx_.setParams()` once per block (separate site from the per-voice path).

## Consequences

- Realized by tasks 160 (VCO+mixer, the keystone seam; supersedes 073b), 161 (VCF+Env+VCA + their
  modulation), 162 (LFO + full modulation routing), 163 (FX param dispatch), 164 (analog character:
  drift/vintage/variance/tune/warmup/expression/velocity/MPE). 160→161→162 are SERIAL (shared dispatch
  path); 163 is parallel (separate FX site); 164 follows.
- Every such task MUST ship an AUDIO-EFFECT test (the class of assertion that was missing): changing a
  control produces the audible change (pitch ratio, cutoff sweep, env time, mix balance, etc.), not just
  "non-silent/deterministic". A final completeness audit (165) re-checks all 91 params have effect.
