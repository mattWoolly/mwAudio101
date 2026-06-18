// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/MinBlepConstants.h — (PI) constants for the minBLEP residual
// table and per-voice applicator (task 027).
//
// These are the (PI) tunables docs/design/01-dsp-oscillators.md §3.2 charters for
// the minBLEP table. They live in their OWN calibration header (one file = one
// module) so the parallel fleet does not serialize on core/calibration/Calibration.h
// [AGENTS.md "Tag every invented constant (PI) and centralize it"]. The orchestrator
// wires the Calibration.h include of this header later; until then DSP sources read
// these constants here and NEVER inline the literals at a call site
// [docs/design/01-dsp-oscillators.md §10 "(PI) centralization"; §3.2].

#pragma once

namespace mw::cal::minblep {

// Oversampling factor of the Blackman-windowed residual table.
// NOT (PI): fixed by the chosen method at 64x [research/10 §5.1; docs/design/01 §3.2].
inline constexpr int kOversampling = 64;

// Half-width of the retained sinc (number of zero-crossings / lobes kept each side).
// (PI) — truncation length chosen to put the stopband below the bless aliasing-floor
// gate [docs/design/01 §3.2; ADR-002 C8]. Tunable default; centralized here.
inline constexpr int kZeroCrossings = 16;

} // namespace mw::cal::minblep
