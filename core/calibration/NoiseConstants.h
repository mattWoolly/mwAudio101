// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/NoiseConstants.h — (PI) calibration constants for the white
// NoiseSource (task 028).
//
// Per the conflict-avoidance rule for the parallel development fleet, a module's
// (PI) constants land in a dedicated header rather than being appended directly to
// the shared core/calibration/Calibration.h orchestrator (which wires this include
// in later). Every constant here is (PI) — a *pragmatic invention*, NOT a measured
// SH-101 spec — and is referenced (never duplicated) by core/dsp/NoiseSource so no
// DSP call site inlines a literal [docs/design/01 §6.3, §6.4, §10 "(PI) central-
// ization"; docs/design/00 §8.3; ADR-008 §1].

#pragma once

#include <cstdint>

namespace mw::cal::noise {

// Optional gentle analog op-amp HF rolloff corner, modeling R86/C32:
//   f_c = 1 / (2*pi*220k*240p) ~ 3 kHz [docs/design/01 §6.4; research/02 §4.2/§4.3].
// This is NOT a pinking filter; the rolloff defaults OFF (the output is white).
// (PI) — derived value, centralized here so the DSP source references it.
inline constexpr double kNoiseHfRolloffHz = 3000.0;

// xorshift32 -> [-1, 1) scaling (research/10 §6): out = (float)x * (2 / 2^32) - 1.
// 2^32 = 4294967296. The generator choice (xorshift32) is the project default per
// the docs/design/01 §6.3 scope note and is isolated behind NoiseSource so it can
// be swapped for a 64-bit LCG/PCG without touching callers. (PI).
inline constexpr float  kNoiseScale  = 2.0f / 4294967296.0f;   // 2 / 2^32
inline constexpr float  kNoiseOffset = 1.0f;                   // maps [0,2) -> [-1,1)

// Largest float strictly below 1.0f (== 1 - 2^-24). Single-precision rounds the top
// of the uint32 range UP to exactly +1.0f in the bare scaling formula, which would
// break the half-open [-1, 1) contract [docs/design/01 §6.3, §10]; that lone case is
// clamped to this value. (PI) — float-domain guard, not a measured fact.
inline constexpr float  kNoiseMaxBelowOne = 0.99999994f;       // 1 - 2^-24

// Per-voice nonzero reseed fallback: xorshift32 cannot escape the all-zero state, so
// a zero seed is replaced by this golden-ratio odd constant [docs/design/01 §6.3].
// (PI) — reproducibility anchor, not a measured fact.
inline constexpr std::uint32_t kNoiseZeroSeedFallback = 0x9E3779B9u;

} // namespace mw::cal::noise
