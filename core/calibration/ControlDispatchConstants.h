// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ControlDispatchConstants.h — the (PI) constant set for the
// ParamSnapshot -> DSP control-dispatch seam (task 160, the ADR-028 keystone).
//
// Per the parallel-fleet conflict-avoidance rule, this module's (PI) constants land in a
// DEDICATED calibration header rather than being appended to the shared orchestrator
// core/calibration/Calibration.h [docs/design/00 §1.2; AGENTS.md "(PI) discipline"]. The
// dispatch seam references these by name and NEVER inlines a literal at a call site.
//
// Every numeric figure here is (PI) — a *pragmatic invention* / tunable integration
// anchor, bounded by the cited circuit-behavior frames, NOT a measured SH-101 oracle.

#pragma once

#include "VcoConstants.h"            // mw::cal::vco::kPitchRefVolts, footageOffsetV, Footage

namespace mw::cal::dispatch {

// ---------------------------------------------------------------------------
// VCO pitch-CV anchor (the count-domain -> VCO-CV-frame reconciliation).
//
// ADR-005 assembles pitch in the integer DAC-count domain (1 count == 1 semitone) and
// ControlCore::countsToVolts maps 12 counts -> 1 V. The CEM3340 VCO converter
// (VcoConstants.h) instead expresses pitch as
//   freqHz = kPitchRefHz * 2^(pitchCvVolts - kPitchRefVolts + footageOffsetV)
// so its CV frame has a DIFFERENT zero-reference than the raw count-domain volts. Both
// frames are 1 V/octave, so a DIFFERENCE in counts maps to the same octave difference in
// either frame; only the absolute anchor differs. The dispatch reconciles them by
// shifting the count-domain volts so the REFERENCE NOTE at the 8' reference footage lands
// exactly on the VCO reference pitch (kPitchRefHz), i.e.
//   pitchCvVolts = countsToVolts(noteCounts) - countsToVolts(kReferenceMidiNote)
//                  + kPitchRefVolts
// This keeps note 48 vs 72 exactly 24 counts == 2 octaves == 4x apart (the headline
// 1V/oct fix) while honoring the VCO's own footage offset for the range switch (the
// footage is NOT folded into the count assembly here, so it is never double-applied).
// (PI) integration anchor — A4 = MIDI note 69 is the standard 440/442 tuning reference
// [docs/design/01 §4.3; docs/design/04 §7.3; ADR-005 §Decision item 2; ADR-028 item 3].
inline constexpr int kReferenceMidiNote = 69;   // A4 — the 8' tuning-reference key

// ---------------------------------------------------------------------------
// mw101.vco.range choice (6 positions) -> (Footage, extra octave offset).
//
// The DSP Footage register has only the four hardware positions 16'/8'/4'/2' (there are
// NO 32'/64' registers — frozen correction, VcoConstants.h / docs/design/01 §4.4). The
// schema's vco.range is a SOFTWARE-EXTENDED 6-choice param (kVcoRange, isSoftwareExt):
// indices 4 (32') and 5 (64') are software-only octaves BELOW 16'. The dispatch expresses
// them by selecting Footage::Sixteen and applying an EXTRA whole-octave CV offset (in
// volts == octaves at 1 V/oct) on top of the footage offset, so the 4-position DSP enum
// stays in its valid domain and the extra octaves ride the pitch CV.
//   index 0 = 16'  -> Footage::Sixteen, +0 oct
//   index 1 =  8'  -> Footage::Eight,   +0 oct   (reference)
//   index 2 =  4'  -> Footage::Four,    +0 oct
//   index 3 =  2'  -> Footage::Two,     +0 oct
//   index 4 = 32'  -> Footage::Sixteen, -1 oct   (one octave below 16')
//   index 5 = 64'  -> Footage::Sixteen, -2 oct   (two octaves below 16')
// (PI) — the 32'/64' extension is a labeled MODERN addition, not SH-101 fidelity
// [docs/design/06 §3.4 software-ext indices; ADR-008 C6; ADR-028].

struct RangeMapping {
    mw101::dsp::Footage footage;
    double              extraOctaves;   // additional CV octaves (volts) on top of footage
};

[[nodiscard]] inline constexpr RangeMapping rangeMappingFor(int choiceIndex) noexcept {
    switch (choiceIndex) {
        case 0:  return { mw101::dsp::Footage::Sixteen,  0.0 };   // 16'
        case 1:  return { mw101::dsp::Footage::Eight,    0.0 };   //  8' (reference)
        case 2:  return { mw101::dsp::Footage::Four,     0.0 };   //  4'
        case 3:  return { mw101::dsp::Footage::Two,      0.0 };   //  2'
        case 4:  return { mw101::dsp::Footage::Sixteen, -1.0 };   // 32' (16' - 1 oct)
        case 5:  return { mw101::dsp::Footage::Sixteen, -2.0 };   // 64' (16' - 2 oct)
        default: return { mw101::dsp::Footage::Eight,    0.0 };   // defensive reference
    }
}

} // namespace mw::cal::dispatch
