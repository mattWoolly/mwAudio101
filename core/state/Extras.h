// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/state/Extras.h — the trivially-copyable <extras> POD payload (task 017).
// Realizes docs/design/06 §5.4 and ADR-025 (per-step accent REMOVED in v1).
//
// This is the fixed-capacity, no-heap payload handed to the audio thread via a
// pre-allocated lock-free SPSC double-buffer swap [ADR-008 C19-C20; ADR-021 L7].

#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace mw::state {

// One sequencer step. POD, no heap. Step state is note/rest/tie/gate ONLY —
// the SH-101 sequencer has NO per-step accent in v1 [ADR-025; ADR-007].
struct SeqStep {
    std::int8_t noteSemitone = 0;  // relative to base; clamped on load
    bool        gate = false;      // gate on/off
    bool        tie  = false;      // legato/slur tie [research/11 §4.7 — labelled gap]
    bool        rest = false;      // rest step       [research/11 §4.7 — labelled gap]
    // NO accent field [ADR-025].
};

// Fixed capacity; a stored pattern shorter/longer is padded/clamped without
// allocation [docs/design/06 §5.4; ADR-008 C20; research/11 §4.7].
inline constexpr int kMaxSeqSteps = 100;

// The audio-thread handoff payload. Trivially copyable, no heap, no locks.
struct Extras {
    std::array<SeqStep, kMaxSeqSteps> steps{};
    int         stepCount  = 0;       // 0..100 active steps
    bool        arpLatch   = false;
    std::int64_t driftSeed = 0;
    bool        seedLocked = false;
};

// --- Compile-time guards (docs/design/06 §5.4; ADR-025) -----------------------
static_assert(std::is_trivially_copyable_v<SeqStep>,
              "SeqStep MUST be trivially copyable (audio-thread POD handoff) [§5.4].");
static_assert(std::is_trivially_copyable_v<Extras>,
              "Extras MUST be trivially copyable (lock-free SPSC swap) [§5.4].");
static_assert(kMaxSeqSteps == 100,
              "kMaxSeqSteps MUST be 100 (fixed-capacity sequence) [§5.4; ADR-008 C20].");
// SeqStep carries exactly note/gate/tie/rest; the no-accent rule is asserted by the
// statetree* unit test (compile-time absence of a member is checked there).

} // namespace mw::state
