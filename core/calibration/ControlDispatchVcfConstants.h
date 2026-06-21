// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ControlDispatchVcfConstants.h — the (PI) constant set for the
// VCF / Envelope / VCA leg of the ParamSnapshot -> DSP control-dispatch seam (task 161,
// extending the ADR-028 keystone built by task 160).
//
// Per the parallel-fleet conflict-avoidance rule a module's (PI) constants land in a
// DEDICATED calibration header rather than being appended to the shared orchestrator
// core/calibration/Calibration.h, and a sibling task's header (here: 160's
// ControlDispatchConstants.h) is NOT edited [docs/design/00 §1.2; AGENTS.md "(PI)
// discipline"; ADR-028]. The dispatch seam references these by name and NEVER inlines a
// literal at a call site [ADR-020 S13].
//
// Every numeric figure here is (PI) — a *pragmatic invention* / tunable integration
// anchor, bounded by the cited circuit-behavior frames (the IR3109 cutoff CV scaling,
// the 1 V/oct keyboard-track law, the ENV->cutoff depth), NOT a measured SH-101 oracle.

#pragma once

#include "FilterTablesConstants.h"   // mw::cal::vcf::kFcRefHz (cutoff CV anchor, cv=0 -> fcRef)
#include "PitchAssemblyConstants.h"  // mw::cal::pitch::kVoltsPerOctave / kCountsPerOctave

namespace mw::cal::dispatch {

// ---------------------------------------------------------------------------
// mw101.vcf.cutoff (normalized [0,1]) -> filter cutoff CV (volts, 1 V/oct).
//
// LadderFilter::setCutoffCv maps cv = 0 V to the reference cutoff fcRefHz (~1 kHz,
// FilterTablesConstants.h) at 1 V/octave, with fc clamped to [10 Hz, 20 kHz] by the
// table. The cutoff PARAM is a unitless 0..1 "norm freq" pot (ParamDefs); the dispatch
// maps it LINEARLY in the CV (octave) domain across a musically useful span so a sweep
// of the pot sweeps the filter from nearly closed to fully open:
//   cutoffVolts = lerp(kCutoffCvAtZero, kCutoffCvAtOne, cutoff01)
// At kFcRefHz = 1 kHz: -6.0 V => ~16 Hz (effectively closed, clamped at 10 Hz only at
// the very bottom) and +4.32 V => ~20 kHz (fully open). The pot's own log-ish skew
// (cal::skew::kCutoff) already packs resolution into the low end, so a LINEAR CV map of
// the de-skewed engineering value gives the expected exponential-feel cutoff sweep.
// (PI) integration anchors [docs/design/02 §5.2, §6; ADR-003 F-08; ADR-028 item 3].
inline constexpr float kCutoffCvAtZero = -6.0f;   // (PI) — cutoff pot at 0 (nearly closed)
inline constexpr float kCutoffCvAtOne  =  4.32f;  // (PI) — cutoff pot at 1 (~20 kHz, fully open)

// ---------------------------------------------------------------------------
// mw101.vcf.env_mod (normalized [0,1]) -> ENV->cutoff depth, in octaves of CV.
//
// The summed cutoff CV adds env_mod x envLevel x kEnvModOctaves (docs/design/02 §1.2:
// "CUTOFF knob + ... + ENV x EnvDepth"). envLevel is the ADSR contour [0,1]; at full
// env_mod and a fully-open envelope the cutoff opens by kEnvModOctaves octaves above the
// base cutoff CV. Five octaves is a wide, clearly-audible filter-envelope sweep without
// instantly slamming a low base cutoff to the 20 kHz ceiling. (PI) depth anchor
// [docs/design/02 §1.2; ADR-028 item 3 (env->cutoff routed per control tick)].
inline constexpr float kEnvModOctaves = 5.0f;     // (PI) — ENV->cutoff full-depth span (oct)

// ---------------------------------------------------------------------------
// mw101.vcf.kbd_track (normalized [0,1]) -> keyboard-tracking depth.
//
// Keyboard tracking raises the cutoff with note pitch: kbd_track x (note - refNote)
// counts x kVoltsPerCount volts summed into the cutoff CV. At kbd_track = 1 the cutoff
// tracks the keyboard EXACTLY at 1 V/octave (full tracking — the resonant peak moves a
// semitone per key); at 0 the cutoff is fixed regardless of note. The reference note is
// the same A4 anchor the VCO pitch dispatch uses (ControlDispatchConstants.h
// kReferenceMidiNote) so tracking is centered on the tuning reference and a note BELOW
// it lowers the cutoff, ABOVE it raises it. (PI) — full tracking is 1 V/oct by the
// 1 V/oct converter frame [docs/design/02 §1.2; ADR-005; ADR-028 item 3].
// (kReferenceMidiNote lives in ControlDispatchConstants.h, 160's header, which we do not
// edit; the dispatch site passes the note delta so this header carries only the depth law.)

} // namespace mw::cal::dispatch
