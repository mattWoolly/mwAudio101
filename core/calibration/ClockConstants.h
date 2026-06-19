// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ClockConstants.h — the Clock (arp/seq/RANDOM shared clock node)
// (PI) tunable-constant block (task 086).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h; it #includes that root and APPENDS into the same
// mw::cal namespaces it reserves, so the two headers compose additively without
// editing the shared Calibration.h [docs/design/06 §3.10; ADR-020 S13; docs/design/00
// §8.3]. No call site inlines any literal below.
//
// EVERY constant here is (PI) — a *pragmatic invention* / TUNABLE DEFAULT, NOT a
// measured SH-101 spec. SWING has no hardware oracle [docs/design/05 §7.6;
// research/06 §8 — swing has no oracle], so kSwingTaper is an honest engineering map.

#pragma once

#include "Calibration.h"

namespace mw::cal {

// ---------------------------------------------------------------------------
// §7.6 — SWING taper (host-sync only). SWING delays even-numbered step edges by a
// deterministic sample offset. The taper has NO hardware oracle; this linear map
// (50% -> 0, 75% -> half a step) is the pragmatic invention named in the design doc
// as `kSwingTaper` [docs/design/05 §7.6; ADR-007 §Resolution 2].
//
//   swingOffsetSamples = kSwingTaper(s) * stepPeriodSamples
//                      = (s - 0.5) * 2.0 * stepPeriodSamples   for s in [0.50, 0.75].
//
// At s = 0.50 the offset is 0 (swing off); at s = 0.75 it is 0.5 step periods.
// ---------------------------------------------------------------------------
namespace clock {
    // SWING fraction range endpoints [docs/design/05 §7.6; ADR-007 C24].
    inline constexpr float kSwingMin     = 0.50f;   // (PI) — swing off (no offset)
    inline constexpr float kSwingMax     = 0.75f;   // (PI) — max delay = half a step

    // Internal-clock rate range endpoints (Hz) [docs/design/05 §7.3; research/06 §4.1].
    // The 0.1 Hz / 30 Hz endpoints are the normative LFO/CLK range; the taper between
    // them is owned by the LFO/param subsystem, not minted here.
    inline constexpr float kInternalRateMinHz = 0.1f;   // (PI clamp) — normative low end
    inline constexpr float kInternalRateMaxHz = 30.0f;  // (PI clamp) — normative high end

    // The linear SWING taper: fraction of one step period to delay an even step edge
    // by, given a swing fraction s in [kSwingMin, kSwingMax]. 50% -> 0, 75% -> 0.5.
    // (PI) — no oracle [docs/design/05 §7.6].
    inline constexpr float kSwingTaper(float s) noexcept {
        return (s - 0.5f) * 2.0f;   // (s - 0.5) * 2.0; 0.50 -> 0.0, 0.75 -> 0.5
    }
}

} // namespace mw::cal
