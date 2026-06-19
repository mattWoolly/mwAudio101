// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/DelayConstants.h — (PI) calibration constants for the FX Delay
// stage (task 093).
//
// Per AGENTS.md every invented constant is tagged (PI) and centralized in a
// calibration header that lives under core/calibration. To avoid serializing on the
// single Calibration.h orchestrator while the development fleet runs in parallel,
// this module's (PI) constants live in their own header and EXTEND the mw::cal
// namespace (adding mw::cal::delay); the orchestrator wires the include into
// Calibration.h later. This header #includes Calibration.h so the (PI) discipline
// (one home for every invented constant) holds and downstream code can include just
// this header. It introduces NO measured-spec assertions — there is no SH-101 oracle
// for the FX Delay [ADR-010 Consequences; ADR-017 re-affirmed locks].
//
// These realize docs/design/07-fx-section.md §5.2.6 verbatim (the (PI) table).

#pragma once

#include "Calibration.h"

namespace mw::cal::delay {

// Maximum delay buffer length in milliseconds — sizes the ring buffer once in
// prepare() so no runtime allocation ever occurs [docs/design/07 §5.2.6, §5.3].
// (PI) — pragmatic invention; no SH-101 oracle for the FX delay.
inline constexpr float kDelayMaxMs = 2000.0f; // (PI)

// Hard feedback clamp: requested feedback is clamped to [0, kDelayMaxFeedback] with
// kDelayMaxFeedback STRICTLY < 1.0 so the recirculating loop cannot diverge
// [docs/design/07 §5.2.4; ADR-010 FX-8, FX-10]. (PI).
inline constexpr float kDelayMaxFeedback = 0.95f; // (PI)

// Feedback one-pole damping LPF cutoff range (BBD-style high loss per repeat). The
// `damp` knob maps across [min, max] [docs/design/07 §5.2.4]. (PI).
inline constexpr float kDelayDampHzMin = 1500.0f;  // (PI) — 1.5 kHz (dark)
inline constexpr float kDelayDampHzMax = 18000.0f; // (PI) — 18 kHz (bright)

// Feedback saturation pre-gain into the gentle tanh soft-clip that keeps repeats
// from building up harshly [docs/design/07 §5.2.4; research/10 §4]. (PI).
inline constexpr float kDelaySatDrive = 1.2f; // (PI)

// Pointer-glide ramp time (ms) for click-free delay-time / division / tempo changes
// [docs/design/07 §5.2.5; ADR-010 FX-11]. (PI).
inline constexpr float kDelayTimeGlideMs = 30.0f; // (PI)

} // namespace mw::cal::delay
