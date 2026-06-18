// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/SubOscillator.cpp — implementation of the 4013 phase-locked divider,
// diode-OR 25% pulse, and per-edge band-limiting (task 031). See SubOscillator.h.
// Realizes docs/design/01-dsp-oscillators.md §5.1-§5.6 / §10 and ADR-002 C4-C6.

#include "SubOscillator.h"

#include "PolyBlep.h"
#include "../calibration/Calibration.h"           // kDtMax (VCO clamp) — read, not inlined
#include "../calibration/SubOscillatorConstants.h"  // kSubHigh / kSubLow

namespace mw101::dsp {

namespace {

// Nyquist dt clamp: guarantees at most one wrap per sample so the divider sees exactly
// one clock edge per sample and the PolyBLEP leading/trailing windows never collide
// [docs/design/01 §4.4 "dt clamp" kDtMax = 0.5]. The VCO owns this clamp; the sub
// recomputes dt from freqHz independently for its own edge placement and applies the
// same ceiling so its behavior matches the master. (PI) safety clamp; centralized.
constexpr double kDtMax = 0.5;   // mirrors docs/design/01 §4.4 kDtMax (VCO) [ADR-002 C3]

// Apply one 4013 clock edge to (q1, q2): Q1 toggles each VCO wrap; Q2 toggles on Q1's
// rising edge (false->true) [§5.3]. Returns the post-edge state via the references.
inline void clockDivider (bool& q1, bool& q2) noexcept
{
    const bool q1prev = q1;
    q1 = ! q1;
    if ((! q1prev) && q1)        // rising edge of Q1
        q2 = ! q2;
}

} // namespace

// -----------------------------------------------------------------------------

void SubOscillator::prepare (double sampleRate, const MinBlepTable& hqTable) noexcept
{
    sampleRate_ = sampleRate;
    hqTable_    = &hqTable;
    blep_.prepare (hqTable, sampleRate);   // sizes the per-voice ring (init-time only)
    reset();
}

void SubOscillator::reset() noexcept
{
    q1_ = false;
    q2_ = false;
    level_ = levelFor (shape_, q1_, q2_);
    blep_.reset();
    // Prime the minBLEP applicator's held DC to the current level so the very first
    // samples in HQ mode hold the correct plateau (scheduleStep accumulates deltas).
    if (level_ != 0.0f)
        blep_.scheduleStep (level_, 1.0f);
}

void SubOscillator::setShape (SubShape s) noexcept
{
    if (s == shape_)
        return;
    shape_ = s;
    // The held level changes with the shape (a different projection of the same divider
    // state). Re-derive it; in HQ mode fold the level delta into the applicator so the
    // plateau follows. This is a structural (non-RT-critical) change, not a per-sample
    // path, but it remains allocation/lock-free.
    const float newLevel = levelFor (shape_, q1_, q2_);
    if (aaMode_ == OscAaMode::MinBlepHq && hqTable_ != nullptr)
        blep_.scheduleStep (newLevel - level_, 1.0f);
    level_ = newLevel;
}

void SubOscillator::setAaMode (OscAaMode m) noexcept
{
    aaMode_ = m;
}

float SubOscillator::levelFor (SubShape s, bool q1, bool q2) noexcept
{
    bool high = false;
    switch (s)
    {
        case SubShape::OctDownSquare:      high = q1;          break;  // Q1   (-1 oct)
        case SubShape::TwoOctDownSquare:   high = q2;          break;  // Q2   (-2 oct)
        case SubShape::TwoOctDown25Pulse:  high = (q1 || q2);  break;  // Q1 OR Q2 (-2 oct)
    }
    return high ? mw::cal::sub::kSubHigh : mw::cal::sub::kSubLow;
}

float SubOscillator::renderSample (float masterPhase, bool wrapped, double freqHz) noexcept
{
    // dt = freq/fs, clamped to Nyquist so the PolyBLEP windows are well-formed [§4.4].
    double dtd = (sampleRate_ > 0.0) ? (freqHz / sampleRate_) : 0.0;
    if (dtd > kDtMax) dtd = kDtMax;
    if (dtd < 0.0)    dtd = 0.0;
    const float dt = static_cast<float> (dtd);

    // --- Clock the divider on the saw wrap (the single 4013 rising clock edge). The
    //     step amplitude of THIS wrap is newLevel - oldLevel of the selected logic; it
    //     is derived from the SAME master accumulator and is the only edge this sample
    //     (one wrap per sample guaranteed by the dt clamp) [§5.3, §5.5].
    float stepThisWrap = 0.0f;
    if (wrapped)
    {
        const float oldLevel = level_;
        clockDivider (q1_, q2_);
        const float newLevel = levelFor (shape_, q1_, q2_);
        stepThisWrap = newLevel - oldLevel;
        level_ = newLevel;
    }

    if (aaMode_ == OscAaMode::MinBlepHq && hqTable_ != nullptr)
    {
        // HQ / escalated tier: schedule the band-limited step into the applicator at the
        // sub-sample wrap fraction, then pop the accumulated correction. The applicator
        // owns the held DC level (driven by scheduleStep), so we do NOT add level_ again.
        if (wrapped && stepThisWrap != 0.0f)
        {
            // The edge occurred when the phase crossed 1.0, i.e. (1 - masterPhase/dt) of
            // the way into THIS sample. frac in [0,1) places it sub-sample-accurately.
            float frac = (dt > 0.0f) ? (1.0f - masterPhase / dt) : 0.0f;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            blep_.scheduleStep (stepThisWrap, frac);
        }
        return blep_.next();
    }

    // --- Default tier: PolyBLEP. The naive output is the held level_; add the two-segment
    //     residual for the edge that brackets this sample, scaled by (A/2) to match the
    //     ADR-002 saw convention value = naive + (A/2)*polyBlep (a -2 step => coeff -1).
    //
    //     Just AFTER a wrap, masterPhase < dt (leading window): the edge is THIS wrap's
    //     step. Just BEFORE the next wrap, masterPhase > 1-dt (trailing window): the edge
    //     is the UPCOMING wrap's step, computed by simulating the next divide forward from
    //     the current state. For dt < 0.5 at most one window is active per sample, so a
    //     single polyBlep() call returns the matching segment [§5.5; ADR-002 C1, C5].
    float out = level_;

    if (dt > 0.0f)
    {
        if (masterPhase < dt)
        {
            // Leading: correct this sample for the step that just occurred at the wrap.
            out += 0.5f * stepThisWrap * polyBlep (masterPhase, dt);
        }
        else if (masterPhase > 1.0f - dt)
        {
            // Trailing: a wrap is imminent next sample. Compute its step amplitude by
            // simulating the divide forward from the current (post-this-sample) state.
            bool nq1 = q1_, nq2 = q2_;
            clockDivider (nq1, nq2);
            const float stepNext = levelFor (shape_, nq1, nq2) - level_;
            out += 0.5f * stepNext * polyBlep (masterPhase, dt);
        }
    }

    return out;
}

} // namespace mw101::dsp
