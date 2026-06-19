// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/VcoConstants.h — (PI) calibration constants for the CEM3340-
// modeled VCO phase core / exponential 1V/oct converter / footage offsets / drift
// model (task 029).
//
// Per the parallel-fleet conflict-avoidance rule, a module's (PI) constants land in
// a DEDICATED calibration header rather than being appended directly to the shared
// core/calibration/Calibration.h orchestrator. This header #includes Calibration.h
// and EXTENDS the mw::cal::vco / mw::cal::drift / mw::cal::warmup namespaces that
// Calibration.h declares as empty placeholders for task 029; the orchestrator wires
// the Calibration.h include of this header in later. DSP sources reference these
// constants here and NEVER inline the literals at a call site
// [docs/design/01-dsp-oscillators.md §4.3, §4.4, §4.7, §10 "(PI) centralization";
// docs/design/00 §8.3; ADR-008 §1; AGENTS.md "centralize it"].
//
// Every numeric figure here is (PI) — a *pragmatic invention* / tunable default
// bounded by the cited CEM3340 datasheet figures, NOT a measured SH-101 oracle.

#pragma once

#include "Calibration.h"

namespace mw101::dsp {

// VCO footage / range switch positions [docs/design/01 §4.4; research/02 §2.4, §6].
// Four positions ONLY — 16'/8'/4'/2'. There are NO 32'/64' registers (frozen
// correction, §4.4). 8' is the A4 = 442 Hz tuning reference; the others are pure CV
// octave offsets about it. The enum values double as the schema choice index.
enum class Footage : unsigned char {
    Sixteen = 0,   // 16' — lowest, -1 oct about the 8' reference
    Eight   = 1,   // 8'  — reference (A4 = 442 Hz),  0 oct
    Four    = 2,   // 4'  — +1 oct
    Two     = 3    // 2'  — +2 oct
};

} // namespace mw101::dsp

namespace mw::cal::vco {

// ---------------------------------------------------------------------------
// Exponential 1V/oct converter anchors [docs/design/01 §4.3].
//   freqHz = kPitchRefHz * 2^(pitchCvVolts - kPitchRefVolts + footageOffsetV + driftScale)
// ---------------------------------------------------------------------------

// The tuning reference: A4 = 442 Hz at the 8' range with Transpose = Middle
// [docs/design/01 §4.3; research/02 §2.8, §6]. The "442" figure is the documented
// SH-101 calibration target; carried as (PI) here so the converter reads one home.
inline constexpr double kPitchRefHz = 442.0;

// The summed pitch CV (after footage offset) that corresponds to the reference
// pitch, chosen so 8' + Transpose-Middle + 0-cent tune lands EXACTLY on 442 Hz
// [docs/design/01 §4.3]. (PI) calibration anchor. Sits within the documented
// internal CV span 0.415 V .. 5 V [research/02 §2.8]; the modulation/CV doc owns the
// key/tune/bend/LFO summation that produces pitchCvVolts and the span clamp.
inline constexpr double kPitchRefVolts = 3.0;

// ---------------------------------------------------------------------------
// Footage octave offsets (volts == octaves at 1V/oct) [docs/design/01 §4.4].
// 16'/8'/4'/2' span -1/0/+1/+2 octaves about the 8' = 442 Hz reference; the octave
// switch is a CV offset, NOT an analog divider [research/02 §2.4, §7.1].
// ---------------------------------------------------------------------------
inline constexpr double kFootageOffsetSixteenV = -1.0;
inline constexpr double kFootageOffsetEightV   =  0.0;   // reference
inline constexpr double kFootageOffsetFourV    =  1.0;
inline constexpr double kFootageOffsetTwoV     =  2.0;

// Resolve a footage position to its CV octave offset (the DSP applies this to the
// summed pitch CV). Centralized so no DSP call site inlines the offsets.
[[nodiscard]] inline constexpr double footageOffsetV (mw101::dsp::Footage f) noexcept {
    switch (f) {
        case mw101::dsp::Footage::Sixteen: return kFootageOffsetSixteenV;
        case mw101::dsp::Footage::Eight:   return kFootageOffsetEightV;
        case mw101::dsp::Footage::Four:    return kFootageOffsetFourV;
        case mw101::dsp::Footage::Two:     return kFootageOffsetTwoV;
    }
    return kFootageOffsetEightV;   // unreachable; defensive reference default
}

// ---------------------------------------------------------------------------
// dt (phase-increment) Nyquist clamp [docs/design/01 §4.4].
//   dt_ = min(freqHz / fs, kDtMax)
// kDtMax = 0.5 guarantees at most one wrap per sample (Nyquist) across the audio
// band, so the single-wrap PolyBLEP/minBLEP assumption (§2.1) holds. (PI) safety
// clamp.
// ---------------------------------------------------------------------------
inline constexpr double kDtMax = 0.5;

} // namespace mw::cal::vco

namespace mw::cal::drift {

// ---------------------------------------------------------------------------
// Drift / stability model [docs/design/01 §4.7]. The CEM3340 is intrinsically
// stable (same-die expo + tempco); model only a SMALL, slow drift, NOT large free-
// running wander. These are (PI) tuning values BOUNDED by the cited datasheet
// figures. The default build ships drift effectively at zero (the per-voice
// scaleErr_/offsetErr_ seeds default to 0); the HOOKS must exist regardless. These
// extend the mw::cal::drift placeholder declared in Calibration.h.
// ---------------------------------------------------------------------------

// Max scale (gain) drift, expressed in ppm of the 1V/oct slope [research/02 §2.9,
// +/-50 ppm]. Applied as a fraction-of-an-octave detune at the full seed.
inline constexpr double kDriftScalePpmMax = 50.0;

// Typical residual scale error percentage (kT/q not perfectly cancelled)
// [research/02 §2.9, 0.05% typ]. (PI) — carried for the variance doc to consume.
inline constexpr double kDriftScaleErrPct = 0.05;

// HF (top-octave) tracking flag (CEM3340 pin 7 model). When true, progressive
// top-octave sharpness is compensated [research/02 §2.7, §7.1]. (PI). Part of the
// drift/stability model (§4.7 item (c)), NOT a warm-up transient time constant.
inline constexpr bool kHfTrackEnable = true;

} // namespace mw::cal::drift

namespace mw::cal::warmup {

// ---------------------------------------------------------------------------
// Warm-up transient [docs/design/01 §4.7 item (a)]. Extends the mw::cal::warmup
// placeholder declared in Calibration.h.
// ---------------------------------------------------------------------------

// Warm-up transient time constant: a first-order settle of the tiny scale/offset
// error over "tens of seconds" from a cold start [research/02 §7.1]. (PI).
inline constexpr double kWarmupTauSec = 30.0;

} // namespace mw::cal::warmup
