// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/VintageMacroConstants.h — the (PI) Age-macro -> target curve table
// kAgeCurve and the per-target group ceilings the VintageMacro host-thread mapping
// scales toward (task 066).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h. It #includes that root and APPENDS its values into
// the SAME namespace (mw::cal::drift) that Calibration.h reserves for the vintage
// stream (and that DriftConstants.h already extends), so the headers compose
// additively and no DSP / mapping call site inlines a (PI) literal
// [docs/design/08 §1.2, §10.1; docs/design/06 §3.10; ADR-008 §1; ADR-020 S13].
//
// PROVENANCE (docs/design/08 §1.3, §10.1; ADR-009 §Decision 7 — no-oracle):
//   - the Age (Vintage) macro and its macro->target curve are an "analog-modeling
//     embellishment", NOT a measured SH-101 spec [docs/design/08 §10 contract row,
//     §10.1]. The curve SHAPE (kAgeCurve) and the per-target ceilings are (PI) —
//     pragmatic inventions / TUNABLE DEFAULTS. A re-tune is one localized edit here.
//
// WHAT THIS OWNS:
//   - kAgeCurve: the (PI) monotonic non-decreasing macro->unit-scale lookup table that
//     docs/design/08 §10.1 names ("The macro -> target curve is a (PI) table kAgeCurve
//     in Calibration.h"). curve(0) == 0 (in tune on load, VV-1 / §10.2) and
//     curve(1) == 1 (full vintage). The host-thread VintageMacro::apply maps age01
//     through this table to a single unit scale s, then lerps each group target from
//     its zero-perturbation baseline toward its ceiling.
//   - the per-target group ceilings the macro drives the drift/variance group toward.
//     These mirror the docs/design/06 §3 schema MAXIMA for the scaled params so a full
//     Age (s==1) lands each target at the top of its schema range, and s==0 lands it
//     at the zero-offset baseline [docs/design/08 §10.1; §10 contract table].
//
// OUT OF SCOPE: the audio-thread drift DSP (vintage-4), the Tier-1/3 + variance band
// widths (DriftConstants.h, task 065), and the parameter IDs/ranges themselves
// (owned by the param schema, docs/design/06 §3; consumed, never re-minted here).

#pragma once

#include <array>
#include <cstddef>

#include "Calibration.h"

namespace mw::cal::drift {

// ---------------------------------------------------------------------------
// kAgeCurve — the (PI) Age-macro -> unit-scale lookup table [docs/design/08 §10.1].
//
// A monotonic NON-DECREASING shaping curve over the normalized Age parameter
// (age01 in [0,1]) returning a unit scale s in [0,1]. The curve is deliberately
// convex-ish (gentle near zero, opening up toward full Age) so that low Age settings
// stay "alive but unmistakably in tune" while still reaching the full schema range at
// age==1 [docs/design/08 §10.1; §10 contract row "in tune on load"]. The exact knots
// are (PI) tunable defaults; only the contract endpoints are normative:
//   curve(0) == 0  (zero added group offset == in tune on load, VV-1 / §10.2)
//   curve(1) == 1  (full vintage; each target reaches its schema ceiling)
//
// Sampled on a uniform grid of kAgeCurveSize knots over age01 in [0,1]; the mapping
// linearly interpolates between knots (see VintageMacro::curve). Monotonicity is a
// build-checked invariant below.
// ---------------------------------------------------------------------------

inline constexpr std::size_t kAgeCurveSize = 9;  // (PI) knot count (8 segments)

// (PI) knots, age01 = i/(N-1). Monotonic non-decreasing, endpoints 0 and 1. The
// gentle low end (small steps near 0) realizes the "alive but in tune" low-Age feel.
inline constexpr std::array<float, kAgeCurveSize> kAgeCurve = {{
    0.000f,  // age 0.000 -> 0      (in tune on load; VV-1 / §10.2)
    0.040f,  // age 0.125
    0.110f,  // age 0.250
    0.210f,  // age 0.375
    0.340f,  // age 0.500
    0.490f,  // age 0.625
    0.660f,  // age 0.750
    0.830f,  // age 0.875
    1.000f,  // age 1.000 -> 1      (full vintage; each target at its schema ceiling)
}};

// --- Compile-time invariants on the curve (the (PI) discipline made mechanical) ---

// Endpoints are exactly 0 and 1 (zero-offset at age 0; full range at age 1).
static_assert(kAgeCurve.front() == 0.0f,
              "VintageMacroConstants: kAgeCurve(0) MUST be 0 (in tune on load) "
              "[docs/design/08 §10.2; ADR-009 VV-1].");
static_assert(kAgeCurve.back() == 1.0f,
              "VintageMacroConstants: kAgeCurve(1) MUST be 1 (full schema range) "
              "[docs/design/08 §10.1].");

// Monotonic NON-DECREASING (a heavier Age never reduces the group it scales).
consteval bool mwAgeCurveMonotonic() {
    for (std::size_t i = 1; i < kAgeCurve.size(); ++i) {
        if (kAgeCurve[i] < kAgeCurve[i - 1]) return false;
    }
    return true;
}
static_assert(mwAgeCurveMonotonic(),
              "VintageMacroConstants: kAgeCurve MUST be monotonic non-decreasing "
              "[docs/design/08 §10.1].");

// ---------------------------------------------------------------------------
// Per-target group ceilings — the top-of-range each scaled target reaches at s==1
// [docs/design/08 §10.1; §10 contract table]. These mirror the docs/design/06 §3
// schema MAXIMA for the drift/variance group; the macro lerps from the zero-offset
// baseline (depth/slop/var = 0, rate = min) toward these. Held here (not inlined at
// the mapping site) so a schema-range change is one localized edit. (PI) defaults.
// ---------------------------------------------------------------------------

inline constexpr float kAgeDriftDepthMaxCents = 50.0f;  // mw101.drift.depth schema max (cents)
inline constexpr float kAgeDriftRateMinHz     = 0.01f;  // mw101.drift.rate schema min (Hz) — baseline
inline constexpr float kAgeDriftRateMaxHz     = 1.0f;   // mw101.drift.rate schema max (Hz)
inline constexpr float kAgeTuneSlopMaxCents   = 20.0f;  // mw101.tune.slop schema max (cents)
inline constexpr float kAgeVarMax             = 1.0f;   // mw101.var.* schema max (0..1 amount)

} // namespace mw::cal::drift
