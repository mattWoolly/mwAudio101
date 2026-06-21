// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ControlDispatchCharacterConstants.h — the (PI) constant set for the
// analog-character / tuning / expression leg of the ParamSnapshot -> DSP control-dispatch
// seam (task 164; ADR-028). This leg wires the existing drift / vintage / variance model
// (core/dsp/drift/DriftModel) + the tune.a4 reference + amp.expression into the same
// once-per-control-tick dispatch the keystone (160) built and 161/162/163 extended.
//
// Per the parallel-fleet conflict-avoidance rule, this leg's (PI) constants land in a
// DEDICATED calibration header rather than being appended to the shared orchestrator
// core/calibration/Calibration.h [docs/design/00 §1.2; AGENTS.md "(PI) discipline"]. The
// dispatch references these by name and NEVER inlines a literal at a call site.
//
// Every numeric figure here is (PI) — a *pragmatic invention* / tunable integration
// anchor, bounded by the cited circuit-behavior frames, NOT a measured SH-101 oracle.

#pragma once

#include "VcoConstants.h"             // mw::cal::vco::kPitchRefHz (the 442 Hz home)
#include "PitchAssemblyConstants.h"   // mw::cal::pitch::kVoltsPerOctave / kCountsPerOctave

namespace mw::cal::character {

// ---------------------------------------------------------------------------
// Cents -> CV-volts conversion (the drift/cal model speaks CENTS, the VCO/VCF CV frame
// speaks VOLTS at 1 V/octave). 1200 cents == 1 octave == kVoltsPerOctave volts, so a
// cents offset maps to volts as cents * (kVoltsPerOctave / 1200). Centralized so neither
// the pitch nor the cutoff drift summation inlines the 1/1200 magic literal.
// (PI)-free: a pure unit identity bounded by the 1 V/oct law (ADR-005; §7.3).
// ---------------------------------------------------------------------------
inline constexpr int    kCentsPerOctave = 100 * mw::cal::pitch::kCountsPerOctave; // 1200
inline constexpr double kVoltsPerCent   =
    mw::cal::pitch::kVoltsPerOctave / static_cast<double>(kCentsPerOctave);       // 1/1200 V

[[nodiscard]] inline constexpr double centsToVolts(double cents) noexcept {
    return cents * kVoltsPerCent;
}

// ---------------------------------------------------------------------------
// tune.a4 -> pitch-reference CV offset.
//
// The CEM3340 VCO converter homes on kPitchRefHz (442 Hz) at the 8' reference
// (VcoConstants.h §4.3): freqHz = kPitchRefHz * 2^(pitchCvVolts - kPitchRefVolts + ...).
// The schema's mw101.tune.a4 (400..460 Hz, default 440) is the user's A4 tuning
// reference. To make the rendered A4 land on `a4Hz` instead of the hardware 442, the
// dispatch adds log2(a4Hz / kPitchRefHz) OCTAVES (== volts at 1 V/oct) onto every voice's
// pitch CV. a4 == 442 => 0 offset (the hardware-accurate home, an exact identity); a4 ==
// 440 => a small flat shift of log2(440/442) ~= -0.00654 oct; a4 == 460 => sharp. The
// reference shift is a pure global CV bias — it never re-anchors the 1 V/oct note spacing.
// (PI)-free: the mapping is the exponential converter's own inverse [docs/design/01 §4.3].
// ---------------------------------------------------------------------------
inline constexpr double kHardwareRefHz = mw::cal::vco::kPitchRefHz;   // 442 Hz home

// ---------------------------------------------------------------------------
// amp.expression (mw101.amp.expression, CC11) -> VCA output scaler.
//
// A clean linear post-VCA amplitude scale folded beside the vca.level fader and the
// velocity scale (Voice render()), so a patch driven by an expression pedal/CC11 scales
// the whole voice output. The param itself reaches the seam (schema default 1.0 == unity,
// no attenuation), so this leg is DIRECTLY AUDIBLE (unlike the bend-wheel / MPE position
// ingress, which is a separate controller seam). kExpressionAtZero is the floor the 0..1
// param maps to: 0 => silence, 1 => unity. A pure pass-through (no PI shaping) keeps the
// CC11 curve the host/bridge's responsibility. (PI) floor anchor only.
// ---------------------------------------------------------------------------
inline constexpr float kExpressionMin = 0.0f;   // expression 0 => silent
inline constexpr float kExpressionMax = 1.0f;   // expression 1 => unity (schema default)

} // namespace mw::cal::character
