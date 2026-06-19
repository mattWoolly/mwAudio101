// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/VcoShapeConstants.h — (PI) calibration constants for the
// band-limited VCO SHAPE construction: the PWM width map (50% .. ~5% duty) and the
// per-voice HQ auto-escalation pitch threshold (task 030).
//
// Per the parallel-fleet conflict-avoidance rule, a module's (PI) constants land in
// a DEDICATED calibration header rather than being appended directly to the shared
// core/calibration/Calibration.h orchestrator. This header #includes Calibration.h
// and EXTENDS the mw::cal::vco namespace that Calibration.h declares as an empty
// placeholder; the VCO shape DSP source references these constants here and NEVER
// inlines the literals at a call site
// [docs/design/01-dsp-oscillators.md §4.5, §4.6, §2.3, §10 "(PI) centralization";
//  ADR-002 C3, C9; docs/design/00 §8.3; ADR-008 §1; AGENTS.md "centralize it"].
//
// Every numeric figure here is (PI) — a *pragmatic invention* / tunable default
// bounded by the cited research, NOT a measured SH-101 oracle.

#pragma once

#include "Calibration.h"

namespace mw::cal::vco {

// ---------------------------------------------------------------------------
// PWM width map [docs/design/01 §4.6; research/02 §2.5, §7.1; ADR-002 C2/C3].
//   duty = kPwmDutyMax - pwmCvNorm * (kPwmDutyMax - kPwmDutyMin)
// so pwmCvNorm = 0 => 50% (square), pwmCvNorm = 1 => ~5%.
// ---------------------------------------------------------------------------

// Max duty (square) at pwmCvNorm == 0. NOT a contested figure: the CEM3340 pulse
// is symmetric (50%) at the centre of its width control [research/02 §2.5].
inline constexpr float kPwmDutyMax = 0.5f;

// Min duty floor at pwmCvNorm == 1. Medium-confidence research (the AMSynths clone
// ~5% observation, NOT the Roland "50% to 0%" spec): (PI), centralized here so the
// floor can be re-tuned without touching the DSP source [research/02 §2.5, §8.2, §8.4].
inline constexpr float kPwmDutyMin = 0.05f;

// Resolve normalized PWM CV in [0,1] to a raw duty fraction (before the dt overlap
// clamp of §4.6 / ADR-002 C3). Centralized so no DSP call site inlines the map.
[[nodiscard]] inline constexpr float pwmDutyFromCvNorm (float pwmCvNorm) noexcept
{
    if (pwmCvNorm < 0.0f) pwmCvNorm = 0.0f;
    if (pwmCvNorm > 1.0f) pwmCvNorm = 1.0f;
    return kPwmDutyMax - pwmCvNorm * (kPwmDutyMax - kPwmDutyMin);
}

// ---------------------------------------------------------------------------
// HQ auto-escalation pitch threshold [docs/design/01 §2.3; ADR-002 C9;
// ADR-018 Q6; research/10 §8 Table VIII].
//
// Any voice whose VCO fundamental exceeds this threshold switches from the
// closed-form PolyBLEP residual to the minBLEP applicator while the condition
// holds — internal model behavior keyed off pitch (the Valimaki limit), NOT a
// user parameter.
// ---------------------------------------------------------------------------

// 2000 Hz fundamental. (PI) rounded DOWN from the cited 2135 Hz perceptual
// aliasing-free limit for 2nd-order PolyBLEP at 44.1 kHz [research/10 §8, NI=2].
inline constexpr double kHqEscalationHz = 2000.0;

// The cited 2135 Hz figure is referenced to 44.1 kHz; scale the threshold with the
// sample rate so higher rates push aliases further out before escalating. (PI)
// engineering convenience [docs/design/01 §2.3].
inline constexpr double kHqEscalationRefSr = 44100.0;

[[nodiscard]] inline constexpr double hqEscalationHzAt (double sampleRate) noexcept
{
    if (sampleRate <= 0.0)
        return kHqEscalationHz;
    return kHqEscalationHz * (sampleRate / kHqEscalationRefSr);
}

} // namespace mw::cal::vco
