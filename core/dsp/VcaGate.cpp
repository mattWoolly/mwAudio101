// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/VcaGate.cpp — VCA anti-thump gate fade + ENV/GATE mode handling (task 060).
// Implements the §4.6 / §4.4 / §6.5 surface declared by VcaGate.h.
//
//   target = gateOpen ? (mode==Gate ? 1.0 : envControl) : kVcaOffsetNull   (§4.4/§4.6)
//   fade_.setTarget(target); control = fade_.process()                      (§4.6 one-pole)
//
// The fade is the single canonical one-pole smoother (mw::params::OnePoleSmoother,
// task 008) shared per ADR-020 S10; its coefficient is derived in prepare() from the
// (PI) kVcaAntiThumpMs fade time and the control-rate tick rate (§4.6, §6.2). The
// nulled floor at gate close uses kVcaOffsetNull (§4.6). Both (PI) constants resolve
// from core/calibration/EnvLfoVcaConstants.h — never inlined here [ADR-020 S13].
//
// SCOPE (task 060): the gate-edge fade, the DC-offset null at the transition, and the
// ENV/GATE target selection (§4.4/§4.6/§6.5). OUT OF SCOPE and owned elsewhere: the
// taper/tanh transfer (Vca, task 056, §4.3), LFO-tremolo summing and velocity scaling
// (ModRouting, §4.4/§5), and the ADR-005 crossfade implementation (control-core). This
// component produces the click-safe control; Vca::process applies the gain.
//
// RT invariants [ADR-001, ADR-019 VT-01, ADR-020 S14]: all state is POD; the only
// sizing/derivation happens in prepare(); no heap allocation and no locks on the audio
// thread; tickControl()/processControlBlock() are noexcept hot paths.

#include "VcaGate.h"

#include "../calibration/EnvLfoVcaConstants.h"

namespace mw101::dsp {

void VcaGate::prepare (double sampleRate, int controlRateDivider) noexcept
{
    // The fade advances on the control-rate tick: fc = sampleRate / controlRateDivider
    // (docs/design/03 §6.2). Guard a non-positive divider to a per-sample rate so the
    // coefficient is always well-defined.
    const int    div          = controlRateDivider > 0 ? controlRateDivider : 1;
    const double tickRateHz   = sampleRate / static_cast<double> (div);

    // Fade time constant is the (PI) kVcaAntiThumpMs (§4.6). The shared smoother takes
    // the time constant in SECONDS and the tick rate in Hz, matching the canonical
    // mw::params::OnePoleSmoother contract (task 008).
    const double tauSeconds = static_cast<double> (mw::cal::vca::kVcaAntiThumpMs) / 1000.0;
    fade_.prepare (tauSeconds, tickRateHz);

    reset();
}

void VcaGate::reset() noexcept
{
    // Known start: gate closed, control parked at the nulled floor (§4.6 — clean by
    // default). reset(value) sets BOTH current and target so there is no startup
    // transient (canonical smoother contract).
    gateOpen_ = false;
    fade_.reset (static_cast<double> (mw::cal::vca::kVcaOffsetNull));
}

void VcaGate::setMode (VcaMode mode) noexcept
{
    // A mode flip only changes the TARGET; the fade carries the current control level
    // toward the new target, so ENV<->GATE is click-safe (§4.4/§6.5). The target is
    // re-derived on the next tickControl(), so storing the mode is sufficient here.
    mode_ = mode;
}

void VcaGate::gateOn() noexcept
{
    // Note-on: open the gate. The target becomes the mode's amplitude source; the fade
    // ramps the control up from its current level (§4.6 onset fade, no thump).
    gateOpen_ = true;
}

void VcaGate::gateOff() noexcept
{
    // Note-off: close the gate. The target becomes the nulled floor (kVcaOffsetNull);
    // the fade ramps the control down rather than dropping instantly (§4.6 offset fade).
    gateOpen_ = false;
}

double VcaGate::targetFor (float envControl) const noexcept
{
    if (! gateOpen_)
    {
        // Closed gate: the nulled floor (§4.6). kVcaOffsetNull default 0 == clean.
        return static_cast<double> (mw::cal::vca::kVcaOffsetNull);
    }

    // Open gate (§4.4): GATE holds a flat full level (1.0) for the gate duration; ENV
    // follows the ADSR-shaped control passed in.
    if (mode_ == VcaMode::Gate)
        return 1.0;

    // ENV mode: track the envelope contour. Clamp to the documented [0,1] control
    // window so a stray out-of-range input cannot push the fade outside the range.
    double c = static_cast<double> (envControl);
    if (c < 0.0) c = 0.0;
    else if (c > 1.0) c = 1.0;
    return c;
}

float VcaGate::tickControl (float envControl) noexcept
{
    fade_.setTarget (targetFor (envControl));
    return static_cast<float> (fade_.process());
}

void VcaGate::processControlBlock (float* out, const float* envControl, int n) noexcept
{
    // Identical to n successive tickControl() calls (verified against the per-tick
    // reference in the vca_thump processControlBlock test).
    for (int i = 0; i < n; ++i)
    {
        fade_.setTarget (targetFor (envControl[i]));
        out[i] = static_cast<float> (fade_.process());
    }
}

} // namespace mw101::dsp
