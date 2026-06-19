// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/LadderFilter.cpp — the shipping per-voice Huovilainen four-stage one-pole
// OTA cascade (task 038, LINEAR core). See LadderFilter.h and docs/design/02 §3, §5.2,
// §5.5 under ADR-003 F-01, F-02, F-08, F-10, F-11, F-12.
//
// This task wires the cascade with the global feedback gain k FORCED TO ZERO: the
// inverting feedback, the +0.5-sample phase compensation, the diode-clamp limiter,
// self-oscillation and the output-side make-up Q are core-filter-5 scope. The
// per-sample structure below is the §5.5 cascade with the feedback term set to zero.
//
// All (PI) constants are read from core/calibration/* (the mw::cal::vcf namespace):
//   - the OTA knee scaler invTwoVt and the tanh approximation come from FastTanh.h /
//     FastTanhConstants.h (task 033);
//   - the cutoff->g map comes from FilterTables (task 035);
//   - the anti-denormal bias comes from LadderFilterConstants.h (this task).
// No (PI) numeric literal is inlined here [ADR-003 F-15; docs/design/02 §10 F-15].

#include "dsp/LadderFilter.h"

namespace mw::dsp {

void LadderFilter::prepare(double fsOsHz, int maxBlockOs) noexcept {
    (void) maxBlockOs;  // no extra per-block scratch in the linear core
    fsOs_ = fsOsHz;
    // Build the per-SR coefficient/compensation tables off the audio thread. This is
    // the ONLY place table storage is (re)built; the lookups are allocation-free at
    // audio rate [ADR-003 F-11, F-14; docs/design/02 §7].
    tables_.build(fsOsHz);
    // Recompute g for the current CV anchor (cv == 0 V => reference cutoff) so the
    // filter has a valid coefficient before the first setter call.
    g_ = tables_.cvToG(0.0f);
    reset();
}

void LadderFilter::reset() noexcept {
    // Flush every integrator state and the saturated outputs to the anti-denormal
    // bias / silence; keep coefficients. The bias keeps a decay tail out of the
    // subnormal band [docs/design/02 §3.2, §5.5 step 5; ADR-003 F-12].
    for (int i = 0; i < 4; ++i) {
        y_[i] = cal::vcf::kAntiDenorm;
        w_[i] = 0.0f;
    }
    fbPrev_ = cal::vcf::kAntiDenorm;
}

void LadderFilter::setCutoffCv(float cutoffVolts) noexcept {
    // Table lookup + interpolation; folds fc = fcRefHz * 2^v (1 V/oct) and
    // g = 1 - exp(-2*pi*fc/fs_os) into one read — no transcendental at audio rate
    // [docs/design/02 §5.2; ADR-003 F-08, F-10].
    g_ = tables_.cvToG(cutoffVolts);
}

void LadderFilter::setCutoffHz(float fcHz) noexcept {
    // fc clamped to [fcMinHz, min(fcMaxHz, 0.45*fs_os)] by the table [F-08].
    g_ = tables_.hzToG(fcHz);
}

void LadderFilter::setResonance(float reso01) noexcept {
    // STUB (task 038): store the control for introspection. The resonance->k taper,
    // the inverting feedback path and the output-side make-up gain are wired by
    // core-filter-5; here the loop gain is FORCED to zero and make-up stays unity so
    // the cascade is feed-forward (the LINEAR core).
    reso01_     = reso01;
    k_          = 0.0f;   // linear core: no feedback
    makeUpGain_ = 1.0f;   // make-up curve is core-filter-5 scope
}

float LadderFilter::processSample(float x) noexcept {
    // §5.5 forward-Euler four-stage tanh cascade with the global feedback gain k = 0,
    // so the inverting-feedback term (fb = diodeClamp(k * fbComp, vClamp)) is zero and
    // the stage-1 input is the bare input transconductor. The structure, evaluation
    // order and the ~5 tanh evals/sample budget match §5.5 / Huovilainen so the full
    // feedback wiring (core-filter-5) drops in without restructuring.
    const float invTwoVt = cal::vcf::invTwoVt;   // OTA knee scaler (PI), from calibration
    const float bias     = cal::vcf::kAntiDenorm; // anti-denormal bias (PI)

    // Per-stage coupling coefficient. The physically-derived Huovilainen one-pole is
    //   dVc/dt = (Ictl/C)*[tanh(Vin/2Vt) - tanh(Vc/2Vt)]   [research/10 §3.6 eq.]
    // whose forward-Euler small-signal pole is at 1 - (Ictl/(C*Fs))*(1/2Vt). The
    // documented small-signal coefficient g = 1 - exp(-2*pi*fc/fs_os) IS that pole
    // distance from unity [research/10 §3.6; ADR-003 F-01], so the coupling
    // coefficient on the tanh-difference must be g*(2Vt) = g / invTwoVt for the net
    // small-signal pole to land at 1 - g (24 dB/oct at the documented cutoff, F-01).
    // The §5.5 pseudocode wrote `g*(in - w)` for brevity, folding the 2Vt knee
    // normalization that this division makes explicit; the realized tuning is the
    // research-derived one. invTwoVt > 0 (a frozen (PI) device-physics constant), so
    // this is a constant divide, not a data-dependent branch (F-02).
    const float gc = g_ / invTwoVt;

    // Step 3 (k = 0): stage-1 input transconductor. With feedback off this is just the
    // OTA-knee tanh of the input.
    const float in0 = fastTanhKnee(x, invTwoVt);

    // Forward-Euler one-poles: y[i] += gc*(w[i-1] - w[i]); w[i] = fastTanhKnee(y[i]).
    // A tiny anti-denormal bias is folded into each accumulation so a decay-to-silence
    // tail never enters the subnormal range (F-12). The per-sample work is fixed and
    // data-independent: no Newton iteration, no signal-dependent branch (F-02).
    y_[0] += gc * (in0   - w_[0]) + bias;  w_[0] = fastTanhKnee(y_[0], invTwoVt);
    y_[1] += gc * (w_[0] - w_[1]) + bias;  w_[1] = fastTanhKnee(y_[1], invTwoVt);
    y_[2] += gc * (w_[1] - w_[2]) + bias;  w_[2] = fastTanhKnee(y_[2], invTwoVt);
    y_[3] += gc * (w_[2] - w_[3]) + bias;  w_[3] = fastTanhKnee(y_[3], invTwoVt);

    // Step 4: maintain the feedback history for the next sample's two-sample average.
    // Unused while k == 0, but kept valid so core-filter-5 reads a consistent history.
    fbPrev_ = y_[3];

    // Step 5: stage-4 output (the lowpass). The make-up gain is NOT applied here — it
    // is exposed via makeUpGain() for the downstream VCA drive node (F-06).
    return y_[3];
}

void LadderFilter::processBlock(float* samplesOs, int numSamplesOs) noexcept {
    for (int n = 0; n < numSamplesOs; ++n) {
        samplesOs[n] = processSample(samplesOs[n]);
    }
}

} // namespace mw::dsp
