// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/KeyAssigner.cpp — implementation of the bit-faithful note-priority /
// retrigger state machine (task 069). See KeyAssigner.h and
// docs/design/04-voice-and-control.md §5.1-§5.4 for the contract.
//
// resolve() implements the §5.4 NORMATIVE K1-K7 table verbatim:
//   - Lowest-note priority (Gate, Lfo): low->high scan, first held wins
//     [§5.3; research/07 §3.2 "as soon as we find a key down, we are done"].
//   - Last-note priority (GateTrig): changedDown = held_ & ~prevScan_; pick the
//     lowest of the just-pressed; else keep the most-recent still-held
//     [§5.3, K4; research/07 §3.2].
//   - Retrigger and clockReset are emitted per the §5.4 table; they are functions
//     of mode + event, NEVER independent parameters [§5.1; ADR-006 C].

#include "KeyAssigner.h"

namespace mw {

namespace {

// Lowest-note scan: banks low->high, key low->high; the first held key wins.
// Bounded O(128), no allocation [docs/design/04 §5.3; research/07 §3.2].
inline int lowestHeld(const std::bitset<128>& set) noexcept {
    for (int n = 0; n < 128; ++n) {
        if (set.test(static_cast<std::size_t>(n))) {
            return n;
        }
    }
    return -1;
}

// Lowest set bit of `set`, or -1 if empty. Used for the last-note XOR
// (lowest-of-just-pressed) [docs/design/04 §5.3, K4; research/07 §3.2].
inline int lowestOf(const std::bitset<128>& set) noexcept {
    return lowestHeld(set);
}

inline bool inRange(int midiNote) noexcept {
    return midiNote >= 0 && midiNote < 128;
}

} // namespace

void KeyAssigner::prepare() noexcept {
    reset();
}

void KeyAssigner::reset() noexcept {
    held_.reset();
    prevScan_.reset();
    lastActive_      = -1;
    gateWasAsserted_ = false;
    newKeyThisTick_  = false;
}

void KeyAssigner::noteOn(int midiNote) noexcept {
    if (!inRange(midiNote)) {
        return;
    }
    held_.set(static_cast<std::size_t>(midiNote));
    // A keypress this tick re-phases the clock when in Lfo/Arp (§5.4 K6). Flagged
    // here so a multi-down tick still asserts CLOCK RESET exactly once.
    newKeyThisTick_ = true;
}

void KeyAssigner::noteOff(int midiNote) noexcept {
    if (!inRange(midiNote)) {
        return;
    }
    held_.reset(static_cast<std::size_t>(midiNote));
}

NoteDecision KeyAssigner::resolve() noexcept {
    NoteDecision d;

    const bool anyDown = held_.any();
    d.gate = anyDown;

    // changedDown = keys held now that were NOT in the prior scan: the firmware
    // XOR of newly-changed-down keys [docs/design/04 §5.3; research/07 §3.2].
    const std::bitset<128> changedDown = held_ & ~prevScan_;
    const bool hasNewDown = changedDown.any();

    int active = -1;

    switch (mode_) {
        case GateTrigMode::Gate:
        case GateTrigMode::Lfo:
            // Lowest-note priority: first held key wins (§5.4 K1/K2/K5).
            active = lowestHeld(held_);
            break;

        case GateTrigMode::GateTrig:
            // Last-note priority (§5.4 K3/K4):
            //  - if any new key(s) went down this tick, pick the LOWEST of the
            //    just-pressed (XOR changed-down vs prior scan);
            //  - otherwise keep the most-recent still-held key; if it was released,
            //    fall back deterministically to the lowest still-held key.
            if (hasNewDown) {
                active = lowestOf(changedDown);
            } else if (lastActive_ >= 0 && held_.test(static_cast<std::size_t>(lastActive_))) {
                active = lastActive_;
            } else {
                active = lowestHeld(held_);
            }
            break;
    }

    d.activeNote = active;

    // --- Retrigger (§5.4 NORMATIVE table) ---------------------------------
    // Gate (K1/K2):    retrigger ONLY on the gate's leading edge (silence -> held);
    //                  legato new keys keep the single held gate, no retrigger.
    // GateTrig (K3/K4): retrigger on the gate edge AND on every new just-pressed
    //                  key (exactly once per multi-down tick).
    // Lfo (K5):        NEVER key-retriggered; the ADSR is fired by the clock H->L
    //                  edge, owned downstream — the key only sets pitch/clockReset.
    const bool gateEdge = anyDown && !gateWasAsserted_;
    switch (mode_) {
        case GateTrigMode::Gate:
            d.retrigger = gateEdge;
            break;
        case GateTrigMode::GateTrig:
            d.retrigger = gateEdge || (anyDown && hasNewDown);
            break;
        case GateTrigMode::Lfo:
            d.retrigger = false;
            break;
    }

    // --- CLOCK RESET (§5.4 K6) --------------------------------------------
    // Asserted on any new keypress while in Lfo mode (and, via the arp boundary
    // §9, while ARP is active — not owned here) [research/07 §5.2].
    d.clockReset = (mode_ == GateTrigMode::Lfo) && newKeyThisTick_ && anyDown;

    // --- Snapshot for the next tick (§5.3) --------------------------------
    prevScan_        = held_;
    lastActive_      = active;
    gateWasAsserted_ = anyDown;
    newKeyThisTick_  = false;

    return d;
}

bool KeyAssigner::anyHeld() const noexcept {
    return held_.any();
}

} // namespace mw
