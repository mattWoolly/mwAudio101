// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/VoiceDriftConstants.h — the per-voice drift-seed mixing function
// and the (PI) steal-fade length for Voice (task 073).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h. It #includes that root and APPENDS its values into
// the SAME mw::cal::voice namespace that VoiceConstants.h (task 067) opened, so the
// pool-sizing caps and these voice-assembly constants compose additively and no call
// site inlines a (PI) literal [docs/design/00 §1.2; docs/design/06 §3.10; ADR-008 §1;
// ADR-020 S13]. The calibration orchestrator wires this include from Calibration.h.
//
// WHAT THIS OWNS:
//   - hashCombine(instanceSeed, voiceIndex): the per-voice drift seed mixer named by
//     docs/design/04-voice-and-control.md §4.4 ("seed = hashCombine(instanceSeed,
//     static_cast<uint32_t>(voiceIndex))"). It MUST be pure integer arithmetic so the
//     seed is byte-stable run-to-run and across the macOS arm64 bless gate / Linux
//     co-gate [ADR-006 §Decision item 1, C18; ADR-019 VT-04]. The mixing constants
//     are (PI) — a pragmatic-invention mixer, NOT a measured SH-101 value.
//   - kStealFadeMs: the (PI) steal fade length (§6.4 — "e.g. a 1-3 ms ramp"), the
//     fast forced-fade-then-reuse window a stolen poly voice ramps stealGain_ over
//     before going Idle. It is a modern addition with no hardware analog [ADR-013].
//
// OUT OF SCOPE (other ADRs / docs own these): the drift DSP law / walk coefficients /
// vintage.age scaling (ADR-009 — DriftConstants.h), and the canonical drift PRNG
// stream itself (mw::dsp::drift::Xorshift128p / seedFromInstance, task 063).

#pragma once

#include <cstdint>

#include "Calibration.h"

namespace mw::cal::voice {

// Per-voice drift seed mixer [docs/design/04 §4.4; ADR-006 C18]. A SplitMix64-style
// integer avalanche over (instanceSeed, voiceIndex), truncated to 32 bits to match
// the §4.2 `uint32_t seed` field. Pure fixed-width unsigned arithmetic => bit-
// identical run-to-run and across platforms (no FP, no transcendental, no wall-clock,
// defined wraparound). constexpr-evaluable so the seed can be checked at compile time.
//
// The two multipliers are the standard SplitMix64 finalizer constants (Steele/Vigna);
// the voiceIndex is spread by an odd golden-ratio multiplier before being folded in
// so adjacent voice indices decorrelate (Fibonacci hashing). These are (PI) mixer
// constants, not measured values.
constexpr std::uint32_t hashCombine(std::uint32_t instanceSeed,
                                    std::uint32_t voiceIndex) noexcept {
    // Spread voiceIndex with the odd golden-ratio multiplier, fold into the seed.
    std::uint64_t x = static_cast<std::uint64_t>(instanceSeed)
                    ^ (static_cast<std::uint64_t>(voiceIndex) * 0x9E3779B97F4A7C15ULL);
    // SplitMix64 finalizer avalanche.
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x = x ^ (x >> 31);
    // Fold the high half into the low half so both halves of the avalanche influence
    // the returned 32-bit seed.
    return static_cast<std::uint32_t>(x ^ (x >> 32));
}

// (PI) steal fade length [docs/design/04 §6.4]. A stolen poly voice ramps stealGain_
// to zero over this many milliseconds (a fast forced fade, NOT a hard cut), then goes
// Idle so the slot can be reused [ADR-006 C15]. Modern addition; no hardware analog.
inline constexpr float kStealFadeMs = 2.0f;   // (PI) 2 ms, in the §6.4 1-3 ms window

} // namespace mw::cal::voice
