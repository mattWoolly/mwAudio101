// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/GoldenKeyConstants.h — the blessed sample-rate set and the
// oversampling ceiling, centralized for the golden harness (task 041).
//
// Per the conflict-avoidance rule for the parallel development fleet, this module's
// constants land in a dedicated header that #includes (and extends the mw::cal
// namespace of) the shared core/calibration/Calibration.h, rather than being
// appended directly to it [AGENTS.md "ADRs & decisions"; docs/design/00 §8.3].
//
// These values are NOT (PI) pragmatic inventions: the blessed set and the
// OS ceiling are normatively fixed by ADR-023 (V12, V15) and docs/design/11 §5.2.
// They are centralized here so neither the harness nor any DSP call site inlines a
// literal sample-rate list. The blessed sample-rate set keys both CLASS-EXACT and
// CLASS-FP golden corpora [docs/design/11 §5.2; ADR-023 V12].

#pragma once

#include <array>
#include <cstddef>

#include "Calibration.h"

namespace mw::cal::golden {

// The blessed sample-rate set: the two base production rates (44.1/48 kHz) and
// their 2x relatives implied by the mandatory 2x oversampling [docs/design/11 §5.2;
// ADR-023 V12, B3; ADR-003 §F-09]. Golden corpora (both determinism classes) are
// generated at each of these, keyed by sample rate. Order is ascending and stable.
inline constexpr std::array<double, 4> kBlessedSampleRatesHz = {{
    44100.0, 48000.0, 88200.0, 96000.0,
}};

// Oversampling ceiling: 2x the top blessed rate (96 kHz) = 192 kHz internal. When
// 2x oversampling would push the internal rate above this, the factor is clamped to
// 1x so the fc <= 0.45*fs_os stability guard continues to hold [docs/design/11 §5.2;
// ADR-023 V15; ADR-003 §F-08]. Defined here as the harness/engine-tag provenance
// reference; the clamp itself is owned by the engine task.
inline constexpr double kOsCeilingHz = 192000.0;

// True iff `sampleRate` is exactly one of the blessed set. Exact comparison is
// correct: the blessed rates are exact integer-valued doubles, and a render keyed by
// a non-blessed rate is refused, never tolerance-matched [docs/design/11 §5.2;
// ADR-023 V12, V14, V17].
inline constexpr bool isBlessedSampleRate(double sampleRate) noexcept {
    for (double r : kBlessedSampleRatesHz)
        if (sampleRate == r) return true;
    return false;
}

} // namespace mw::cal::golden
