// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ModuleClassExactCorpusConstants.h — (PI) corpus-authoring constants
// for the per-module CLASS-EXACT golden corpora (task 080, golden / module-class-exact).
//
// Per the conflict-avoidance rule for the parallel development fleet, this module's
// constants land in a dedicated header that #includes (and extends the mw::cal
// namespace of) the shared core/calibration/Calibration.h, rather than being appended
// directly to it [AGENTS.md "ADRs & decisions"; docs/design/00 §8.3]. It re-uses (does
// NOT redefine) the blessed sample-rate set already centralized in GoldenKeyConstants.h
// (mw::cal::golden::kBlessedSampleRatesHz) [docs/design/11 §5.2].
//
// WHAT THIS DECLARES. The stimulus parameters of the SIX integer/control corpora the
// CLASS-EXACT corpus is blessed from — sequencer per-step bytes, the 4013 divider-OR
// sub-osc edge indices, the fixed-seed integer PRNG stream, the arp step ordering, the
// param-smoothing block boundaries, and the CC/param map [docs/design/11 §5.1 EXACT
// members; ADR-013 C5]. Each is rendered to an exact integer-valued f32 payload and
// SHA-256 hash-compared (a one-byte diff FAILS), bit-for-bit identical on macOS arm64
// AND Linux x64 [docs/design/11 §6.2; ADR-013 C5].
//
// HONESTY. Every numeric here is (PI) — a pragmatic stimulus/authoring choice, NOT a
// measured SH-101 spec [docs/design/11 §1.3; ADR-013 owner ratification]. The corpus
// proves the integer/control DSP still renders what it rendered when blessed; it can
// NEVER prove fidelity to a real SH-101. The fixed PRNG seed reproduces identically
// across runs and platforms because the generator is pure integer arithmetic
// [docs/design/11 §5.1; core/util/Prng.h].

#pragma once

#include <array>
#include <cstdint>

#include "Calibration.h"
#include "GoldenKeyConstants.h"   // mw::cal::golden::kBlessedSampleRatesHz (re-used, not redefined)

namespace mw::cal::golden::classexact {

// --- PRNG-stream corpus ------------------------------------------------------------
// (PI) the fixed seed for the integer-PRNG-stream golden. The CLASS-EXACT PRNG corpus
// uses a FIXED seed and reproduces identically across runs (and across arm64 / Linux,
// since the PCG generator is pure fixed-width integer arithmetic) [docs/design/11 §5.1
// — "fixed-seed integer PRNG stream"]. (PI).
inline constexpr std::uint64_t kPrngSeed = 12345ULL;

// (PI) the number of 32-bit draws captured into the PRNG-stream blob. Long enough that
// the stream's avalanche is exercised, short enough to keep the four-rate corpus
// footprint bounded. (PI).
inline constexpr int kPrngDraws = 256;

// --- Sequencer-byte corpus ---------------------------------------------------------
// (PI) the fixed pitch palette recorded into the StepSequencer for the seq-byte golden:
// a short, deterministic note / rest / tie program exercising all three slot-byte
// flavors (plain note, REST, TIE) so the per-step byte layout is captured exactly. The
// pitches are 6-bit DAC values [0, 63] (the SeqStep pitch field width). (PI).
inline constexpr std::array<int, 8> kSeqPitches = {{ 0, 12, 24, 36, 48, 60, 63, 7 }};

// --- 4013 divider-OR corpus --------------------------------------------------------
// (PI) the number of VCO clock edges (saw wraps) clocked through the 4013 dual flip-flop
// divider for the divider-OR edge-index golden. Each edge toggles Q1; Q2 toggles on Q1's
// rising edge; the OR output (the -2 oct 25% pulse) is Q1 || Q2 [docs/design/01 §5.3,
// §5.4]. >= 8 covers two full -2-oct periods so the 75%/25% duty pattern is captured.
// (PI).
inline constexpr int kDividerEdges = 64;

// --- Arp-ordering corpus -----------------------------------------------------------
// (PI) the held-key set (32-key bitmap key indices) and the number of clock edges
// advanced for the arp step-ordering golden. A four-note chord over enough edges that
// every UP / U&D / DOWN traversal completes at least one full cycle, so the ordering is
// captured exactly. (PI).
inline constexpr std::array<int, 4> kArpHeldKeys = {{ 0, 4, 7, 12 }};
inline constexpr int kArpEdges = 16;

// --- Param-smoothing-boundary corpus -----------------------------------------------
// (PI) the param-smoothing block-boundary golden captures the control-rate TICK at which
// the one-pole smoother first SNAPS to a stepped target (the integer block-boundary
// index), NOT the FP de-zipper trajectory [docs/design/11 §5.1 — "parameter-smoothing
// block boundaries"; docs/design/00 §4.4 control-rate chunk-boundary tick]. The target
// step, the per-class time constant, the tick rate and the tick count below are (PI).
inline constexpr int    kSmoothTicks         = 64;     // (PI) — control-rate ticks captured
inline constexpr double kSmoothTickRateHz    = 1500.0; // (PI) — ~one tick per kRenderBlock @ 48k
inline constexpr double kSmoothTimeConstantS = 0.0015; // (PI) — a fast de-zipper TC (~1.5 ms),
                                                       // chosen so the smoother SNAPS to the
                                                       // step well within kSmoothTicks, giving
                                                       // the integer boundary a captured 1->0
                                                       // transition (not stuck-on past the end).
inline constexpr double kSmoothTarget        = 1.0;    // (PI) — normalized step target

// --- Shared blob length ------------------------------------------------------------
// (PI) the canonical per-rate render length, in f32 samples, every CLASS-EXACT blob is
// padded/truncated to so the four-rate corpus has a uniform footprint and the keys are
// directly comparable. Each corpus writes its integer/control payload into the leading
// frames and zero-pads the remainder. (PI).
inline constexpr int kBlobFrames = 512;

} // namespace mw::cal::golden::classexact
