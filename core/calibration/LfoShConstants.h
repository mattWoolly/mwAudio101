// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/LfoShConstants.h — (PI) calibration constants for the LFO digital
// uniform sample/hold "Random" core (task 059).
//
// Per the parallel-fleet conflict-avoidance rule, a module's (PI) constants land in a
// dedicated header that #includes the calibration root and EXTENDS the reserved
// mw::cal::lfo namespace (the orchestrator wires this include from Calibration.h
// later); they are NOT appended directly to the shared Calibration.h. Every constant
// here is (PI) — a *pragmatic invention*, NOT a measured SH-101 spec — referenced
// (never duplicated) by core/dsp/Lfo.cpp so no DSP call site inlines a literal
// [docs/design/03 §3.5; docs/design/00 §8.3; ADR-008 §1; ADR-020 S13].
//
// The "Random" position is a digital CPU+DAC pseudo sample/hold (NOT an analog noise
// S/H): a sample/hold register reloaded with a UNIFORM pseudo-random value on each LFO
// cycle edge, internally mapped to [-1, 1] (from the original 0..+10 V 6-bit
// pseudo-S/H) [docs/design/03 §3.5; research/04 §3.4]. The generator is a SEEDED POD
// so the value stream is deterministic / golden-reproducible [docs/design/03 §3.3,
// §3.5; docs/design/00 §9.2].

#pragma once

#include <cstdint>

#include "Calibration.h"   // extends the mw::cal::lfo namespace reserved there

namespace mw::cal::lfo {

// Fixed golden seed for the LFO Random S/H PRNG state (rngState_). Seeding to a fixed
// nonzero constant on reset()/prepare() makes the uniform value stream bit-identical
// run-to-run AND across platforms, so the golden harness stays stable [docs/design/03
// §3.5; docs/design/00 §9.2; ADR-013]. (PI) — reproducibility anchor, not a measured
// fact. (Distinct from the per-voice drift seed; the LFO core itself is deterministic
// and is not a drift source.)
inline constexpr std::uint64_t kLfoRandomSeed = 0x5151'5151'5151'5151ULL;

// PCG-XSH-RR 64/32 step constants for the inline Random S/H generator. These mirror
// the project's mw::util::Prng (core/util/Prng.h) so the LFO's seeded uint64 state
// advances with the SAME pure-integer arithmetic (wraparound defined => identical on
// every conforming compiler/platform; no FP, no libm) [core/util/Prng.h; docs/design/
// 11 §5.1; docs/design/00 §9.2]. (PI) — algorithm choice, isolated for re-tune.
inline constexpr std::uint64_t kLfoRandomLcgMultiplier = 6364136223846793005ULL;
inline constexpr std::uint64_t kLfoRandomLcgIncrement  = 1442695040888963407ULL; // odd

} // namespace mw::cal::lfo
