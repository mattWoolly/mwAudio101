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

#include <algorithm>   // std::clamp — velocity ledger clamp only (task 162b)

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
    pressSerial_.fill(0u);   // 0 == "never pressed"
    keyVelocity_.fill(1.0f); // neutral velocity for any key never pressed (task 162b)
    nextSerial_      = 1u;
    lastActive_      = -1;
    gateWasAsserted_ = false;
    newKeyThisTick_  = false;
}

void KeyAssigner::noteOn(int midiNote, float velocity) noexcept {
    if (!inRange(midiNote)) {
        return;
    }
    const auto i = static_cast<std::size_t>(midiNote);
    // Stamp a FRESH press with an increasing serial so "most-recently-pressed
    // still-held" is well-defined; a re-press of an already-held key does NOT
    // re-stamp (mirrors the task-152 oracle) (§5.3). Stamp BEFORE setting the bit.
    if (!held_.test(i)) {
        pressSerial_[i] = nextSerial_++;
    }
    // Record THIS key's velocity (task 162b velocity-ingress): EVERY press updates it
    // (a re-press carries its new velocity), purely a value carrier — it does not touch
    // the priority/retrigger scan. Clamped to [0,1]. resolve() reads it for the winning
    // note so the VoiceManager hands the Voice the real per-note velocity (not 1.0).
    keyVelocity_[i] = std::clamp(velocity, 0.0f, 1.0f);
    held_.set(i);
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
            //    fall back to the most-recently-pressed still-held key (§5.3).
            if (hasNewDown) {
                active = lowestOf(changedDown);
            } else if (lastActive_ >= 0 && held_.test(static_cast<std::size_t>(lastActive_))) {
                active = lastActive_;
            } else {
                // Active key released with no new down: fall back to the
                // most-recently-pressed still-held key, NOT the lowest-held — the
                // NORMATIVE §5.3 / §5.4 K3 last-note rule (mirrors the oracle).
                active = mostRecentHeld();
            }
            break;
    }

    d.activeNote = active;

    // --- Velocity of the WINNING note (task 162b velocity-ingress) --------
    // Emit the per-note velocity recorded for the resolved active note so the
    // VoiceManager hands the Voice the REAL velocity instead of 1.0. MONO/UNISON drive
    // the ONE decision, so this is exactly "the active note's velocity"; under GateTrig a
    // just-pressed key both wins priority AND is the retrigger note, so a retrigger
    // naturally uses the new note's velocity (§5.4 K3/K4). With no note held (gate off)
    // keep neutral 1.0 — a gate-off tick must not perturb the velocity routing.
    d.velocity = (active >= 0)
                     ? keyVelocity_[static_cast<std::size_t>(active)]
                     : 1.0f;

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

int KeyAssigner::mostRecentHeld() const noexcept {
    int best = -1;
    std::uint32_t bestSerial = 0u;
    for (int n = 0; n < 128; ++n) {
        const auto i = static_cast<std::size_t>(n);
        if (held_.test(i) && pressSerial_[i] >= bestSerial) {
            bestSerial = pressSerial_[i];
            best       = n;
        }
    }
    return best;
}

} // namespace mw
