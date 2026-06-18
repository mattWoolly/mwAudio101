// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/TriggerSource.h — the S7 trigger-source switch (task 083).
//
// Realizes docs/design/05 §4.1–§4.4 verbatim: S7 is ONE selector binding both note
// priority and envelope retrigger — never two independent params [ADR-007 §Decision 2,
// C4–C6; research/07 §3.2–§3.3]. It resolves a 32-key held bitmap into a monophonic
// TriggerDecision (selectedKey, gate, retrigger, legato).
//
//   S7 position | Note priority | Envelope retrigger
//   GateTrig    | last-note     | retrigger on every new key/step          (C4)
//   Gate        | lowest-note   | no legato retrigger; one sustained gate   (C5)
//   Lfo         | lowest-note   | envelope re-fired each LFO/clock cycle     (C6)
//
// GATE-mode priority is LOWEST-note, not high-note — this corrects a forum misreading
// and must not regress [research/07 §3.2, §8.2; research/06 §5].
//
// RT invariants [docs/design/05 §4.4; ADR-007 C26]: resolve() is `const noexcept`,
// branch-on-mode only, no heap allocation, no lock. The held bitmap is a fixed
// uint32_t. Last-pressed tracking uses a fixed std::array<int8_t, 32> stamped with a
// monotonic press counter — pre-sized in prepare(), never resized.
//
// This class is JUCE-free POD/core logic [ADR-001/D2]. The TrigMode / NotePriority /
// KeyState / TriggerDecision PODs are owned by control/ControlTypes.h (task 081);
// this header consumes them. Parameter IDs/ranges are owned by docs/design/06 §2
// [ADR-008]; the live trigger-source ID is mw101.key.trigger_priority (§9.1).
//
// Scope boundary: this resolves the trigger decision ONLY. ADSR firing and VCA gating
// are consumers; voice/poly/unison selection is the voice stream; clock-edge
// generation is the Clock — consumed here as the `lfoEdge` bool.

#pragma once

#include <array>
#include <cstdint>

#include "control/ControlTypes.h"

namespace mw::control {

// S7 trigger-source switch: coupled note-priority + envelope-retrigger resolver.
// Operates over a 32-key held bitmap; emits a monophonic TriggerDecision per tick.
class TriggerSource {
public:
    // Number of keys in the held bitmap (one uint32_t) [docs/design/05 §4.2].
    static constexpr int kNumKeys = 32;

    // Pre-size / clear the fixed last-pressed array and reset the press counter and
    // edge bookkeeping. Off the audio thread; never resizes thereafter [§4.4].
    void prepare() noexcept;

    // Set the S7 mode; this sets BOTH the note-priority rule AND the retrigger rule —
    // they are coupled, never independent [§4.1; ADR-007 §Decision 2].
    void setMode(TrigMode m) noexcept;

    // Note priority derived from the current mode: LastNote for GateTrig, LowestNote
    // for Gate and Lfo [§4.3].
    [[nodiscard]] NotePriority priority() const noexcept;

    [[nodiscard]] TrigMode mode() const noexcept { return mode_; }

    // Observe a keyboard scan: stamp newly-pressed keys with a monotonic press
    // counter (for the LastNote most-recent fallback) and update the held==0
    // transition bookkeeping used by the Gate non-legato retrigger rule. Call once
    // per scan BEFORE resolve(). noexcept, no allocation [§4.3, §4.4].
    void observe(const KeyState& ks) noexcept;

    // Resolve the held bitmap into a monophonic decision for this tick. `lfoEdge` is
    // true on an H->L clock edge and drives the Lfo-mode re-fire. `const noexcept`,
    // no allocation, no lock — branch-on-mode only [§4.3, §4.4].
    [[nodiscard]] TriggerDecision resolve(const KeyState& ks, bool lfoEdge) const noexcept;

private:
    // Select the lowest set bit (lowest key index) of `held`, or -1 if none.
    [[nodiscard]] static int lowestKey(std::uint32_t held) noexcept;

    // Select the still-held key with the highest press stamp (most-recent), or -1 if
    // no key is held. Ties (no stamp) resolve to the lowest held key, deterministically.
    [[nodiscard]] int mostRecentHeldKey(std::uint32_t held) const noexcept;

    TrigMode mode_{TrigMode::GateTrig};

    // Last-pressed press-order stamp per key (0 == never pressed since prepare).
    // Fixed-size, pre-sized in prepare(), never resized [§4.4; ADR-007 C26].
    std::array<std::uint32_t, kNumKeys> pressStamp_{};
    std::uint32_t pressCounter_{0};   // monotonic; incremented per newly-pressed key

    // Held-state bookkeeping for the Gate non-legato retrigger rule (§4.3): true once
    // any key has been held, used to detect the held==0 -> held!=0 transition.
    bool prevAnyHeld_{false};

    // Per-scan flags computed in observe() and read by the const resolve() (which may
    // not mutate). [§4.3]
    bool nonLegatoOnset_{false};  // held was 0 before this scan, now non-zero
    bool anyJustPressed_{false};  // a key was newly pressed in this scan
};

} // namespace mw::control
