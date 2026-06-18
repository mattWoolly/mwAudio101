// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/GlideConstants.h — (PI) calibration constants for the per-voice
// portamento slew (task 068).
//
// These are the cross-module (PI) constants for the glide RC-style exponential
// integrator [docs/design/04-voice-and-control.md §5.5; ADR-005 §Decision item 2].
// They live in a NEW mw::cal::glide namespace — a sibling of the central
// core/calibration/Calibration.h table — so the parallel fleet does not serialize
// on Calibration.h, and so no call site inlines a (PI) literal [docs/design/00
// §1.2 calibration policy; docs/design/06 §3.10; AGENTS.md "(PI) discipline"].
// Glide.h includes THIS header directly; the orchestrator wires the include into
// Calibration.h later.
//
// Every value here is (PI) — a *pragmatic invention*, NOT a measured SH-101 spec.
// The glide CURVE shape (linear vs exponential/RC) and whether the slide is
// constant-time or constant-rate are explicitly UNDOCUMENTED
// [docs/research/05-mixer-modulation-glide.md §5 honest label]. The model is an
// RC-style exponential integrator on the pitch CV with the time constant mapped
// from the 0-5 s TIME knob; that mapping is the engineering choice frozen here.

#pragma once

#include "Calibration.h"

namespace mw::cal::glide {

// The documented TIME knob span [docs/research/05 §5, §6: "Portamento Time
// (0 ~ 5s)", confirmed]. Range, not (PI) — the endpoints are measured spec.
inline constexpr float kTimeMinSeconds = 0.0f;
inline constexpr float kTimeMaxSeconds = 5.0f;

// (PI) mapping from the 0-5 s TIME knob to the one-pole RC time constant (tau, in
// seconds) used by the exponential slew. The hardware curve is undocumented
// [docs/research/05 §5 honest label], so we map TIME linearly onto tau: tau = TIME.
// Larger TIME => larger tau => slower convergence to the pitch target. This makes
// the acceptance contract ("longer TIME yields slower convergence") hold by
// construction while keeping the math trivially testable against the per-tick
// one-pole coefficient. (PI).
inline constexpr float kTimeToTauScale = 1.0f; // tau_seconds = scale * timeSeconds

// (PI) snap-to-target threshold so the slew lands exactly on the target rather
// than asymptoting forever, and so the "is-gliding" state is deterministic. Kept
// in step with cal::smoothing::kSnapThreshold (the project-wide de-zipper snap
// epsilon) [docs/design/04 §5.5; ADR-020 S10]. Units: pitch (Hz) error. (PI).
inline constexpr float kSnapThreshold =
    static_cast<float>(mw::cal::smoothing::kSnapThreshold);

} // namespace mw::cal::glide
