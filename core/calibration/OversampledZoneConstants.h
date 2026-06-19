// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/OversampledZoneConstants.h — constants for the per-voice
// oversampled-zone WRAPPER (task 047, core-filter-8): the supported quality strides
// and the OS_CEILING clamp reference.
//
// Per the conflict-avoidance rule for the parallel development fleet, this module's
// constants land in a dedicated header that #includes (and extends the mw::cal
// namespace of) the shared core/calibration/Calibration.h, rather than being
// appended directly to it [AGENTS.md "ADRs & decisions"; docs/design/00 §8.3].
//
// The OS ceiling (192 kHz internal = 2x the top blessed rate) is NOT a (PI) pragmatic
// invention: it is normatively fixed by ADR-023 V15 and centralized in
// GoldenKeyConstants.h as `mw::cal::golden::kOsCeilingHz`. This header re-exports it
// under the zone namespace so the wrapper's clamp call-site does not inline a literal,
// and adds only the small set of zone-stride identifiers the wrapper needs
// [docs/design/00 §8.5 V15/V16; ADR-004 C9, C11].

#pragma once

#include "Calibration.h"
#include "GoldenKeyConstants.h"

namespace mw::cal::oszone {

// Supported oversampling strides (the per-voice quality tiers) [ADR-004 Contract
// rows 10/11; docs/design/00 §4.1]. 2x is the blessed, bit-exact default; 1x is the
// eco / pass-through tier (the resamplers are bypassed). 4x HQ re-derives the
// half-sample compensation and re-runs the CI floor and is OUT OF SCOPE for this
// task and for the underlying realtime Oversampler kernel (kMaxFactor = 2).
inline constexpr int kFactor1x      = 1;
inline constexpr int kFactor2x      = 2;
inline constexpr int kDefaultFactor = 2;   // ADR-004 C10 blessed default

// The oversampling ceiling above which 2x would push the internal rate past the
// filter-stability guard's safe range, so the factor is clamped to 1x. This is the
// SAME normative value as the golden harness reference; re-exported here so the zone
// wrapper's clamp does not inline a literal [docs/design/00 §8.5 V15; ADR-023 V15].
inline constexpr double kOsCeilingHz = mw::cal::golden::kOsCeilingHz;  // 192 kHz internal

// True iff running `factor`-times oversampling at host rate `hostFsHz` would push the
// internal (oversampled) rate strictly ABOVE the ceiling. When true the wrapper must
// clamp the active factor to 1x and record the clamp for provenance [ADR-023 V15/V16;
// docs/design/00 §8.5]. Strictly-above: a host rate whose 2x lands exactly on the
// ceiling (e.g. 96 kHz -> 192 kHz) is still allowed; only rates whose 2x EXCEEDS it
// (e.g. 176.4/192 kHz host) clamp.
[[nodiscard]] inline constexpr bool wouldExceedCeiling(double hostFsHz, int factor) noexcept {
    return hostFsHz * static_cast<double>(factor) > kOsCeilingHz;
}

} // namespace mw::cal::oszone
