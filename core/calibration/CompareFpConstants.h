// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/CompareFpConstants.h — constants for the CLASS-FP two-stage
// comparer (task 043).
//
// Per the parallel-fleet conflict-avoidance rule, this module's constants land in a
// dedicated header that #includes (and extends the mw::cal namespace of) the shared
// core/calibration/Calibration.h, rather than being appended directly to it
// [AGENTS.md "ADRs & decisions"; docs/design/00 §8.3].
//
// The perceptual alias limit is NOT a (PI) pragmatic invention: it is the
// published Valimaki-Pekonen-Nam 2012 result cited by the design — ~2135 Hz for the
// 2nd-order PolyBLEP (NI=2) and ~7.8 kHz for the 3rd-order B-spline
// [docs/research/10 §8 (Table VIII / abstract); docs/design/11 §6.3]. The comparer
// reports alias-floor energy ABOVE this limit; the per-corpus ceiling it is gated
// against lives in the MANIFEST, never here [docs/design/11 §6.3/§6.4; ADR-013 C7].
//
// The default Stage-1 fast-reject margin and the FFT window length below ARE (PI):
// engineering choices for the offline comparer with no measured-oracle backing.

#pragma once

#include <cstddef>

#include "Calibration.h"

namespace mw::cal::golden {

// Perceptual alias-free limit (Hz), the boundary above which Stage 2 accumulates
// "alias-floor" energy [docs/research/10 §8; docs/design/11 §6.3]. The 2nd-order
// PolyBLEP perceptual limit (~2135 Hz fundamental at fs=44.1 kHz, NI=2) is the
// tighter, default boundary used by the comparer's alias-floor metric.
inline constexpr double kAliasFloorLimitHzNi2     = 2135.0;  // Valimaki 2012 Table VIII
inline constexpr double kAliasFloorLimitHzBSpline = 7800.0;  // Valimaki 2012 abstract

// The comparer's default alias-floor boundary (the tighter NI=2 limit). A render
// keyed by a different correction order can pass a different limit in via FpTolerance;
// this is only the default the comparer uses when none is overridden.
inline constexpr double kAliasFloorLimitHz = kAliasFloorLimitHzNi2;

// (PI) — Stage-1 fast-reject margin. Stage 1 raises its flag (and Stage 2 runs) once
// any scalar fingerprint metric reaches this fraction of its tolerance band; below
// it, the difference is comfortably inside tolerance and Stage 2 is skipped
// [docs/design/11 §6.1]. 1.0 == flag exactly at the band edge (the conservative
// choice: never skip Stage 2 on a sample that is already at the limit).
inline constexpr double kStage1FlagMargin = 1.0;  // (PI) — fast-reject at the band edge

// (PI) — default windowed-FFT analysis length for Stage 2 (power of two). The
// comparer Hann-windows and zero-pads / truncates to this length per analysis frame
// [docs/design/11 §6.3]. A buffer shorter than this is analyzed in a single padded
// frame.
inline constexpr std::size_t kStage2FftLength = 4096;  // (PI) — FFT analysis length

} // namespace mw::cal::golden
