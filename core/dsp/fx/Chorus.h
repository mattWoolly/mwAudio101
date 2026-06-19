// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/Chorus.h — the Juno-style BBD Chorus stage (task 092).
//
// Realizes docs/design/07-fx-section.md §5.1 (topology), §5.1.2 (this exact class
// signature), §5.1.3 (modulation/width/mix), §5.1.4 ((PI) calibration constants),
// and ADR-010 FX-6 (the Chorus contract row). It is the primary identity-legitimate
// mono-to-stereo widener: two ANTI-PHASE LFO-modulated fractional-delay lines panned
// hard L/R, fed from the (mono) Drive output [§5.1.1; ADR-010 FX-6; research/11
// §4.5, §7.1]. Stereo is born only in the wet content [§3.3 rule 2; ADR-010 FX-4].
//
// Real-time invariants [§3.2; ADR-010 FX-10]:
//   - prepare() is the ONLY allocation site (the two FractionalDelayLine ring
//     buffers are sized to base+depth there); reset/process/setParams take no locks
//     and perform no heap allocation.
//   - process()/setParams()/reset()/latencySamples() are noexcept hot paths.
//   - rate/depth/width/mix are smoothed (control-rate one-pole de-zipper) so
//     automation never zippers [§5.1.2 "smoothed rate/depth/width/mix"; ADR-020].
//
// Latency: the Chorus modulation delay is INTENDED MUSICAL DELAY and does NOT
// contribute to reported PDC; latencySamples() returns 0 [§5.1.3; ADR-017 L3].
//
// This file carries no JUCE types [ADR-001]. Out of scope here (owned elsewhere):
// the chain Mode==Off early-out dispatch / chain wiring (fx-7); parameter ID/range
// registration (param-schema, docs/design/06 §2); Drive and Delay processing.

#pragma once

#include <vector>

#include "FractionalDelayLine.h"
#include "FxParams.h"
#include "../../calibration/ChorusConstants.h"

namespace mw::fx {

// Two anti-phase LFO-modulated fractional-delay lines panned hard L/R. Class
// signature is fixed verbatim by docs/design/07-fx-section.md §5.1.2.
class Chorus {
public:
    enum class Mode { Off = 0, I = 1, II = 2, IandII = 3 };

    Chorus() noexcept = default;

    // Allocate the two ring buffers to hold the maximum (base + full depth)
    // excursion. Called off the audio thread; the ONLY allocation site [§3.2,
    // §5.1.2; ADR-010 FX-10]. Precomputes the smoothing coefficient and seeds the
    // anti-phase LFO state. Re-callable / idempotent on sample-rate change.
    void prepare(double sampleRate, int maxBlockSize) noexcept;

    // Clear all delay-line and LFO/smoother state to a known start; no alloc [§3.2].
    void reset() noexcept;

    // Adopt a new decoded parameter snapshot. Sets smoother TARGETS (rate/depth/
    // width/mix) and the mode; the smoothers glide toward them in process(). The
    // mode also publishes to the public `mode` field so the chain's Mode==Off
    // early-out (fx-7) can read it. noexcept, no alloc, no lock.
    void setParams(const FxParams::ChorusP& p) noexcept;

    // Mono drive output in `mono`; mixes wet stereo into L/R (which already hold the
    // dry mono per §3.4 step 6): L[n] = dryL[n] + mix*wetL[n] (likewise R) [§5.1.3].
    // width = 0 collapses the two taps to a centered mono wet added equally to L/R;
    // width = 1 is the full hard-panned anti-phase image [§5.1.3; ADR-010 FX-6].
    // noexcept hot path: no alloc, no lock.
    void process(const float* mono, float* L, float* R, int numSamples) noexcept;

    Mode mode = Mode::Off;

    // The Chorus modulation delay is intended musical delay, never reported as PDC
    // [§5.1.3; ADR-017 L3].
    [[nodiscard]] int latencySamples() const noexcept { return 0; }

    // The modulation LFO waveform [§5.1.3]: a unit bipolar triangle in [-1,1] for a
    // phase in [0,1) (wrapped). Exposed for the anti-phase oracle: because the two
    // lines run 0.5 cycle apart and the triangle is odd-symmetric about the
    // half-period, lfoWave(p + 0.5) == -lfoWave(p), which is exactly the anti-phase
    // contract of §5.1.2/§5.1.3. Used internally by process().
    [[nodiscard]] static float lfoWave(float phase) noexcept;

private:

    // Resolve the mode's preset LFO rate (Hz) and depth scalar [§5.1.4]. I+II
    // combines both pairs (averaged rate, summed-and-clamped depth).
    static void modePreset(Mode m, float& rateHz, float& depthScalar) noexcept;

    FractionalDelayLine lineL_, lineR_;     // sized to max base+depth in prepare
    float lfoPhaseL_ = 0.0f, lfoPhaseR_ = 0.5f; // anti-phase (0.5 cycle apart) [§5.1.2]

    double sampleRate_   = 48000.0;
    float  baseDelaySmp_ = 0.0f;            // kChorusBaseDelayMs in samples
    float  depthSmp_     = 0.0f;            // kChorusDepthMs in samples (full excursion)

    // Smoothed (control-rate one-pole) rate/depth/width/mix targets [§5.1.2]. We tick
    // these per sample with a per-sample coefficient so the read offset never zippers.
    float rateHz_       = mw::cal::chorus::kChorusModeIRateHz;
    float depth_        = 0.0f;             // smoothed depth scalar (mode * param)
    float width_        = 1.0f;             // smoothed stereo separation
    float mix_          = 0.0f;             // smoothed dry/wet of the chorus stage
    float rateTarget_   = mw::cal::chorus::kChorusModeIRateHz;
    float depthTarget_  = 0.0f;
    float widthTarget_  = 1.0f;
    float mixTarget_    = 0.0f;
    float smoothCoeff_  = 0.0f;             // per-sample one-pole de-zipper coeff
};

} // namespace mw::fx
