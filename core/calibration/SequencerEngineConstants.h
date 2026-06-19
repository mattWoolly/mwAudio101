// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/SequencerEngineConstants.h — the SequencerEngine control-tick
// (PI) tunable-constant block (task 087).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h; it #includes that root and APPENDS into a new
// mw::cal::seq namespace it reserves, so the two headers compose additively without
// editing the shared Calibration.h [docs/design/06 §3.10; ADR-020 S13; docs/design/00
// §8.3]. No call site inlines any literal below.
//
// EVERY constant here is (PI) — a *pragmatic invention* / TUNABLE DEFAULT, NOT a
// measured SH-101 spec. The control-tick PERIOD models the coarse hardware loop of
// 1.5–3.5 ms; the shipped default is the ~2 ms VINTAGE tick named in [ADR-016 R-1] /
// [ADR-005] [docs/design/05 §2.2; research/07 §2.3; research/06 §6.1].

#pragma once

#include "Calibration.h"   // composes into the same mw::cal root table

namespace mw::cal {

// ---------------------------------------------------------------------------
// §2.2 — Control-tick period. The state machine (SequencerEngine) runs on a fixed
// control tick DECOUPLED from clock-edge placement [docs/design/05 §2.2; ADR-007
// §Resolution 4, C27]. The default models the coarse hardware super-loop:
//   - shipped default = 2.0 ms (the ~2 ms VINTAGE tick, ADR-016 R-1 / ADR-005);
//   - the documented hardware loop band is 1.5–3.5 ms (research/07 §2.3).
// Clock EDGES are placed at sample-accurate sub-block offsets independent of this
// tick, so host-sync stays tight while the authentic stepping feel is preserved.
// ---------------------------------------------------------------------------
namespace seq {
    // The shipped default control-tick period (seconds) [docs/design/05 §2.2;
    // ADR-007 §Resolution 4]. (PI) — the ~2 ms vintage tick.
    inline constexpr double kControlTickSeconds = 0.002;   // (PI) ~2 ms VINTAGE tick

    // The documented coarse-loop band the default sits inside [research/07 §2.3].
    // Exposed so tests can assert the default is the named vintage value within band.
    inline constexpr double kControlTickMinSeconds = 0.0015;  // (PI) band low (1.5 ms)
    inline constexpr double kControlTickMaxSeconds = 0.0035;  // (PI) band high (3.5 ms)
}

} // namespace mw::cal
