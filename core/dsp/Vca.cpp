// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Vca.cpp — BA662A-class OTA VCA control-law taper + tanh soft-drive
// (task 056). Implements the §4.3 transfer declared by Vca.h (task 052):
//
//   taper(control) = pow(clamp(control, 0, 1), kVcaTaperExp)            // §4.3
//   out = tanh(kVcaOtaDrive * taper(control) * in) / tanh(kVcaOtaDrive) // §4.3
//
// kVcaTaperExp selects the perceptual curve (1.0 = linear-in-current raw OTA decay
// shape; >1.0 pre-shaped toward dB-linear as if an exp converter were present). The
// tanh is the OTA soft taper; with kVcaOtaDrive in the linear window the VCA stays
// near unity-per-taper, matching typical Roland input attenuation. The tanh is
// normalized by tanh(kVcaOtaDrive) so that at full control (taper(1)==1) a full-scale
// input maps to full-scale output (§4.3 acceptance hook / §4.5 full-scale reference).
// There is NO documented dedicated saturation stage; drive is an optional character
// control only [docs/design/03 §4.3, §4.5, §4.7; research/04 §4.3, §4.7, §5.1].
//
// Both (PI) constants resolve from core/calibration/EnvLfoVcaConstants.h — the single
// cross-module calibration table — never inlined here [docs/design/03 §1.2;
// ADR-020 S13].
//
// SCOPE (task 056): the §4.3 taper/tanh transfer of process()/processBlock() and the
// optional setDrive() character hook (§4.5). OUT OF SCOPE and owned elsewhere: the
// anti-thump gate fade (task 010, §4.6), ENV vs GATE amplitude-source selection and
// the LFO-tremolo summing (ModRouting / task 010, §4.4), and FX-drive chain placement
// (ADR-017). setMode()/reset() keep the documented POD state coherent for those later
// tasks without changing this task's transfer.
//
// RT invariants [ADR-001, ADR-019 VT-01, ADR-020 S14]: all state is POD; the
// sampleRate is the only thing prepare() records (no tables to size for this stage);
// no heap allocation and no locks on the audio thread; process()/processBlock() are
// noexcept hot paths.

#include "Vca.h"

#include "../calibration/EnvLfoVcaConstants.h"

#include <cmath>

namespace mw101::dsp {

void Vca::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    // No coefficient tables are needed for the §4.3 taper/tanh transfer; the
    // anti-thump fade coefficient (gateFadeCoeff_) is computed by task 010 (§4.6).
    reset();
}

void Vca::reset() noexcept
{
    // Clear the anti-thump state to a known start (owned/used by task 010, §4.6).
    // The taper/tanh transfer itself is stateless, so there is nothing else to clear.
    offsetNull_ = 0.0f;
    gateFade_   = 0.0f;
}

void Vca::setMode (VcaMode mode) noexcept
{
    // Mode (ENV vs GATE source) is consumed by the amplitude-source assembly in
    // ModRouting / task 010 (§4.4); it does not change the §4.3 taper/tanh transfer.
    mode_ = mode;
}

void Vca::setDrive (float driveNorm) noexcept
{
    // Optional OTA character control (§4.5). The §4.3 transfer this task implements
    // uses the centralized kVcaOtaDrive (the documented default linear window); the
    // mapping of driveNorm onto an additional character stage is FX-chain placement
    // governed by ADR-017 and is OUT OF SCOPE here. Store the request for that later
    // task so the documented POD surface stays coherent.
    drive_ = driveNorm;
}

float Vca::process (float in, float control) noexcept
{
    // taper(control) = pow(clamp(control, 0, 1), kVcaTaperExp)  (§4.3).
    float c = control;
    if (c < 0.0f) c = 0.0f;
    else if (c > 1.0f) c = 1.0f;

    const float taper = std::pow (c, mw::cal::vca::kVcaTaperExp);

    // out = tanh(kVcaOtaDrive * taper * in) / tanh(kVcaOtaDrive)  (§4.3). The
    // denominator is a positive constant (kVcaOtaDrive default 1.0 > 0); normalizing
    // by it makes full control + full input map to full scale (§4.3 / §4.5).
    const float drive = mw::cal::vca::kVcaOtaDrive;
    const float norm  = std::tanh (drive);
    return std::tanh (drive * taper * in) / norm;
}

void Vca::processBlock (float* buffer, const float* control, int n) noexcept
{
    // Hoist the drive normalization out of the loop; the transfer is identical to
    // process() applied per sample (verified against the per-sample reference in the
    // vca_taper processBlock test).
    const float drive = mw::cal::vca::kVcaOtaDrive;
    const float exp   = mw::cal::vca::kVcaTaperExp;
    const float norm  = std::tanh (drive);

    for (int i = 0; i < n; ++i)
    {
        float c = control[i];
        if (c < 0.0f) c = 0.0f;
        else if (c > 1.0f) c = 1.0f;

        const float taper = std::pow (c, exp);
        buffer[i] = std::tanh (drive * taper * buffer[i]) / norm;
    }
}

} // namespace mw101::dsp
