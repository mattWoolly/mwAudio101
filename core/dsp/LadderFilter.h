// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/LadderFilter.h — the shipping per-voice Huovilainen four-stage one-pole
// OTA cascade for the IR3109 VCF model (task 038, the LINEAR core).
//
// Single source of truth: docs/design/02-dsp-filter.md §3 (class), §5.2 (cutoff->g
// mapping), §5.5 (per-sample forward-Euler cascade), under the normative ADR-003
// contract rows F-01, F-02, F-08, F-10, F-11, F-12.
//
// SCOPE OF THIS TASK (core-filter linear core): the full public interface (§3.1) and
// state layout (§3.2); prepare/reset; the cutoff setters (table lookup, no
// transcendental at audio rate); and the four cascaded one-poles with fastTanhKnee,
// with the global feedback gain k FORCED TO ZERO. So processSample/processBlock run
// the cascade purely feed-forward here.
//
// OUT OF SCOPE (wired by core-filter-5, the resonance task): the inverting global
// feedback, the +0.5-sample (two-sample-average) phase compensation, the diode-clamp
// limiter, self-oscillation, and the output-side make-up Q gain. The state and
// accessors those need (fbPrev_, k_, makeUpGain_, setResonance) exist here as the
// k=0 / unity-gain stubs the full interface declares (§3.1), so core-filter-5 fills
// in the feedback path without changing this header's surface.
//
// Real-time invariants [docs/design/02 §1.3; ADR-003 F-02, F-10, F-11, F-12;
// docs/design/00 §9.1]:
//   * prepare() builds/owns the FilterTables (the ONLY allocator) and resets state;
//     it runs off the audio thread.
//   * reset(), the setters, processSample() and processBlock() are noexcept,
//     allocate no heap and take no lock.
//   * Per-sample work is fixed and data-independent: forward Euler, no Newton /
//     iterate-to-tolerance solve, no signal-dependent branch count (F-02).
//   * No std::tanh / std::tan / std::exp on the audio thread — the knee is the
//     shared fastTanhKnee approximation and the cutoff map is a table lookup (F-10).
//   * Every integrator state carries an anti-denormal bias; FTZ/DAZ is set by the
//     engine at process entry (F-12).
//
// mwcore is JUCE-free.

#pragma once

#include "dsp/FastTanh.h"
#include "dsp/FilterTables.h"

#include "calibration/FastTanhConstants.h"
#include "calibration/LadderFilterConstants.h"

namespace mw::dsp {

class LadderFilter {
public:
    LadderFilter() noexcept = default;

    // Off-audio-thread setup. Builds the per-SR FilterTables into preallocated
    // storage (the ONLY allocator here) and resets state. fsOsHz is the OVERSAMPLED
    // rate (factor * host rate). maxBlockOs is the max oversampled block length
    // (accepted for interface completeness; this linear core keeps no extra
    // per-block scratch). (docs/design/02 §3.1; ADR-003 F-11, F-14; ADR-004 C15)
    void prepare(double fsOsHz, int maxBlockOs) noexcept;

    // Drop all integrator/feedback memory to the anti-denormal bias; keep
    // coefficients. noexcept, no alloc. (docs/design/02 §3.1, §5.5 step 5; F-12)
    void reset() noexcept;

    // --- Control-rate setters (call once per control block, NOT per sample) ---

    // Summed, smoothed cutoff CV in volts (1 V/oct), referenced so cv == 0 V maps to
    // the reference cutoff fcRefHz. Mapped to the prewarped coefficient g through the
    // FilterTables (table lookup + interpolation, no transcendental). (docs/design/02
    // §3.1, §5.2; ADR-003 F-08, F-10)
    void setCutoffCv(float cutoffVolts) noexcept;

    // Set cutoff directly in Hz (used by the reference/tests and by callers that
    // pre-resolve CV->Hz). Clamped to [fcMinHz, min(fcMaxHz, 0.45*fs_os)] by the
    // table. (docs/design/02 §3.1, §5.2; ADR-003 F-08)
    void setCutoffHz(float fcHz) noexcept;

    // Normalized resonance control in [0, 1]. STUB in this task: the resonance->k
    // taper, the inverting feedback and the output-side make-up gain are wired by
    // core-filter-5. Here the value is stored for introspection but the loop gain k
    // is FORCED TO ZERO (linear core) and the make-up gain stays unity, so the
    // cascade is feed-forward. (docs/design/02 §3.1; task 038 scope/out-of-scope)
    void setResonance(float reso01) noexcept;

    // --- Audio-rate hot path (fs_os) ---

    // Process one oversampled sample through the four cascaded one-pole OTA cells
    // with the OTA-knee tanh at each transconductor. In this task the global feedback
    // gain k is zero, so the cascade is purely feed-forward (no feedback, no diode
    // clamp). Returns the stage-4 output. noexcept, no alloc, fixed cost.
    // (docs/design/02 §5.5 steps 3/5; ADR-003 F-01, F-02)
    [[nodiscard]] float processSample(float x) noexcept;

    // Block form over the oversampled buffer; equivalent to a processSample loop,
    // writing the filtered output back in place. noexcept, no alloc. (docs/design/02
    // §3.1)
    void processBlock(float* samplesOs, int numSamplesOs) noexcept;

    // The output-side make-up gain scalar for the CURRENT resonance setting, applied
    // downstream by the VCA drive node (NOT applied here). UNITY in this task (the
    // make-up curve is core-filter-5 scope). (docs/design/02 §3.1, §5.3; ADR-003 F-06)
    [[nodiscard]] float makeUpGain() const noexcept { return makeUpGain_; }

    // Current normalized loop gain k (for tests / reference cross-check). ZERO in this
    // task (k forced to zero — linear core). (docs/design/02 §3.1; F-05)
    [[nodiscard]] float loopGainK() const noexcept { return k_; }

    // --- Introspection for tests (no audio-rate cost) ---

    // The per-stage one-pole coefficient g = 1 - exp(-2*pi*fc/fs_os) currently in use
    // (set by the cutoff setters via the table). (docs/design/02 §5.2; F-01)
    [[nodiscard]] float coeffG() const noexcept { return g_; }

    // The oversampled rate the tables were built for. (docs/design/02 §3.2)
    [[nodiscard]] double sampleRateOs() const noexcept { return fsOs_; }

    // The stored normalized resonance control (un-mapped). (introspection)
    [[nodiscard]] float resonance01() const noexcept { return reso01_; }

private:
    // Coefficients (control-rate; updated by the setters). (docs/design/02 §3.2)
    double fsOs_       = 88200.0;            // oversampled rate
    float  g_          = 0.0f;               // per-stage one-pole coeff = 1 - exp(-2*pi*fc/fs_os) (F-01)
    float  k_          = 0.0f;               // normalized loop gain; FORCED to 0 in this task (F-05)
    float  makeUpGain_ = 1.0f;               // output-side Q make-up scalar; UNITY in this task (F-06)
    float  reso01_     = 0.0f;               // stored resonance control (stub; mapped by core-filter-5)

    // Integrator states (one per stage) + their saturated outputs w_[i] =
    // fastTanhKnee(y_[i]). Carry the anti-denormal bias (F-12). (docs/design/02 §3.2)
    float  y_[4] = { cal::vcf::kAntiDenorm, cal::vcf::kAntiDenorm,
                     cal::vcf::kAntiDenorm, cal::vcf::kAntiDenorm };
    float  w_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Feedback-phase compensation history: the stage-4 output one sample ago (for the
    // two-sample average the inverting feedback uses). Maintained here so core-filter-5
    // can read a valid history; unused while k == 0. (docs/design/02 §3.2; F-03)
    float  fbPrev_ = cal::vcf::kAntiDenorm;

    // Read-only at audio rate; owned by this filter and built in prepare() (F-11).
    FilterTables tables_{};
};

} // namespace mw::dsp
