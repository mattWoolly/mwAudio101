// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ChorusConstants.h — (PI) calibration constants for the Juno-style
// Chorus stage (task 092).
//
// Per AGENTS.md every invented constant is tagged (PI) and centralized in a
// calibration header. To avoid serializing on the single Calibration.h orchestrator
// while the development fleet runs in parallel, this module's (PI) constants live in
// their own header that #includes Calibration.h and EXTENDS the mw::cal namespaces;
// the orchestrator wires this include into Calibration.h later. This header
// introduces NO measured-spec assertions — there is no physical-unit oracle for the
// FX Chorus [ADR-010 Consequences].
//
// These realize docs/design/07-fx-section.md §5.1.4 (the (PI) calibration table for
// the Chorus stage). Values are taste-calibrated against Juno/BBD literature.

#pragma once

#include "Calibration.h"

namespace mw::cal {

// Chorus stage (PI) constants — docs/design/07-fx-section.md §5.1.3 / §5.1.4.
// The Juno-style BBD widener: two anti-phase LFO-modulated fractional-delay lines.
// Each line's read offset = kChorusBaseDelayMs + depth*kChorusDepthMs*lfo(phase)
// [§5.1.3]. Modes I / II / I+II select LFO rate/depth presets [§5.1.4].
namespace chorus {

// Center delay of each BBD line (ms). The static "bucket-brigade" tap around which
// the LFO modulates. (PI) — docs/design/07-fx-section.md §5.1.4 (suggested 7.5 ms).
inline constexpr float kChorusBaseDelayMs = 7.5f;

// Maximum modulation excursion (ms) at depth == 1. The peak +/- swing the LFO adds
// to the base delay. (PI) — §5.1.4 (suggested 4.0 ms).
inline constexpr float kChorusDepthMs = 4.0f;

// Mode I LFO rate (Hz). The classic slow Juno chorus shimmer.
// (PI) — §5.1.4 (suggested 0.5 Hz).
inline constexpr float kChorusModeIRateHz = 0.5f;

// Mode II LFO rate (Hz). The faster, deeper Juno chorus.
// (PI) — §5.1.4 (suggested 0.83 Hz).
inline constexpr float kChorusModeIIRateHz = 0.83f;

// Mode I depth scalar (0..1). Multiplies the depth excursion for Mode I.
// (PI) — §5.1.4 (suggested 0.6).
inline constexpr float kChorusModeIDepth = 0.6f;

// Mode II depth scalar (0..1). Multiplies the depth excursion for Mode II.
// (PI) — §5.1.4 (suggested 1.0).
inline constexpr float kChorusModeIIDepth = 1.0f;

} // namespace chorus

} // namespace mw::cal
