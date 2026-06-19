// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/LadderFilter.cpp — the shipping per-voice Huovilainen four-stage one-pole
// OTA cascade. See LadderFilter.h and docs/design/02 §3, §5.1-§5.6 under ADR-003
// F-01..F-08, F-10, F-11, F-12.
//
// Task 038 (the LINEAR core) built the four-stage cascade with the global feedback
// gain k FORCED TO ZERO. Task 039 (this resonance task) WIRES THE FEEDBACK LOOP into
// the same surface without changing the header (as task 038 anticipated): setResonance
// maps reso01 -> normalized loop gain k via the calibrated taper and computes the
// output-side make-up Q scalar (§5.1, §5.3, ADR-003 F-05/F-06); processSample now
// realizes the §5.5 inverting feedback with the +0.5-sample two-sample-average phase
// compensation (F-03), the feedback-path diode clamp as the amplitude governor (§5.4,
// F-04), and the residual half-sample tuning compensation (§7.3) applied to the
// effective loop gain.
//
// All (PI) constants are read from core/calibration/* (the mw::cal::vcf namespace):
//   - the OTA knee scaler invTwoVt and the tanh approximation come from FastTanh.h /
//     FastTanhConstants.h (task 033);
//   - the cutoff->g map and the resoTuningComp(g) factor come from FilterTables
//     (task 035);
//   - the anti-denormal bias comes from LadderFilterConstants.h (task 038);
//   - the resonance->k taper (kMax, kResoCurveExp), the make-up depth (makeUpDepth) and
//     the diode-clamp threshold (vClamp) come from LadderResonanceConstants.h (this
//     task).
// No (PI) numeric literal is inlined here [ADR-003 F-15; docs/design/02 §10 F-15].

#include "dsp/LadderFilter.h"

#include <cmath> // std::pow — control-rate only (setResonance), never the audio hot path

#include "calibration/LadderResonanceConstants.h"

namespace mw::dsp {

namespace {

// Symmetric diode-clamp feedback limiter [docs/design/02 §5.4; ADR-003 F-04;
// docs/research/03 §4.2]. Memoryless soft saturator on the (inverting) feedback signal:
// it passes small feedback with unit slope (so it does NOT shift the resonant onset)
// but limits large excursions, "reducing the level as soon as it starts to conduct" and
// bounding the loop to a fixed point. Realized with the shared fastTanh so NO std::tanh
// reaches the audio thread (F-10): out = vClamp * fastTanh(fb / vClamp), bounded to
// +/- vClamp. The (PI) shape is one of the acceptable realizations the design names
// ("a tanh-with-larger-knee or a polynomial soft-clip"); the normative requirement is
// that it lives in the FEEDBACK path and is the amplitude governor [docs/design/02 §5.4].
[[nodiscard]] inline float diodeClamp(float fb, float vClamp) noexcept {
    return vClamp * fastTanh(fb / vClamp);
}

} // namespace

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
    // Control-rate setter (§5.1, §5.3; ADR-003 F-05/F-06). Maps the normalized control
    // reso01 in [0,1] to the normalized loop gain k in [0, kMax] via the calibrated
    // taper, and computes the output-side make-up Q scalar from the SAME taper.
    //
    // Clamp the control to [0,1]; reso01 == 1 reaches self-oscillation onset
    // [docs/design/02 §5.1]. std::pow runs here (control rate, once per control block),
    // NOT on the audio hot path — consistent with setCutoffHz's table-index log2 (F-10
    // governs the audio thread, not the setters).
    float r = reso01;
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    reso01_ = r;

    // resonanceCurve(reso01) = reso01^kResoCurveExp, a unit-output x^p taper
    // (resonanceCurve(1) == 1) [docs/design/02 §5.1, §9]. kResoCurveExp is (PI).
    const float curve = std::pow(r, cal::vcf::kResoCurveExp);

    // k = kMax * resonanceCurve(reso01); kMax is the (PI) normalized self-osc loop gain
    // reached at reso01 = 1 [docs/design/02 §5.1; ADR-003 F-05]. This is the DOCUMENTED
    // normalized loop gain in [0, kMax] that loopGainK() reports; the residual
    // half-sample feedback-tuning compensation (§7.3) is applied to the effective loop
    // gain per sample in processSample (where the current g is in hand), not baked in
    // here, so it always tracks the live cutoff [docs/design/02 §7.3; ADR-003 F-14].
    k_ = cal::vcf::kMax * curve;

    // makeUpGain = 1 + makeUpDepth * resonanceCurve(reso01); rises monotonically with
    // resonance [docs/design/02 §5.3; ADR-003 F-06]. Exposed via makeUpGain() for the
    // downstream VCA drive; it is NOT applied inside processSample and the filter INPUT
    // is never scaled with resonance [docs/design/02 §5.3 "(PI)"; ADR-003 F-06].
    makeUpGain_ = 1.0f + cal::vcf::makeUpDepth * curve;
}

float LadderFilter::processSample(float x) noexcept {
    // §5.5 forward-Euler four-stage tanh cascade with the SH-101 resonance topology:
    // inverting global feedback with the +0.5-sample two-sample-average phase
    // compensation (F-03), diode-clamped in the feedback path (F-04). The structure,
    // evaluation order and the ~5 tanh evals/sample budget match §5.5 / Huovilainen.
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

    // Step 1: feedback-phase compensation. The +0.5-sample delay is the average of the
    // last two stage-4 outputs, holding the feedback phase ~180 deg at cutoff up to
    // Fs/4 [docs/design/02 §5.5 step 1; docs/research/10 §3.8; ADR-003 F-03]. At the top
    // of this sample y_[3] holds the output from sample n-1 and fbPrev_ from sample n-2.
    const float fbComp = 0.5f * (y_[3] + fbPrev_);

    // Step 2: diode-clamp the INVERTING feedback (F-04). The effective loop gain folds
    // in the residual half-sample tuning compensation for the CURRENT cutoff coefficient
    // (§7.3, F-14); resoTuningComp is a per-SR table lookup built in prepare, near unity
    // by construction, allocation-free and noexcept (F-11). The clamp is the amplitude
    // governor: self-oscillation amplitude is its fixed point [docs/design/02 §5.4;
    // ADR-003 F-04/F-05].
    const float kEff = k_ * tables_.resoTuningComp(g_);
    const float fb   = diodeClamp(kEff * fbComp, cal::vcf::vClamp);

    // Step 3: stage-1 input transconductor with the global inverting feedback subtracted
    // (F-03). The make-up gain is NOT applied to the input — input scaling is invariant
    // to resonance [docs/design/02 §5.3, §5.5 step 3; ADR-003 F-06].
    const float in0 = fastTanhKnee(x - fb, invTwoVt);

    // Forward-Euler one-poles: y[i] += gc*(w[i-1] - w[i]); w[i] = fastTanhKnee(y[i]).
    // A tiny anti-denormal bias is folded into each accumulation so a decay-to-silence
    // tail never enters the subnormal range (F-12). The per-sample work is fixed and
    // data-independent: no Newton iteration, no signal-dependent branch (F-02).
    y_[0] += gc * (in0   - w_[0]) + bias;  w_[0] = fastTanhKnee(y_[0], invTwoVt);
    y_[1] += gc * (w_[0] - w_[1]) + bias;  w_[1] = fastTanhKnee(y_[1], invTwoVt);
    y_[2] += gc * (w_[1] - w_[2]) + bias;  w_[2] = fastTanhKnee(y_[2], invTwoVt);
    y_[3] += gc * (w_[2] - w_[3]) + bias;  w_[3] = fastTanhKnee(y_[3], invTwoVt);

    // Step 4: maintain the feedback history for the next sample's two-sample average.
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
