// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ArpConstants.h — the Arpeggiator (PI) tunable-constant block
// (task 084).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h. It carries the arp subsystem default(s) in a new
// mw::cal::arp namespace, composing additively with the root table; the calibration
// orchestrator wires this include from Calibration.h later. No control call site
// inlines this literal [docs/design/05 §5.2; ADR-007 C11; ADR-020 S13; docs/design/06
// §3.10].
//
// EVERY constant here is (PI) — a *pragmatic invention* / TUNABLE DEFAULT, NOT a
// measured SH-101 spec. The exact U&D turnaround math (whether the top and bottom
// notes repeat at the turnaround) is only partially traced in the recovered firmware
// [research/06 §2.1, §8.2; research/07 §5.4, §9], so it is exposed as a documented
// switchable choice and NOT asserted bit-exact [docs/design/05 §5.2; ADR-007 C11].

#pragma once

#include "Calibration.h"   // composes into the same mw::cal root table

namespace mw::cal {

// ---------------------------------------------------------------------------
// §5.2 — Arpeggiator U&D (up-and-down) turnaround default. The default choice is a
// pragmatic invention since no source fixes it [docs/design/05 §5.2; ADR-007 C11].
//
//   false (default) => endpoints NOT repeated: a 4-note set plays 1 2 3 4 3 2 ...
//   true            => endpoints repeated:     a 4-note set plays 1 2 3 4 4 3 2 1 ...
// ---------------------------------------------------------------------------
namespace arp {
    inline constexpr bool kArpUandDRepeatEndpoints = false;   // (PI) tunable default
}

} // namespace mw::cal
