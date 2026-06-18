// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/ModRouting.cpp — the fixed-routing COMBINE math (task 057).
// Implements the ModRoutingCombiner entry points DECLARED by task 053 in
// core/dsp/ModRouting.h. Realizes docs/design/03 §5.1 (depth scaling), §3.5
// (mod-bus kModBusLpHz one-pole LPF) and §5.2 (velocity routing into VCA level +
// VCF cutoff amount; velocity ON by default per ADR-016 R-2).
//
// Every (PI) scalar is read from core/calibration/EnvLfoVcaConstants.h, never
// inlined here [ADR-020 S13; docs/design/03 §3.5, §5.3].

#include "ModRouting.h"

#include "../calibration/EnvLfoVcaConstants.h"   // mw::cal::lfo::kModBusLpHz, etc.

#include <cmath>

namespace mw101::dsp {

void ModRoutingCombiner::prepare(double sampleRate) noexcept
{
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;

    // §3.5 mod-bus low-pass: a fixed (PI) ~16 kHz one-pole on the modulation signals.
    // One-pole coefficient a = 1 - exp(-2*pi*fc/fs) so y[n] = y[n-1] + a*(x - y[n-1]);
    // derived here (non-RT), never on the audio thread. Same one-pole form used by
    // the shared NoiseSource HF rolloff so there is one filter flavor, not two.
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    const double fc = static_cast<double>(mw::cal::lfo::kModBusLpHz);   // (PI) corner
    const double a  = 1.0 - std::exp(-kTwoPi * fc / sampleRate_);

    bus_.lpState = 0.0f;
    bus_.lpCoeff = static_cast<float>(a);
}

void ModRoutingCombiner::reset() noexcept
{
    bus_.lpState = 0.0f;
}

ModContributions ModRoutingCombiner::combine(const ModDepths& depths,
                                             const VelocityRouting& vel,
                                             float envLevel,
                                             float lfoValue,
                                             float velNorm) noexcept
{
    // §3.5: the fixed kModBusLpHz one-pole sits on the modulation bus and filters the
    // LFO value once for ALL destinations (not duplicated per shape/destination). The
    // envelope contour is a generated control signal and is NOT bus-filtered here
    // (the bus LPF is the modulation-noise/edge tamer for the LFO path, §3.5/§6.1).
    bus_.lpState += bus_.lpCoeff * (lfoValue - bus_.lpState);
    const float lfo = bus_.lpState;

    // §5.2: velocity is ON by default (ADR-016 R-2). enabled folds to a 0/1 gate so
    // the OFF pole removes BOTH the VCA-level scaling and the VCF cutoff contribution.
    const float velEnabled = vel.enabled ? 1.0f : 0.0f;

    ModContributions out;

    // §5.1: scale the one envelope and the one LFO by the per-destination depths.
    out.pitchMod = depths.lfoToPitch * lfo;                                  // VCO pitch
    out.pwMod    = depths.envToPw * envLevel + depths.lfoToPw * lfo;          // PWM

    // §5.2 VCF cutoff amount: env + LFO depth contributions, plus the additive
    // velocity contribution cutoffMod += velNorm * toCutoffAmount * velEnabled.
    out.cutoffMod = depths.envToCutoff * envLevel
                  + depths.lfoToCutoff * lfo
                  + velNorm * vel.toCutoffAmount * velEnabled;

    // §5.2 VCA level: baseAmp is the ENV/GATE source level summed with the LFO
    // tremolo contribution (§4.4), then scaled by velocity:
    //   vcaControl = baseAmp * lerp(1.0, velNorm, toVcaAmount * velEnabled).
    // At full velocity (velNorm=1) the level is unchanged; at low velocity it scales
    // down by toVcaAmount. OFF (velEnabled=0) collapses the lerp factor to 1.0.
    const float baseAmp   = envLevel + depths.lfoToVca * lfo;
    const float velFactor = vel.toVcaAmount * velEnabled;
    const float velLerp   = 1.0f + (velNorm - 1.0f) * velFactor;   // lerp(1, velNorm, f)
    out.vcaControl = baseAmp * velLerp;

    return out;
}

} // namespace mw101::dsp
