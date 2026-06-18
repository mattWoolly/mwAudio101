// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/SubOscillatorConstants.h — (PI) constants for the 4013-derived
// sub-oscillator (task 031).
//
// These are the (PI) tunables docs/design/01-dsp-oscillators.md §5 charters for the
// sub-oscillator. Per the conflict-avoidance rule for the parallel fleet they live in
// their OWN calibration header (one file = one module) and EXTEND the mw::cal namespace
// by #including the central Calibration.h, rather than editing it
// [AGENTS.md "Tag every invented constant (PI) and centralize it"; docs/design/01 §10
// "(PI) centralization"]. The DSP source reads these constants here and NEVER inlines
// the literals at a call site.

#pragma once

#include "Calibration.h"   // extend the mw::cal namespaces; do not edit that file

namespace mw::cal::sub {

// Bipolar output levels of the divider-derived squares / 25% pulse, BEFORE the sub
// LEVEL control (LEVEL is applied by the source mixer, not the sub-oscillator)
// [docs/design/01 §5.4, §5.6, §8]. The naive (pre-band-limit) output is +kSubHigh when
// the selected logic is high and kSubLow when low.
//
// (PI) — the nominal bipolar full-scale of a source signal. Centralized here so the
// plateau level is one localized edit, never duplicated at the call site [§10].
inline constexpr float kSubHigh =  1.0f;   // (PI) — logic-high output level
inline constexpr float kSubLow  = -1.0f;   // (PI) — logic-low output level

} // namespace mw::cal::sub
