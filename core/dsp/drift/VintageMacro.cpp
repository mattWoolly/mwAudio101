// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/drift/VintageMacro.cpp — out-of-line definition of the host-thread
// VintageMacro::apply mapping (task 066).
//
// apply() is the single non-audio-thread entry point: it maps the Age parameter
// (age01) to the scaled drift/variance group targets via VintageMacro::computeTargets
// (which itself maps through the (PI) kAgeCurve table) and writes ONLY the param
// TARGETS onto the canonical per-target de-zipper smoothers via setTarget(). It runs
// no audio-thread DSP: it never advances a smoother (no process()), runs no
// PRNG/thermal/filter work, and allocates nothing [docs/design/08 §3.2, §10.1;
// ADR-009 §Decision 7, VV-1]. The audio thread later reads those already-smoothed
// targets, so the macro costs the audio thread nothing.

#include "VintageMacro.h"

namespace mw::dsp::drift {

void VintageMacro::apply(float age01,
                         OnePoleSmoother& driftDepth,
                         OnePoleSmoother& driftRate,
                         OnePoleSmoother& tuneSlop,
                         OnePoleSmoother& varCutoff,
                         OnePoleSmoother& varEnvTime,
                         OnePoleSmoother& varPw,
                         OnePoleSmoother& varGlide) noexcept {
    const VintageTargets t = computeTargets(age01);
    // Touches ONLY the param TARGETS — no smoother process(), no audio-thread DSP.
    driftDepth.setTarget(t.driftDepthCents);
    driftRate.setTarget(t.driftRateHz);
    tuneSlop.setTarget(t.tuneSlopCents);
    varCutoff.setTarget(t.varCutoff);
    varEnvTime.setTarget(t.varEnvTime);
    varPw.setTarget(t.varPw);
    varGlide.setTarget(t.varGlide);
}

} // namespace mw::dsp::drift
