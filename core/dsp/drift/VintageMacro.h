// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/drift/VintageMacro.h — the HOST-THREAD Vintage (Age) macro -> target
// mapping (task 066).
//
// Realizes docs/design/08 §3.2 (data-flow: "host thread: Age macro -> VintageMacro::
// apply() -> smoothed param targets (APVTS)"), §10.1 (Age-macro semantics + the (PI)
// kAgeCurve table), §10.2 (param default 0 == in tune on load) and ADR-009 §Decision 7
// / VV-1. This module is NON-AUDIO-THREAD: it maps the normalized Age parameter
// (age01 in [0,1]) through kAgeCurve to ALREADY-SMOOTHED targets for the drift /
// variance group, so the audio thread pays nothing for the macro [docs/design/08 §3.2,
// §10.1; ADR-009 §Decision 7].
//
// WHAT THIS OWNS (its whole scope):
//   - VintageTargets: a POD of the scaled drift/variance group target values the
//     macro writes (drift.depth, drift.rate, tune.slop, var.cutoff/env_time/pw/glide).
//   - VintageMacro::curve(age01): the kAgeCurve lookup + linear interpolation, a pure
//     monotonic age01 -> unit-scale (s in [0,1]) map; curve(0) == 0 (in tune on load).
//   - VintageMacro::computeTargets(age01): the pure mapping to a VintageTargets POD,
//     lerping each group target from its zero-offset baseline toward its schema
//     ceiling by s. age01 == 0 => every target at its zero-perturbation baseline
//     (zero added group offset == in tune on load, VV-1 / §10.2).
//   - VintageMacro::apply(age01, smoothers...): writes the computed targets onto the
//     canonical per-target OnePoleSmoother de-zipper TARGETS via setTarget() only. It
//     performs NO audio-thread DSP (no smoother process(), no PRNG/thermal/filter
//     work) — it touches only the param TARGETS [docs/design/08 §3.2/§10.1; ADR-009
//     §Decision 7]. The audio thread later reads those already-smoothed targets.
//
// OUT OF SCOPE (other tasks own these): the audio-thread drift DSP / processBlock
// (vintage-4), the Tier-1/3 + variance draws (DriftState.h, task 065), the parameter
// IDs/ranges (param schema, docs/design/06 §3 — consumed, never re-minted), and any
// juce::ValueTree/APVTS bridge (plugin stream; mwcore is JUCE-free, ADR-001 C1).
//
// All numeric figures (the curve shape, the per-target ceilings) are (PI) TUNABLE
// DEFAULTS sourced from core/calibration/VintageMacroConstants.h, NOT measured SH-101
// specs [docs/design/08 §10.1; ADR-009 §8].

#pragma once

#include <type_traits>

#include "calibration/VintageMacroConstants.h"
#include "params/Smoother.h"

namespace mw::dsp::drift {

// The canonical de-zipper smoother (task 008). The macro writes the ALREADY-SMOOTHED
// target store via setTarget(); it never advances the smoother (that is the audio
// thread's per-tick job) [docs/design/08 §9; ADR-020 S14].
using OnePoleSmoother = mw::params::OnePoleSmoother;

// ---------------------------------------------------------------------------
// VintageTargets — the scaled drift/variance group target values the macro writes,
// in each parameter's native schema units [docs/design/08 §10, §10.1]. POD; the
// defaults are the zero-offset baseline (== the age01 == 0 / in-tune-on-load mapping).
// ---------------------------------------------------------------------------
struct VintageTargets {
    float driftDepthCents = 0.0f;                              // mw101.drift.depth (cents)
    float driftRateHz     = mw::cal::drift::kAgeDriftRateMinHz; // mw101.drift.rate (Hz) — baseline = min
    float tuneSlopCents   = 0.0f;                              // mw101.tune.slop (cents)
    float varCutoff       = 0.0f;                              // mw101.var.cutoff (0..1)
    float varEnvTime      = 0.0f;                              // mw101.var.env_time (0..1)
    float varPw           = 0.0f;                              // mw101.var.pw (0..1)
    float varGlide        = 0.0f;                              // mw101.var.glide (0..1)
};

class VintageMacro {
public:
    // --- Pure age01 -> unit-scale map through the (PI) kAgeCurve table --------------
    // Clamps age01 to [0,1], then linearly interpolates kAgeCurve. Monotonic
    // non-decreasing; curve(0) == 0 (in tune on load) and curve(1) == 1 (full range)
    // [docs/design/08 §10.1, §10.2; ADR-009 VV-1]. Pure + noexcept; host-thread.
    [[nodiscard]] static float curve(float age01) noexcept {
        const auto& tbl = mw::cal::drift::kAgeCurve;
        constexpr int n = static_cast<int>(mw::cal::drift::kAgeCurveSize);

        if (age01 <= 0.0f) return tbl.front();   // exactly 0 at the bottom (VV-1)
        if (age01 >= 1.0f) return tbl.back();     // exactly 1 at the top

        const float pos   = age01 * static_cast<float>(n - 1); // grid index, [0, n-1]
        const int   lo    = static_cast<int>(pos);
        const int   hi    = (lo + 1 < n) ? lo + 1 : lo;
        const float frac  = pos - static_cast<float>(lo);
        return tbl[static_cast<std::size_t>(lo)]
             + frac * (tbl[static_cast<std::size_t>(hi)] - tbl[static_cast<std::size_t>(lo)]);
    }

    // --- Pure mapping age01 -> the scaled group targets (no smoother needed) ---------
    // Lerps each group target from its zero-offset baseline (depth/slop/var = 0,
    // rate = schema min) toward its schema ceiling by s = curve(age01). age01 == 0
    // => zero added group offset (every target at baseline) == in tune on load
    // [docs/design/08 §10.1, §10.2; ADR-009 VV-1]. Pure + noexcept; host-thread.
    [[nodiscard]] static VintageTargets computeTargets(float age01) noexcept {
        namespace c = mw::cal::drift;
        const float s = curve(age01);   // unit scale in [0,1], s(0) == 0

        VintageTargets t;
        t.driftDepthCents = s * c::kAgeDriftDepthMaxCents;
        // Drift rate lerps from its schema MIN baseline toward its schema max, so a
        // full Age opens the drift bandwidth while age==0 leaves it at the slow min.
        t.driftRateHz     = c::kAgeDriftRateMinHz
                          + s * (c::kAgeDriftRateMaxHz - c::kAgeDriftRateMinHz);
        t.tuneSlopCents   = s * c::kAgeTuneSlopMaxCents;
        t.varCutoff       = s * c::kAgeVarMax;
        t.varEnvTime      = s * c::kAgeVarMax;
        t.varPw           = s * c::kAgeVarMax;
        t.varGlide        = s * c::kAgeVarMax;
        return t;
    }

    // --- Host-thread apply: write the targets onto the canonical de-zipper smoothers --
    // Touches ONLY the param TARGETS (setTarget) — NO audio-thread DSP: it never calls
    // process() / advances a smoother, runs no PRNG/thermal/filter work, and allocates
    // nothing [docs/design/08 §3.2, §10.1; ADR-009 §Decision 7]. The smoothers are the
    // already-smoothed target store the audio thread reads. Host-thread only.
    //
    // The smoother arguments are the per-target de-zippers for, in order:
    // drift.depth, drift.rate, tune.slop, var.cutoff, var.env_time, var.pw, var.glide.
    // Defined out-of-line in VintageMacro.cpp.
    static void apply(float age01,
                      OnePoleSmoother& driftDepth,
                      OnePoleSmoother& driftRate,
                      OnePoleSmoother& tuneSlop,
                      OnePoleSmoother& varCutoff,
                      OnePoleSmoother& varEnvTime,
                      OnePoleSmoother& varPw,
                      OnePoleSmoother& varGlide) noexcept;
};

// --- RT/POD discipline: VintageTargets is a trivially-copyable standard-layout POD.
static_assert(std::is_trivially_copyable_v<VintageTargets>);
static_assert(std::is_standard_layout_v<VintageTargets>);

} // namespace mw::dsp::drift
