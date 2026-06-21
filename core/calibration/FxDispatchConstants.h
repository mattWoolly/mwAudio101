// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/FxDispatchConstants.h — the (PI) constant set + decode helpers for
// the ParamSnapshot -> FxParams control-dispatch (task 163; ADR-028 item 5).
//
// Per the parallel-fleet conflict-avoidance rule, this module's (PI) constants land in a
// DEDICATED calibration header rather than being appended to the shared orchestrator
// core/calibration/Calibration.h [docs/design/00 §1.2; AGENTS.md "(PI) discipline"]. The
// FX dispatch references these by name and NEVER inlines a literal at a call site.
//
// WHAT IS DECODED HERE. Most FX params are normalized 0..1 knobs the stages interpret
// internally (drive amount/tone/output, chorus depth/width/mix, delay damp/width/mix), or
// already-engineering-range values (delay_feedback is 0..kDelayFeedbackMax in the
// registry), and pass straight through into FxParams. The TWO params whose FxParams field
// is in ENGINEERING units the dispatch must reconstruct are:
//
//   * mw101.fx.delay_time -> FxParams::DelayP::timeMs (free-delay milliseconds). The
//     design table fixes the musical span at 1..2000 ms with a log skew [docs/design/07
//     §5.2.7]. The registry stores the param as a kDelayTime-skewed 0..1 value; the test
//     bridge (and the shell) emit convertTo0to1, so the dispatch deskews to a linear 0..1
//     then LOG-maps it across [kDelayFreeMinMs, kDelayFreeMaxMs].
//   * mw101.fx.chorus_rate -> FxParams::ChorusP::rate (LFO Hz). The Chorus stage treats a
//     non-zero p.rate as an explicit Hz override of the mode preset [Chorus.cpp §5.1.3];
//     the design table fixes the override span at 0.05..10 Hz, log [docs/design/07 §5.1.5].
//     The chorus_rate registry param is a LINEAR 0..1, so the dispatch LOG-maps it across
//     [kChorusRateMinHz, kChorusRateMaxHz] (a chorus_rate of 0 leaves rate==0 -> the stage
//     keeps the mode-preset rate, which is the documented fallback).
//
// Every numeric figure here is (PI) — a *pragmatic invention* / tunable integration
// anchor, bounded by the cited design-table frames, NOT a measured SH-101 oracle. The
// musical spans intentionally match the docs/design/07 §5.x param tables.

#pragma once

#include <algorithm>
#include <cmath>

#include "DelayConstants.h"   // mw::cal::delay::kDelayMaxMs ceiling (clamp guard)

namespace mw::cal::fxdispatch {

// --- Delay free-time span (sync OFF) [docs/design/07 §5.2.7: "1..2000 ms, log skew"]. ---
inline constexpr float kDelayFreeMinMs = 1.0f;     // (PI) shortest free delay
inline constexpr float kDelayFreeMaxMs = 2000.0f;  // (PI) longest free delay (== ring max)

// --- Chorus rate override span [docs/design/07 §5.1.5: "0.05..10 Hz, log skew"]. ---
inline constexpr float kChorusRateMinHz = 0.05f;   // (PI) slowest LFO override
inline constexpr float kChorusRateMaxHz = 10.0f;   // (PI) fastest LFO override

// Log-map a LINEAR-normalized [0,1] value across [lo, hi] (geometric interpolation:
// lo * (hi/lo)^norm). Both bounds must be > 0. noexcept, pure arithmetic; the std::pow
// runs at control rate only (the per-block FX decode, never the per-sample path).
[[nodiscard]] inline float logMap(float norm, float lo, float hi) noexcept {
    const float n = std::clamp(norm, 0.0f, 1.0f);
    return lo * std::pow(hi / lo, n);
}

// Decode the kDelayTime-skewed (or already-linear) delay_time normalized value to free
// milliseconds. `deskewedNorm` is the LINEAR 0..1 the dispatch produces after inverting
// the registry skew. Clamped to the ring ceiling so a future span change can never
// request more than the buffer holds.
[[nodiscard]] inline float delayFreeMs(float deskewedNorm) noexcept {
    const float ms = logMap(deskewedNorm, kDelayFreeMinMs, kDelayFreeMaxMs);
    return std::min(ms, mw::cal::delay::kDelayMaxMs);
}

// Decode the chorus_rate normalized value (linear 0..1) to an LFO Hz override. A value of
// 0 maps to EXACTLY 0 Hz so the Chorus stage's `p.rate > 0` test keeps the mode-preset
// rate (the documented fallback); any positive value log-maps into the override span.
[[nodiscard]] inline float chorusRateHz(float norm) noexcept {
    const float n = std::clamp(norm, 0.0f, 1.0f);
    if (n <= 0.0f) return 0.0f;   // 0 => keep the mode preset rate [Chorus.cpp §5.1.3]
    return logMap(n, kChorusRateMinHz, kChorusRateMaxHz);
}

} // namespace mw::cal::fxdispatch
