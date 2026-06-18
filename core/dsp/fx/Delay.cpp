// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/Delay.cpp — translation-unit anchor for the FX Delay stage (task 093).
//
// The Delay DSP is defined inline in Delay.h (header-only, like FractionalDelayLine)
// so the chain can inline the hot path. This .cpp exists so the class is compiled
// into the mwcore static library — exercising the no-JUCE-in-core source guard and
// the frozen FP-discipline flags over the Delay translation unit, and matching the
// docs/design/07 §5.2.2 Delay.h/.cpp file layout. It defines no out-of-line members;
// including the header here is sufficient to type-check and codegen it under the
// disciplined flags [docs/design/07 §2.1, §5.2; ADR-001 C1, C12].

#include "Delay.h"

namespace mw::fx {

// Compile-time guards on the Delay contract that must hold wherever Delay is built
// (kept in the .cpp so they ride the disciplined mwcore TU). These mirror the
// acceptance criteria for §5.2 and ADR-010 FX-7/FX-8.
static_assert(mw::cal::delay::kDelayMaxFeedback < 1.0f,
              "Delay: feedback clamp MUST be strictly < 1.0 so the loop cannot "
              "diverge [docs/design/07 §5.2.4; ADR-010 FX-8].");

static_assert(mw::cal::delay::kDelayMaxMs > 0.0f,
              "Delay: max buffer length must be positive [docs/design/07 §5.2.6].");

// Tempo-sync oracle sanity (compile-time): 1/8 == half a beat, 1/4 == one beat.
static_assert(Delay::beatsPerDivision(0) == 1.0,
              "Delay: 1/4 division == one beat [docs/design/07 §5.2.3].");
static_assert(Delay::beatsPerDivision(1) == 0.5,
              "Delay: 1/8 division == half a beat [docs/design/07 §5.2.3].");

} // namespace mw::fx
