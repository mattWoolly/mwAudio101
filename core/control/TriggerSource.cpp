// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/TriggerSource.cpp — S7 trigger-source switch implementation (task 083).
//
// Realizes docs/design/05 §4.3 resolution rules (normative). See TriggerSource.h for
// the contract and RT invariants [§4.1–§4.4; ADR-007 C4–C6, C26].

#include "control/TriggerSource.h"

namespace mw::control {

void TriggerSource::prepare() noexcept {
    pressStamp_.fill(0u);     // fixed array, pre-sized; cleared, never resized [§4.4]
    pressCounter_ = 0u;
    prevAnyHeld_ = false;
    nonLegatoOnset_ = false;
    anyJustPressed_ = false;
}

void TriggerSource::setMode(TrigMode m) noexcept {
    // Sets BOTH priority and retrigger rule — they are coupled [§4.1].
    mode_ = m;
}

NotePriority TriggerSource::priority() const noexcept {
    // LastNote for GateTrig; LowestNote for Gate and Lfo [§4.3].
    return (mode_ == TrigMode::GateTrig) ? NotePriority::LastNote
                                         : NotePriority::LowestNote;
}

void TriggerSource::observe(const KeyState& ks) noexcept {
    // Stamp every newly-pressed key with a monotonic press counter so the LastNote
    // most-recent-still-held fallback can pick the latest press [§4.3].
    anyJustPressed_ = (ks.justPressed != 0u);
    std::uint32_t pressed = ks.justPressed;
    while (pressed != 0u) {
        const int key = __builtin_ctz(pressed);  // lowest set bit index
        pressed &= pressed - 1u;                  // clear it
        if (key < kNumKeys) {
            pressStamp_[static_cast<std::size_t>(key)] = ++pressCounter_;
        }
    }

    // Clear stamps for keys that are no longer held so a future re-press is "newer"
    // than any stale stamp and the most-recent fallback never favors a released key.
    std::uint32_t released = ks.justReleased;
    while (released != 0u) {
        const int key = __builtin_ctz(released);
        released &= released - 1u;
        if (key < kNumKeys) {
            pressStamp_[static_cast<std::size_t>(key)] = 0u;
        }
    }

    // Gate non-legato retrigger: fire only on a held==0 -> held!=0 transition [§4.3].
    const bool anyHeld = (ks.held != 0u);
    nonLegatoOnset_ = anyHeld && !prevAnyHeld_;
    prevAnyHeld_ = anyHeld;
}

int TriggerSource::lowestKey(std::uint32_t held) noexcept {
    if (held == 0u) {
        return -1;
    }
    return __builtin_ctz(held);   // lowest set bit == lowest key index [§4.3]
}

int TriggerSource::mostRecentHeldKey(std::uint32_t held) const noexcept {
    if (held == 0u) {
        return -1;
    }
    int best = -1;
    std::uint32_t bestStamp = 0u;
    std::uint32_t bits = held;
    while (bits != 0u) {
        const int key = __builtin_ctz(bits);
        bits &= bits - 1u;
        const std::uint32_t stamp =
            (key < kNumKeys) ? pressStamp_[static_cast<std::size_t>(key)] : 0u;
        // Strictly-greater keeps the FIRST-seen (lowest) key on a tie, since the bit
        // scan ascends; this makes the no-stamp fallback deterministically the lowest
        // held key [§4.4 determinism].
        if (best < 0 || stamp > bestStamp) {
            best = key;
            bestStamp = stamp;
        }
    }
    return best;
}

TriggerDecision TriggerSource::resolve(const KeyState& ks, bool lfoEdge) const noexcept {
    TriggerDecision d{};

    const bool anyHeld = (ks.held != 0u);

    // A keypress is legato when a key was already held before this scan's new press,
    // i.e. there is more than one held key OR a held key plus a fresh press [§4.2].
    // Concretely: a justPressed exists AND another key was already down.
    const std::uint32_t alreadyHeld = ks.held & ~ks.justPressed;
    d.legato = anyHeld && (alreadyHeld != 0u) && anyJustPressed_;

    switch (mode_) {
        case TrigMode::GateTrig: {
            // Last-note priority: pick the lowest of the keys in justPressed, else the
            // most-recent still-held key [§4.3].
            if (anyJustPressed_) {
                d.selectedKey = lowestKey(ks.justPressed);
            } else {
                d.selectedKey = mostRecentHeldKey(ks.held);
            }
            d.gateOn = anyHeld;
            // Retrigger on every new key (and on every seq/arp step — driven by the
            // caller via a synthesized justPressed). [§4.3 / C4]
            d.retrigger = anyHeld && anyJustPressed_;
            break;
        }
        case TrigMode::Gate: {
            // Lowest-note priority; one sustained gate. [§4.3 / C5]
            d.selectedKey = lowestKey(ks.held);
            d.gateOn = anyHeld;
            // Retrigger only on a held==0 -> held!=0 transition; a legato keypress does
            // NOT retrigger. [§4.3 / C5]
            d.retrigger = nonLegatoOnset_;
            break;
        }
        case TrigMode::Lfo: {
            // Lowest-note priority; gate asserted while a key is held; envelope
            // re-fires on each lfoEdge while held. [§4.3 / C6]
            d.selectedKey = lowestKey(ks.held);
            d.gateOn = anyHeld;
            d.retrigger = anyHeld && lfoEdge;
            break;
        }
    }

    return d;
}

} // namespace mw::control
