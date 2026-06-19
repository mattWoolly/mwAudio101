// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/Delay.h — the FX Delay stage: single mono delay core read to a stereo
// output, fractional read, feedback-path damping LPF + gentle saturation, clamped
// feedback, tempo sync, optional ping-pong, click-free time glide (task 093).
//
// Realizes docs/design/07-fx-section.md §5.2 verbatim:
//   §5.2.1 topology  — one FractionalDelayLine core; mono-sum write; read to stereo.
//   §5.2.2 signature — prepare/reset/setParams/process(float*L,float*R,int)/on/
//                      latencySamples().
//   §5.2.3 tempo sync — delayMs = (60000/bpm)*beatsPerDivision over
//                      {1/4,1/8,1/8.,1/8T,1/16,1/16T}; conversion cached, recomputed
//                      only on tempo/division change [ADR-010 FX-7].
//   §5.2.4 feedback/damp/sat/ping-pong — feedback clamped to [0,kDelayMaxFeedback]
//                      (<1.0); one-pole damping LPF + gentle tanh saturation +
//                      denormal flush in the feedback path; ping-pong alternating
//                      taps; Width=0 => centered mono [ADR-010 FX-8, FX-10].
//   §5.2.5 click-free  — pointer-glide read position via a SmoothedValue ramp on any
//                      time/division/tempo change [ADR-010 FX-11].
//   §5.2.6 constants  — kDelayMaxMs/kDelayMaxFeedback/kDelayDampHz{Min,Max}/
//                      kDelaySatDrive/kDelayTimeGlideMs in core/calibration.
//
// RT invariants [docs/design/07 §3.2; ADR-010 FX-10; ADR-017 L3, L10]: the ring
// buffer is sized to kDelayMaxMs once in prepare() (the only allocation site);
// process/setParams/reset/latencySamples are noexcept and perform no heap allocation
// and take no locks; the feedback path flushes denormals defensively. The Delay
// musical time is intended musical delay and does NOT contribute to reported PDC —
// latencySamples() returns 0 [ADR-017 L3]. This file carries no JUCE types [ADR-001].

#pragma once

#include <algorithm>
#include <cmath>

#include "FractionalDelayLine.h"
#include "FxParams.h"
#include "../FastTanh.h"
#include "../../params/Smoother.h"
#include "../../calibration/DelayConstants.h"

namespace mw::fx {

class Delay {
public:
    Delay() noexcept = default;

    // Allocate the mono ring buffer at kDelayMaxMs and configure the per-tick
    // pointer-glide coefficient. Called off the audio thread; the ONLY allocation
    // site [docs/design/07 §5.2.2, §5.3; ADR-010 FX-10].
    void prepare(double sampleRate, int maxBlockSize) noexcept {
        (void) maxBlockSize; // the core is sized by time, not block size
        sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;

        const int maxSamples = msToSamples(mw::cal::delay::kDelayMaxMs);
        core_.prepare(maxSamples);

        // Pointer-glide ramp over kDelayTimeGlideMs. One process() call advances the
        // smoother one control-rate tick; with audio-rate stepping (one tick per
        // sample on the hot path here) we configure the tick rate to the sample rate
        // so the glide spans ~kDelayTimeGlideMs of audio [docs/design/07 §5.2.5].
        const double glideSeconds =
            static_cast<double>(mw::cal::delay::kDelayTimeGlideMs) * 0.001;
        glide_.prepare(glideSeconds, sampleRate_);

        // Default to the §5.2.7 free-time default so an un-setParams'd instance is
        // well-defined; reset() snaps the glide to it.
        targetDelaySamples_ = clampDelaySamples(msToSamplesF(350.0f));
        reset();
        recomputeConversion_ = true;
    }

    // Clear all state to a known start; no allocation [docs/design/07 §5.2.2].
    void reset() noexcept {
        core_.reset();
        dampStateL_ = 0.0f;
        dampStateR_ = 0.0f;
        fbL_ = 0.0f;
        fbR_ = 0.0f;
        pingState_ = 0; // start the ping-pong bounce on L
        glide_.reset(static_cast<double>(targetDelaySamples_));
    }

    // Publish a new parameter snapshot. Recomputes the (cached) tempo-sync
    // ms-conversion ONLY when the host tempo or the selected division changes — never
    // per sample [docs/design/07 §5.2.3; ADR-010 FX-7]. noexcept, no alloc, no lock.
    void setParams(const FxParams::DelayP& p, double hostBpm) noexcept {
        on = p.on;

        // --- Tempo sync: cache the conversion, recompute only on tempo/division
        //     change (§5.2.3 / FX-7). ----------------------------------------------
        const bool syncChanged     = (p.sync != sync_);
        const bool bpmChanged      = (hostBpm != lastBpm_);
        const bool divisionChanged = (p.division != lastDivision_);
        const bool timeChanged     = (p.timeMs != lastTimeMs_);

        if (syncChanged || bpmChanged || divisionChanged || timeChanged
            || recomputeConversion_) {
            float delayMs;
            if (p.sync) {
                const double bpm = (hostBpm > 0.0) ? hostBpm : 120.0;
                // delayMs = (60000 / bpm) * beatsPerDivision (§5.2.3 / FX-7).
                delayMs = static_cast<float>((60000.0 / bpm)
                                             * beatsPerDivision(p.division));
            } else {
                delayMs = p.timeMs;
            }
            cachedDelayMs_      = delayMs;
            targetDelaySamples_ = clampDelaySamples(msToSamplesF(delayMs));
            glide_.setTarget(static_cast<double>(targetDelaySamples_));
            ++conversionRecomputeCount_;
            recomputeConversion_ = false;
        }

        sync_         = p.sync;
        lastBpm_      = hostBpm;
        lastDivision_ = p.division;
        lastTimeMs_   = p.timeMs;

        // --- Feedback clamped hard to [0, kDelayMaxFeedback] (<1.0) (§5.2.4 / FX-8).
        float fb = p.feedback;
        if (fb < 0.0f) fb = 0.0f;
        if (fb > mw::cal::delay::kDelayMaxFeedback) fb = mw::cal::delay::kDelayMaxFeedback;
        feedback_ = fb;

        // --- Damping LPF cutoff: damp in [0,1] maps to [min,max] Hz (0 = dark)
        //     (§5.2.4). One-pole coefficient a = exp(-2*pi*fc/fs). -----------------
        float damp = p.damp;
        if (damp < 0.0f) damp = 0.0f;
        if (damp > 1.0f) damp = 1.0f;
        const float fc = mw::cal::delay::kDelayDampHzMin
            + damp * (mw::cal::delay::kDelayDampHzMax - mw::cal::delay::kDelayDampHzMin);
        const double a = std::exp(-2.0 * kPi * static_cast<double>(fc) / sampleRate_);
        dampCoeff_ = static_cast<float>(a); // y = (1-a)*x + a*y_prev

        // --- Width, Mix, ping-pong (§5.2.4). -------------------------------------
        width_    = clamp01(p.width);
        mix_      = clamp01(p.mix);
        pingpong_ = p.pingpong;
    }

    // Process one stereo block IN PLACE. On entry L/R carry the dry (+chorus wet)
    // signal; the Delay sums its wet content into L/R per Mix/Width/ping-pong. The
    // single mono core is fed the mono-sum of the input plus the recirculated,
    // damped, saturated, clamped feedback [docs/design/07 §5.2.1, §5.2.4]. noexcept
    // hot path: no alloc, no lock; the feedback path flushes denormals [FX-10].
    void process(float* L, float* R, int numSamples) noexcept {
        for (int n = 0; n < numSamples; ++n) {
            const float inL = L[n];
            const float inR = R[n];

            // Pointer-glide the read position one tick (§5.2.5): no zipper on time
            // changes — the read tap ramps to the new delay in samples.
            const float readSamples = static_cast<float>(glide_.process());

            // Single mono delay core (§5.2.1): write the mono-sum of the block input
            // plus the recirculated feedback (already damped+saturated+clamped from
            // the previous tap), then read the fractional tap back.
            const float monoIn = 0.5f * (inL + inR);
            core_.write(monoIn + fbMono_);
            const float wet = core_.read(readSamples);

            // Feedback path (§5.2.4): one-pole damping LPF -> gentle tanh saturation
            // -> clamped feedback gain -> denormal flush -> back to the write.
            dampStateL_ = (1.0f - dampCoeff_) * wet + dampCoeff_ * dampStateL_;
            const float damped = dampStateL_;
            const float sat = mw::dsp::fastTanh(mw::cal::delay::kDelaySatDrive * damped);
            fbMono_ = flushDenormal(feedback_ * sat);

            // Ping-pong: alternate which channel the wet tap lands on per repeat so
            // echoes bounce L->R->L (§5.2.4). When OFF, wet is equal on both channels.
            float wetL, wetR;
            if (pingpong_) {
                // Bounce on the recirculation phase: even repeats favour L, odd R.
                if (pingState_ == 0) { wetL = wet; wetR = 0.0f; }
                else                 { wetL = 0.0f; wetR = wet; }
            } else {
                wetL = wet;
                wetR = wet;
            }

            // Width scales the L/R spread: width=1 keeps the (possibly hard-panned)
            // taps, width=0 collapses to the centered mono mean so out[L]==out[R]
            // (§5.2.4 / FX-8). center = mean of the two wet taps.
            const float center = 0.5f * (wetL + wetR);
            const float sprL = center + width_ * (wetL - center);
            const float sprR = center + width_ * (wetR - center);

            // Mix: dry already in L/R; add mix*wet (§5.2.4).
            L[n] = inL + mix_ * sprL;
            R[n] = inR + mix_ * sprR;
        }

        // Advance the ping-pong bounce once per block boundary that crosses one delay
        // period would be ideal, but a per-block toggle keeps the hot path branch-free
        // and the bounce audible. We toggle on the delay period below.
        advancePingPong(numSamples);
    }

    bool on = false; // per-block bypass (early-out owned by the chain) [ADR-010 FX-3]

    // The Delay musical time is intended musical delay and does NOT contribute to
    // reported PDC — returns 0 [docs/design/07 §5.2.4, §6.1; ADR-017 L3].
    [[nodiscard]] int latencySamples() const noexcept { return 0; }

    // --- Inspectors for tests (cheap, const, no state change) --------------------
    // The realized (cached) delay in samples the read tap is gliding TOWARD.
    [[nodiscard]] float targetDelaySamples() const noexcept {
        return static_cast<float>(targetDelaySamples_);
    }
    // The cached ms-equivalent delay time (recomputed only on tempo/division change).
    [[nodiscard]] float cachedDelayMs() const noexcept { return cachedDelayMs_; }
    // The applied (clamped) feedback gain actually used in the loop.
    [[nodiscard]] float appliedFeedback() const noexcept { return feedback_; }
    // How many times the tempo-sync conversion has been recomputed (FX-7 caching).
    [[nodiscard]] long conversionRecomputeCount() const noexcept {
        return conversionRecomputeCount_;
    }

    // beats-per-quarter weighting for a note division. PUBLIC + static so tests can
    // assert the (60000/bpm)*beatsPerDivision oracle directly [docs/design/07 §5.2.3].
    // Division order matches the FxParams::DelayP::division enum int
    // {1/4=0, 1/8=1, 1/8.=2, 1/8T=3, 1/16=4, 1/16T=5} (order owned by doc 06).
    [[nodiscard]] static constexpr double beatsPerDivision(int division) noexcept {
        switch (division) {
            case 0: return 1.0;        // 1/4  — one beat
            case 1: return 0.5;        // 1/8  — half a beat
            case 2: return 0.75;       // 1/8. — dotted eighth = 1.5 * 1/8
            case 3: return 1.0 / 3.0;  // 1/8T — eighth triplet = (1/8)*(2/3)
            case 4: return 0.25;       // 1/16 — quarter of a beat
            case 5: return 1.0 / 6.0;  // 1/16T— sixteenth triplet = (1/16)*(2/3)
            default: return 0.5;       // fall back to 1/8
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    [[nodiscard]] static float clamp01(float v) noexcept {
        return (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    // Flush a denormal/subnormal magnitude to exactly zero so the recirculating loop
    // never enters a denormal CPU stall [docs/design/07 §3.2; ADR-010 FX-10; RT-5].
    [[nodiscard]] static float flushDenormal(float v) noexcept {
        return (std::fabs(v) < 1.0e-20f) ? 0.0f : v;
    }

    [[nodiscard]] int msToSamples(float ms) const noexcept {
        return static_cast<int>(std::ceil(static_cast<double>(ms) * 0.001 * sampleRate_));
    }
    [[nodiscard]] float msToSamplesF(float ms) const noexcept {
        return static_cast<float>(static_cast<double>(ms) * 0.001 * sampleRate_);
    }

    // Clamp the requested delay (in samples) to a readable range: at least 1 sample
    // and at most the buffer length so the read tap never wraps past the write head.
    [[nodiscard]] float clampDelaySamples(float s) const noexcept {
        const float maxS =
            static_cast<float>(msToSamples(mw::cal::delay::kDelayMaxMs)) - 1.0f;
        if (s < 1.0f) s = 1.0f;
        if (s > maxS) s = maxS;
        return s;
    }

    // Toggle the ping-pong channel once per delay-period worth of samples processed.
    void advancePingPong(int numSamples) noexcept {
        if (!pingpong_) return;
        pingAccum_ += numSamples;
        const float period = static_cast<float>(targetDelaySamples_);
        while (period > 0.0f && static_cast<float>(pingAccum_) >= period) {
            pingAccum_ -= static_cast<int>(period);
            pingState_ ^= 1;
        }
    }

    FractionalDelayLine core_;        // single mono delay core, sized to kDelayMaxMs
    mw::params::OnePoleSmoother glide_; // pointer-glide read position (§5.2.5)

    double sampleRate_ = 48000.0;

    // Cached tempo-sync conversion (§5.2.3) — recomputed only on tempo/division/time
    // change, never per sample.
    bool   sync_         = false;
    double lastBpm_      = -1.0;   // sentinel so the first setParams always computes
    int    lastDivision_ = -1;
    float  lastTimeMs_   = -1.0f;
    bool   recomputeConversion_ = true;
    float  cachedDelayMs_ = 0.0f;
    int    targetDelaySamples_ = 1;
    long   conversionRecomputeCount_ = 0;

    // Loop coefficients.
    float feedback_  = 0.0f;
    float dampCoeff_ = 0.0f; // one-pole LPF pole (a in y = (1-a)x + a*y_prev)
    float width_     = 1.0f;
    float mix_       = 0.0f;
    bool  pingpong_  = false;

    // Per-sample loop state.
    float dampStateL_ = 0.0f; // single mono damping LPF state (one core)
    float dampStateR_ = 0.0f; // reserved (kept for §5.2.2 layout parity)
    float fbMono_     = 0.0f; // recirculated feedback sample (damped+sat+clamped)
    float fbL_ = 0.0f, fbR_ = 0.0f; // reserved (§5.2.2 layout parity)

    // Ping-pong bounce state.
    int pingState_ = 0; // 0 => wet to L, 1 => wet to R
    int pingAccum_ = 0; // samples accumulated toward the next bounce
};

} // namespace mw::fx
