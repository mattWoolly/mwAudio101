// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/FractionalDelayLineConstants.h — (PI) calibration constants for
// the shared FractionalDelayLine (task 089).
//
// Per AGENTS.md every invented constant is tagged (PI) and centralized in a
// calibration header. To avoid serializing on the single Calibration.h orchestrator
// while the development fleet runs in parallel, this module's (PI) constants live in
// their own header in the mw::cal namespace; the orchestrator wires the include into
// Calibration.h later. This header introduces NO measured-spec assertions.
//
// The (PI) choice here is the interpolation order used by FractionalDelayLine::read.
// docs/design/07-fx-section.md §5.3: "Interpolation is at least linear; cubic/Lagrange
// is permitted ... the (PI) choice of interpolation order centralizes in Calibration.h."

#pragma once

namespace mw::cal::fracdelay {

// Interpolation order for the fractional read tap. 1 == linear (the §5.3 contract
// floor); a higher order (e.g. 3 for cubic/Lagrange) is permitted. We ship LINEAR:
// it is the documented at-least-linear floor, is exact for ramps/DC, and keeps the
// hot-path read branch-free and allocation-free [docs/design/07-fx-section.md §5.3].
// (PI) — pragmatic invention; no SH-101 oracle for the FX delay interpolation.
inline constexpr int kInterpolationOrder = 1;

} // namespace mw::cal::fracdelay
