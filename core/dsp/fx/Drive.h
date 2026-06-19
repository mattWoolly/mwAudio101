// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/Drive.h — the post-voice FX Drive stage (task 091).
//
// Realizes docs/design/07-fx-section.md §4.1 (topology/placement), §4.2 (this exact
// class signature), §4.3 (asymmetric waveshaper), §4.4 (pre/de-emphasis tilt), §4.5
// (DC blocker), §4.6 (parameter DSP interpretation), §4.7 (Drive RT invariants), and
// ADR-010 FX-5, ADR-017 L2/L8/L9/§A2.
//
// Drive is a SINGLE post-voice stage that runs once on the mono voice sum with its
// OWN dedicated 2x oversampling [ADR-017 §A2, L2, L9]. It is mono-in / mono-out and
// NEVER widens [§4.1; §3.3 rule 2; ADR-010 FX-4]. Inside the 2x zone the flow is:
//
//   upsample 2x -> pre-emphasis tilt -> input gain (Drive) -> asymmetric waveshaper
//     -> de-emphasis tilt -> downsample 2x -> DC blocker -> output makeup
//
// The DC blocker runs AFTER the downsampler (it is a level/offset correction, not a
// band-limiting concern) [§4.1; §4.5; ADR-010 FX-5].
//
// Real-time invariants [§4.7; §3.2; ADR-010 FX-10; ADR-017 L10]:
//   - The 2x oversampler (FxOversampler2x) is the ONLY allocation source; sized in
//     prepare(). reset/process/setParams/latencySamples take no locks and perform no
//     heap allocation.
//   - process() is noexcept and operates in-place on the caller's mono buffer.
//   - Drive/Output gains are smoothed (control-rate one-pole de-zipper) so automation
//     never zippers [§4.2 "smoothed gains for Drive / Output"; ADR-020].
//
// Latency: latencySamples() returns the FxOversampler2x round-trip group delay — a
// FIXED integer independent of drive amount and of `on` — the FX Drive's contribution
// to the host's constant PDC [§6.1; ADR-017 L2, L5, L8].
//
// This file carries no JUCE types [ADR-001]. Out of scope here (owned elsewhere):
// the chain per-block bypass dispatch / chain wiring (fx-7 FxChain); mono-to-stereo
// widening (Drive never widens, §4.1); parameter ID/range registration (param-schema,
// docs/design/06 §2).

#pragma once

#include "FxOversampler2x.h"
#include "FxParams.h"
#include "../../calibration/DriveConstants.h"

namespace mw::fx {

// Asymmetric oversampled waveshaper with pre/de-emphasis tilt and a post-downsample
// DC blocker. Class signature is fixed verbatim by docs/design/07-fx-section.md §4.2.
class Drive {
public:
    Drive() noexcept = default;

    // Allocate the dedicated 2x oversampler scratch (the ONLY allocation site) and
    // measure its fixed round-trip group delay; precompute the tilt-shelf and DC
    // blocker coefficients for this sample rate and the per-sample de-zipper
    // coefficient. Called off the audio thread. Re-callable / idempotent on
    // sample-rate / block-size change [§4.2, §4.7; ADR-017 L10].
    void prepare(double sampleRate, int maxBlockSize) noexcept;

    // Clear all internal state (oversampler delay lines, tilt + DC-blocker state) to a
    // known silent start; snap smoothed gains to their targets. No alloc [§4.7].
    void reset() noexcept;

    // Adopt a new decoded Drive parameter snapshot [§4.6]. Sets the per-block-bypass
    // `on` flag and the smoother TARGETS for the pre-gain (from amount), tilt (from
    // tone) and makeup (from output); the smoothers glide toward them in process().
    // noexcept, no alloc, no lock.
    void setParams(const FxParams::DriveP& p) noexcept;

    // Process one mono block IN-PLACE; returns the processed mono pointer (== mono).
    // noexcept hot path: no alloc, no lock [§4.2, §4.7]. The chain only calls this
    // when `on == true` (the per-block bypass early-out is owned by fx-7); a bypassed
    // Drive costs ~0 because process() is not entered [§4.7; ADR-010 FX-3].
    float* process(float* mono, int numSamples) noexcept;

    // Per-block bypass flag, mapped from doc 06 mw101.fx.drive_enable [§4.2, §4.6].
    bool on = false;

    // The fixed 2x halfband round-trip group delay (in base-rate samples) — the FX
    // Drive's contribution to constant PDC. It equals the FxOversampler2x group delay
    // and is INVARIANT to drive amount and to `on` [§6.1; ADR-017 L2, L5, L8].
    [[nodiscard]] int latencySamples() const noexcept { return os_.latencySamples(); }

    // --- Exposed for the acceptance oracles (no JUCE, pure-function) --------------
    // The asymmetric memoryless shaper of §4.3, with the bias re-centering term so a
    // zero input maps to zero output. Static so a test can compare 2x vs 1x using the
    // identical curve. `preGain` is the (smoothed) input gain into the shaper.
    [[nodiscard]] static float shape(float x, float preGain) noexcept;

private:
    // One-pole tilt-shelf coefficient bundle (pre-emphasis; de-emphasis is its
    // inverse). At tone == 0.5 both shelves are unity (flat) [§4.4].
    struct Tilt {
        // y[n] = x[n] + g*(x[n] - lp[n]); lp[n] = lp[n-1] + a*(x[n] - lp[n-1]).
        // A one-pole high-shelf: g > 0 boosts highs (pre), g < 0 cuts highs (de).
        float a   = 0.0f;   // one-pole smoothing coeff for the pivot frequency
        float lp  = 0.0f;   // low-pass state
        void  reset() noexcept { lp = 0.0f; }
        [[nodiscard]] float process(float x, float g) noexcept {
            lp += a * (x - lp);
            return x + g * (x - lp);
        }
    };

    FxOversampler2x os_;        // dedicated post-voice 2x pair (ADR-017 L2)

    // Pre/de-emphasis tilt state — one shelf each side of the shaper [§4.4].
    Tilt preTilt_{};
    Tilt deTilt_{};

    // DC blocker state (after downsample) [§4.5]: y[n] = x[n] - x[n-1] + R*y[n-1].
    float dcX1_ = 0.0f;
    float dcY1_ = 0.0f;
    float dcR_  = mw::cal::drive::kDcBlockR; // sample-rate-scaled pole radius

    // Smoothed gains for Drive (pre-gain into shaper) and Output (post makeup) [§4.2].
    // Tilt gain (from tone) is smoothed too so a tone sweep never zippers.
    float preGain_       = 1.0f;   // linear pre-gain into the shaper
    float tiltGain_      = 0.0f;   // signed shelf gain (>0 bright into shaper)
    float outGain_       = 1.0f;   // linear makeup gain
    float preGainTarget_ = 1.0f;
    float tiltTarget_    = 0.0f;
    float outTarget_     = 1.0f;
    float smoothCoeff_   = 0.0f;   // per-sample one-pole de-zipper coeff

    double sampleRate_ = 48000.0;
};

} // namespace mw::fx
