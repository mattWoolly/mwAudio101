// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/LadderReferenceTPT.cpp — implementation of the offline TPT/ZDF 4-pole
// ladder reference oracle (task 034). Contract: docs/design/02-dsp-filter.md §8;
// theory: docs/research/10 §3.2 (TPT/ZDF), §3.3 (prewarp g = tan(pi*fc/fs)),
// §3.4 (H(0) = 1/(1+k), k = 4 self-osc threshold).
//
// Per trapezoidal one-pole (Zavalishin TPT lowpass) with instantaneous gain
// G = g/(1+g):
//     v = (u - s) * G              // integrator input scaled by G
//     y = v + s                    // lowpass output  == G*u + (1-G)*s
//     s = y + v                    // trapezoidal state update for the next sample
//
// Four poles in series with a single global negative feedback k around the whole
// cascade form a zero-delay loop. With u1 = x - k*y4, u_{i+1} = y_i, and writing
// each stage's state contribution as Y_i = (1-G)*s_i, the chain gives
//     y4 = G^4*(x - k*y4) + S,   where  S = G^3*Y1 + G^2*Y2 + G*Y3 + Y4.
// Solving the delay-free loop instantaneously:
//     y4 = (G^4*x + S) / (1 + k*G^4).
// We then recover u1 = x - k*y4 and run the per-pole updates in order, which keeps
// every integrator state consistent with the solved output (no iteration). This is
// the textbook ZDF solve; it is exact for the LINEAR model (no tanh).

#include "dsp/LadderReferenceTPT.h"

#include <cmath>

namespace mw::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Clamp a value into [lo, hi].
[[nodiscard]] double clampD(double v, double lo, double hi) noexcept {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

} // namespace

void LadderReferenceTPT::prepare(double fsHz) noexcept {
    fs_ = (fsHz > 0.0) ? fsHz : 48000.0;
    reset();
}

void LadderReferenceTPT::reset() noexcept {
    s_[0] = s_[1] = s_[2] = s_[3] = 0.0;
}

void LadderReferenceTPT::setCutoffHz(double fcHz) noexcept {
    // Keep fc in a stable open band: above 0 and strictly below Nyquist so that
    // tan(pi*fc/fs) stays finite and positive. The shipping engine owns the
    // user-facing clamp (docs/design/02 §6); here we only guard the prewarp.
    const double nyquist = 0.5 * fs_;
    const double fc = clampD(fcHz, 1.0e-4, 0.49 * fs_); // 0.49*fs => below the tan pole
    (void) nyquist;
    g_ = std::tan(kPi * fc / fs_); // TPT prewarp (docs/research/10 §3.3)
    G_ = g_ / (1.0 + g_);          // instantaneous one-pole gain
}

void LadderReferenceTPT::setResonanceK(double k) noexcept {
    // Analytic model: k in [0, 4); k = 4 is the self-oscillation threshold
    // (docs/research/10 §3.4). Guard the open interval so the linear solve never
    // sees a non-positive loop denominator at DC.
    k_ = clampD(k, 0.0, 4.0);
}

double LadderReferenceTPT::processSample(double x) noexcept {
    const double G  = G_;
    const double G2 = G * G;
    const double G3 = G2 * G;
    const double G4 = G3 * G;

    // State contribution of each stage: Y_i = (1 - G) * s_i.
    const double oneMinusG = 1.0 - G;
    const double Y1 = oneMinusG * s_[0];
    const double Y2 = oneMinusG * s_[1];
    const double Y3 = oneMinusG * s_[2];
    const double Y4 = oneMinusG * s_[3];

    // Accumulated state seen at stage 4 through the forward chain.
    const double S = G3 * Y1 + G2 * Y2 + G * Y3 + Y4;

    // Delay-free (ZDF) solve for the stage-4 output.
    const double y4 = (G4 * x + S) / (1.0 + k_ * G4);

    // Recover the stage-1 input from the inverting global feedback.
    const double u1 = x - k_ * y4;

    // Run each one-pole in order so the integrator states stay consistent with the
    // solved output. y_i = G*u_i + (1-G)*s_i; v_i = (u_i - s_i)*G; s_i = y_i + v_i.
    double u = u1;
    for (int i = 0; i < 4; ++i) {
        const double v = (u - s_[i]) * G;
        const double y = v + s_[i];
        s_[i] = y + v;   // trapezoidal state update
        u = y;           // this stage's output feeds the next stage's input
    }

    return u; // == y4 (last stage output)
}

} // namespace mw::dsp
