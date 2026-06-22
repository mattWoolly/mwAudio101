// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ControlDispatchLfoConstants.h — the (PI) constant set for the
// LFO + modulation-routing leg of the ParamSnapshot -> DSP control-dispatch seam
// (task 162, extending the ADR-028 keystone built by task 160 / 161).
//
// Per the parallel-fleet conflict-avoidance rule a module's (PI) constants land in a
// DEDICATED calibration header rather than being appended to the shared orchestrator
// core/calibration/Calibration.h, and a sibling task's header (160's
// ControlDispatchConstants.h / 161's ControlDispatchVcfConstants.h) is NOT edited
// [docs/design/00 §1.2; AGENTS.md "(PI) discipline"; ADR-028]. The dispatch seam
// references these by name and NEVER inlines a literal at a call site [ADR-020 S13].
//
// Every numeric figure here is (PI) — a *pragmatic invention* / tunable integration
// anchor, bounded by the cited circuit-behavior frames (the LFO->pitch vibrato depth,
// the LFO->cutoff wobble depth, the LFO->PWM sweep depth, the velocity->VCA/VCF depths,
// the pitch-bend cents->CV law), NOT a measured SH-101 oracle.

#pragma once

#include "PitchAssemblyConstants.h"  // mw::cal::pitch::kVoltsPerCount / kCountsPerOctave

namespace mw::cal::dispatch {

// ---------------------------------------------------------------------------
// LFO destination select (mw101.lfo.dest choice {Pitch=0, Filter=1, PWM=2},
// ParamDefs detail::kLfoDest).
//
// CORRECTION (task 180 / ADR-029 / QA-154-3): lfo.dest is NOT an exclusive mux. The
// ratified topology (ADR-007 §Decision item 1) routes the single instantaneous LFO
// value into THREE FIXED DESTINATIONS with per-destination depth gains, ALL ACTIVE
// every control tick — VCO pitch, PWM, VCF cutoff [docs/design/05 §3.3 marks LFO->VCO
// pitch and LFO->VCF cutoff "always"; docs/design/03 §3.6 "Routing is fixed with
// per-destination depths, not a matrix"]. Per docs/design/06 ~L381 lfo.dest selects
// which destination the single hardware LFO routing **EMPHASIZES** — an emphasis
// selector, NOT a single-position switch that silences the others. (The earlier
// single-leg gating mis-cited docs/design/03 §3.2, which governs the LFO *waveform*
// being single-select — four positions, no sine core — and says nothing about
// *destinations*.) The decode below is retained for the emphasis gain only; it never
// zeroes a non-selected leg. (PI) index mapping mirrors the choice-label order.
enum class LfoDest : int { Pitch = 0, Filter = 1, Pwm = 2 };

[[nodiscard]] inline constexpr LfoDest lfoDestFor(int choiceIndex) noexcept {
    switch (choiceIndex) {
        case 1:  return LfoDest::Filter;
        case 2:  return LfoDest::Pwm;
        default: return LfoDest::Pitch;
    }
}

// lfo.dest EMPHASIS gains (task 180 / ADR-029 item 2). The selected destination's depth
// is multiplied by kLfoEmphasisSelected and the unselected ones by kLfoEmphasisUnselected.
// Both default to 1.0 so the control is NON-DESTRUCTIVE: every destination modulates at its
// own depth REGARDLESS of dest, and dest changes nothing until a deliberate emphasis curve is
// ratified by a future ADR (then only these two (PI) figures move — no call site re-tunes).
// The unselected gain MUST stay > 0 (the control may never again zero a destination — that was
// the QA-154-3 regression) [docs/design/06 ~L381 "emphasizes"; ADR-029 item 2]. (PI) anchors.
inline constexpr float kLfoEmphasisSelected   = 1.0f;   // (PI) — selected-dest depth multiplier
inline constexpr float kLfoEmphasisUnselected = 1.0f;   // (PI) — non-selected (MUST be > 0)

// Per-leg emphasis multiplier for `leg` given the selected destination `dest`: the selected
// leg gets kLfoEmphasisSelected, every other leg kLfoEmphasisUnselected (both 1.0 by default).
[[nodiscard]] inline constexpr float lfoEmphasisGain(LfoDest dest, LfoDest leg) noexcept {
    return (dest == leg) ? kLfoEmphasisSelected : kLfoEmphasisUnselected;
}

// ---------------------------------------------------------------------------
// mw101.lfo.shape choice {Tri=0, Sq=1, Random=2, Noise=3, Sine=4} -> dsp::LfoShape.
//
// The DSP LfoShape has only FOUR positions (SmoothTri / Square / Random / Noise —
// the four 1982-hardware positions; the 5th "Sine" choice is a software-reissue
// SH-01A artifact, docs/design/03 §3.2). The dispatch maps the schema's 5-choice
// param onto the 4-position DSP enum: index 4 ("Sine") selects SmoothTri (the
// "rounded toward sine" triangle is the closest hardware-true smooth shape — there
// is no separate sine core, §3.3). (PI) integration mapping [ADR-028 item 3].
//   index 0 = Tri    -> SmoothTri
//   index 1 = Sq     -> Square
//   index 2 = Random -> Random
//   index 3 = Noise  -> Noise
//   index 4 = Sine   -> SmoothTri (rounded-toward-sine; no separate sine core)
// (left as a free function in the dispatch .cpp because LfoShape lives in the
// mw101::dsp namespace; this header carries only the (PI) depth/rate anchors.)

// ---------------------------------------------------------------------------
// LFO->Pitch vibrato depth (mw101.lfo.depth_pitch [0,1]) -> peak pitch CV deviation.
//
// At full depth the LFO swings the pitch CV by +-kLfoPitchDepthSemis semitones around
// the held note (a clearly-audible vibrato). Expressed in CV volts via kVoltsPerCount
// (1 count == 1 semitone) so it sums into the same 1 V/oct pitch CV the count-domain
// authority assembles. kLfoPitchDepthSemis = 1 semitone is a musical max vibrato that
// is unambiguous on a Goertzel sweep without detuning into the next note's band. (PI)
// depth anchor [docs/design/03 §3.5; ADR-028 item 3 (LFO->pitch routed per tick)].
inline constexpr float kLfoPitchDepthSemis = 1.0f;   // (PI) — full-depth vibrato (semitones)
inline constexpr float kLfoPitchDepthVolts =
    kLfoPitchDepthSemis * static_cast<float>(mw::cal::pitch::kVoltsPerCount);

// ---------------------------------------------------------------------------
// LFO->Cutoff wobble depth (mw101.lfo.depth_cutoff [0,1]) -> peak cutoff CV deviation.
//
// At full depth the LFO swings the filter cutoff CV by +-kLfoCutoffDepthOctaves octaves
// around the base cutoff (the classic filter-wobble). Summed into the SAME cutoff CV
// (volts, 1 V/oct) the 161 leg assembles, so it rides on top of the base cutoff +
// env_mod + kbd_track terms. Two octaves is a wide, clearly-audible sweep without
// instantly slamming the table ceiling. (PI) depth anchor [docs/design/02 §1.2;
// docs/design/03 §3.5; ADR-028 item 3 (LFO->cutoff routed per tick)].
inline constexpr float kLfoCutoffDepthOctaves = 2.0f;   // (PI) — full-depth wobble (oct)

// ---------------------------------------------------------------------------
// LFO->PWM sweep depth (mw101.lfo.depth_pwm [0,1]) -> peak pulse-width CV deviation.
//
// At full depth the LFO swings the normalized PWM CV by +-kLfoPwmDepthNorm around the
// base pulse width (the classic pulse-width-modulation chorus sweep). The PWM CV is the
// 0..1 normalized duty control the oscillator section maps to a duty; the swing is
// clamped to [0,1] at the apply site so a base width near an edge does not wrap. (PI)
// depth anchor [docs/design/01 §PWM; docs/design/03 §3.5; ADR-028 item 3].
inline constexpr float kLfoPwmDepthNorm = 0.45f;   // (PI) — full-depth PWM swing (norm)

// ---------------------------------------------------------------------------
// LFO delay (mw101.lfo.delay [0,1] "time") -> a fade-IN time for the LFO depth.
//
// The MOD section's DELAY fades the LFO modulation in over a time after the keypress
// (so a held note's vibrato/wobble swells in rather than starting at full depth). The
// param is a normalized 0..1 "time"; the dispatch maps it LINEARLY to seconds across
// [0, kLfoDelayMaxSec] and ramps a per-voice depth-scale 0->1 over that time from the
// note's keypress. kLfoDelayMaxSec = 3 s is a generous musical maximum. (PI) anchor
// [docs/design/03 §3.x MOD DELAY; ADR-028 item 3].
inline constexpr float kLfoDelayMaxSec = 3.0f;   // (PI) — full LFO-delay fade-in (s)

// ---------------------------------------------------------------------------
// Velocity -> VCA depth (mw101.vel.depth [0,1], gated by mw101.vel.enable).
//
// Velocity scales the VCA amplitude when enabled: the per-note velocity [0,1] is folded
// into the VCA control as control *= (1 - depth) + depth*velocity, so at depth 0 velocity
// has no effect (full level for any key) and at depth 1 a soft key (vel~0) is near-silent
// while a hard key (vel=1) is full. kVelVcaDepthMax = 1 lets the param reach full velocity
// sensitivity. (PI) — velocity->VCA is the documented default routing [docs/design/03
// §5; research/04 §4.4; ADR-028 item 3 (velocity->{VCA,VCF})].
inline constexpr float kVelVcaDepthMax = 1.0f;   // (PI) — velocity->VCA full-scale depth

// ---------------------------------------------------------------------------
// Velocity -> VCF cutoff depth (mw101.vel.depth, gated by mw101.vel.enable).
//
// Velocity also opens the filter when enabled: a hard key raises the cutoff CV by up to
// kVelCutoffOctaves octaves (scaled by vel.depth x velocity), summed into the SAME cutoff
// CV the 161 leg assembles. kVelCutoffOctaves = 2 is a clearly-audible velocity->brightness
// without dominating the env/kbd terms. (PI) depth anchor [docs/design/03 §5; ADR-028
// item 3 (velocity->{VCA,VCF})].
inline constexpr float kVelCutoffOctaves = 2.0f;   // (PI) — velocity->cutoff full-depth (oct)

} // namespace mw::cal::dispatch
