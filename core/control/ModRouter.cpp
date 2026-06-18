// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/ModRouter.cpp — the fixed LFO/ADSR modulation router (task 082).
//
// Implements docs/design/05 §3.2: the branch-light, allocation-free resolve()
// mapping one instantaneous LFO value plus the shared ADSR value into
// pitch / PWM / cutoff / VCA-tremolo via independent fixed depth gains and the
// PWM/VCA source switches [ADR-007 §Decision 1, C1, C2]. The hot path is noexcept,
// allocation-free and lock-free [ADR-007 C26; ADR-001 C3/C4].

#include "control/ModRouter.h"

namespace mw::control {

void ModRouter::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
}

void ModRouter::setPwmSource(PwmSource s) noexcept {
    pwmSource_ = s;
}

void ModRouter::setVcaSource(VcaSource s) noexcept {
    vcaSource_ = s;
}

void ModRouter::setDepths(const ModDepths& d) noexcept {
    depths_ = d;
}

ModOutputs ModRouter::resolve(const ModInputs& in) const noexcept {
    ModOutputs out{};

    // LFO → VCO pitch (single MOD depth, vibrato) — docs/design/05 §3.1, §3.3.
    out.pitchMod = in.lfoValue * depths_.lfoToPitch;

    // VCF cutoff = LFO depth + ADSR ENV depth (a SUM of both fixed paths) — §3.2/C3.
    out.cutoffMod = in.lfoValue * depths_.lfoToCutoff
                  + in.envValue * depths_.envToCutoff;

    // PWM by the source switch (§3.1/§3.2, C2): LFO when = Lfo, ENV when = Env,
    // otherwise the static MANUAL width.
    out.pwmMod = (pwmSource_ == PwmSource::Lfo) ? in.lfoValue * depths_.lfoToPwm
               : (pwmSource_ == PwmSource::Env) ? in.envValue * depths_.envToPwm
                                                : in.pwmManual;

    // LFO → VCA tremolo (SH-101-specific path summed at the VCA control node) — §3.1.
    out.vcaTremolo = in.lfoValue * depths_.lfoToVca;

    return out;
}

} // namespace mw::control
