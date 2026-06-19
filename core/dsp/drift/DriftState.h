// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/drift/DriftState.h — the per-voice DriftState POD plus the CalibrationDraw
// (Tier 1) and NoteOnOffsets (Tier 3 + four variance spreads) structs and the pure
// draw helpers drawCalibration / drawSlopCents / drawNoteOn (task 065).
//
// Realizes docs/design/08 §4 (Tier-1 frozen trimmer draws), §6 (Tier-3 slop), §7
// (frozen-at-note-on variance spreads) and §8.1 (DriftState layout); ADR-009
// §Decision 1/3/4. This file owns the DATA LAYOUT and the PURE DRAW HELPERS only;
// the DriftModel orchestration / processBlock, the ThermalState internals, and the
// OnePoleSmoother definition are OUT OF SCOPE (vintage-4 / vintage-2 / core-types).
//
// REAL-TIME / PROVENANCE invariants (docs/design/08 §12; ADR-009 §Decision 5, VV-16):
//   - DriftState is a trivially-copyable POD so it lives by value inside the
//     pre-allocated DriftState[kMaxVoices] array DriftModel allocates in prepare();
//   - every draw helper is pure + noexcept + heap/lock-free; draws happen at
//     construct / Re-roll (Tier 1) and at note-on (Tier 3 + variance), NEVER per
//     sample [docs/design/08 §12.1, §12.3];
//   - cal.spread = 0 (and var.* = 0) yields ZERO perturbation; for a fixed seed the
//     draws are bit-identical run-to-run [docs/design/08 §4.2, §8.1/§8.2, VV-17].
//
// All band magnitudes are (PI) TUNABLE DEFAULTS sourced from
// core/calibration/DriftConstants.h, NOT measured SH-101 specs [ADR-009 §8].

#pragma once

#include <type_traits>

#include "Xorshift128p.h"
#include "calibration/DriftConstants.h"
#include "params/Smoother.h"

namespace mw::dsp::drift {

// ---------------------------------------------------------------------------
// Tier 1 — frozen per-instance calibration draw [docs/design/08 §4.2].
// ---------------------------------------------------------------------------
struct CalibrationDraw {            // frozen per voice (and per instance for voice 0)
    float tuneCents     = 0.0f;     // VR-7/VR-9 + VR-2 combined, ADDITIVE cents
    float vcfWidthScale = 1.0f;     // VR-8, MULTIPLICATIVE on cutoff CV slope
    float cutoffOffset  = 0.0f;     // uncalibrated cutoff offset, ADDITIVE (cents-equiv)
};

// ---------------------------------------------------------------------------
// Tier 3 + variance — all frozen once per note-on [docs/design/08 §7.2].
// ---------------------------------------------------------------------------
struct NoteOnOffsets {              // all frozen once per note-on
    float slopCents     = 0.0f;     // Tier 3, ADDITIVE cents
    float varCutoff     = 0.0f;     // ADDITIVE, cents-equiv
    float varEnvScale   = 1.0f;     // MULTIPLIER on A/D/R time constants
    float varPw         = 0.0f;     // ADDITIVE duty fraction
    float varGlideScale = 1.0f;     // MULTIPLIER on glide time constant
};

// ---------------------------------------------------------------------------
// Per-voice drift state POD [docs/design/08 §8.1]. One per voice, pre-sized into
// DriftState[kMaxVoices] by DriftModel::prepare (the array lives in DriftModel,
// vintage-4 — out of scope here). Aggregates: the per-voice PRNG, the Tier-1
// calibration draw, the Tier-2 thermal integrator, the Tier-3 + variance note-on
// offsets, the five mandatory per-target de-zipper smoothers, and the active flag.
//
// NOTE on the smoother type: docs/design/08 §8.1/§9 names the canonical de-zipper
// `mw::dsp::OnePoleSmoother`; the realized canonical smoother (task 008) lives at
// core/params/Smoother.h as mw::params::OnePoleSmoother. This struct CONSUMES that
// single existing declaration (defining the smoother is out of scope here); the
// using-alias below records the design-doc name without re-declaring a type.
using OnePoleSmoother = mw::params::OnePoleSmoother;

// ThermalState (Tier 2) is owned by ThermalState.h (vintage-2). It is forward-used
// here only as a by-value member so DriftState matches the §8.1 layout; until that
// header lands the field is a minimal POD placeholder that vintage-2 supersedes.
// (Defining ThermalState internals is explicitly OUT OF SCOPE for task 065.)
struct ThermalStatePlaceholder {
    float  T          = 0.0f;       // bounded OU state, cents-domain normalized [-1,1]
    float  pinkState[7]{};          // optional Voss-McCartney rows (off by default)
    double warmupSec  = 0.0;        // elapsed warm time; <0 == warm-up disabled
};

struct DriftState {                 // POD; one per voice; pre-sized [kMaxVoices]
    Xorshift128p             rng;       // seeded from instance_seed ^ mix(voice_index)
    CalibrationDraw          cal;       // Tier 1, frozen at construct / Re-roll
    ThermalStatePlaceholder  thermal;   // Tier 2 OU integrator (own per voice)
    NoteOnOffsets            noteOn;     // Tier 3 + variance, frozen at note-on
    OnePoleSmoother smPitch, smCutoff, smPw, smEnv, smGlide;  // canonical smoother, §9
    bool                     active = false;
};

// ---------------------------------------------------------------------------
// Draw helpers — pure, noexcept, no alloc. spread01 / var*01 are the 0..1 width
// multipliers (the schema params); a 0 multiplier yields the identity (zero offset
// / unit scale). Draws consume the per-voice PRNG, so a fixed seed => deterministic
// output [docs/design/08 §4.2, §8.2, VV-17].
// ---------------------------------------------------------------------------

// Symmetric bounded uniform draw in [-1, 1) for the tolerance-band perturbations
// ("within service-manual tolerance bands", §4.1; bounded so multiplicative scales
// stay positive). Tier-3 slop separately uses gaussian()/cubic() per kSlopShape.
inline float drawBipolar(Xorshift128p& rng) noexcept {
    return 2.0f * rng.nextFloat01() - 1.0f;   // [-1, 1)
}

// Tier 1 [docs/design/08 §4.1, §4.2]. tuneCents = (VR-7/9 band + VR-2 band),
// ADDITIVE; vcfWidthScale = 1 + draw*band, MULTIPLICATIVE (VR-8 is a SCALE, never an
// offset, and is independent of the cutoffOffset path); cutoffOffset is the WIDEST
// ADDITIVE band. Every band is scaled by spread01, so spread01=0 => identity draw.
inline CalibrationDraw drawCalibration(Xorshift128p& rng, float spread01) noexcept {
    namespace c = mw::cal::drift;
    CalibrationDraw d;
    // Two independent draws for the coupled VCO-Tune + D/A-Tune cents offset.
    const float tune = drawBipolar(rng) * c::kCalBandTuneCents;
    const float dac  = drawBipolar(rng) * c::kCalBandDacCents;
    d.tuneCents     = (tune + dac) * spread01;
    // VR-8: a multiplicative SCALE on the cutoff CV slope (not an offset).
    d.vcfWidthScale = 1.0f + drawBipolar(rng) * c::kCalBandVcfScale * spread01;
    // Uncalibrated cutoff OFFSET — additive, widest Tier-1 band, independent path.
    d.cutoffOffset  = drawBipolar(rng) * c::kCalBandCutoffOffset * spread01;
    return d;
}

// Tier 3 [docs/design/08 §6; ADR-009 §Decision 3]. slopCents is the param value
// (cents); the shape is selected by kSlopShape (Gaussian default, cubic alternative).
// Both shapers are zero-mean; slopCents=0 => 0.
inline float drawSlopCents(Xorshift128p& rng, float slopCents) noexcept {
    if constexpr (mw::cal::drift::kSlopShape == mw::cal::drift::SlopShape::Cubic) {
        return slopCents * rng.cubic();
    } else {
        return slopCents * rng.gaussian();
    }
}

// Tier 3 + the four variance spreads, all frozen at note-on [docs/design/08 §7.1,
// §7.2; ADR-009 §Decision 4]. cutoff/PW are ADDITIVE in the native domain;
// env-time/glide are MULTIPLICATIVE (1 + draw*band) on the time constant. Each var
// param (0..1) scales its band, so a 0 param yields a zero offset / unit scale.
inline NoteOnOffsets drawNoteOn(Xorshift128p& rng,
                                float slopCents, float varCutoff01,
                                float varEnv01, float varPw01, float varGlide01) noexcept {
    namespace c = mw::cal::drift;
    NoteOnOffsets n;
    n.slopCents     = drawSlopCents(rng, slopCents);                         // Tier 3
    n.varCutoff     = drawBipolar(rng) * c::kVarCutoffCents * varCutoff01;   // additive
    n.varEnvScale   = 1.0f + drawBipolar(rng) * c::kVarEnvBand  * varEnv01;  // multiplicative
    n.varPw         = drawBipolar(rng) * c::kVarPwFrac     * varPw01;        // additive
    n.varGlideScale = 1.0f + drawBipolar(rng) * c::kVarGlideBand * varGlide01; // multiplicative
    return n;
}

// --- RT-safety: the data structs are trivially-copyable / standard-layout PODs so
// they fit by value into the pre-allocated DriftState[kMaxVoices] array [§8.1, §12.1].
static_assert(std::is_trivially_copyable_v<CalibrationDraw>);
static_assert(std::is_standard_layout_v<CalibrationDraw>);
static_assert(std::is_trivially_copyable_v<NoteOnOffsets>);
static_assert(std::is_standard_layout_v<NoteOnOffsets>);
static_assert(std::is_trivially_copyable_v<DriftState>);
static_assert(std::is_standard_layout_v<DriftState>);

} // namespace mw::dsp::drift
