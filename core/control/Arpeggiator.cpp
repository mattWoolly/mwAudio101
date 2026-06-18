// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/Arpeggiator.cpp — implementation of the UP / U&D / DOWN arpeggiator
// over a fixed 32-key held bitmap (task 084). Realizes docs/design/05 §5.1–§5.4.
//
// Stepping walks the SET BITS of the 32-bit held bitmap in ascending key order; UP
// and DOWN choose the direction, U&D oscillates per §5.2. Two bitmaps are tracked: a
// SOUNDING set (`held_`) that retains released keys while HOLD is latched, and the
// physically-down set (`physical_`) used to detect "all keys released into the latch"
// and "a fresh chord that replaces the latched set" [§5.1/C10]. All hot-path work is
// pure integer arithmetic over the fixed bitmaps + cursor — no allocation, no lock
// [docs/design/05 §5.4, §10; ADR-007 C26].

#include "control/Arpeggiator.h"

#include <bit>      // std::popcount, std::countr_zero
#include <cstdint>

namespace mw::control {

void Arpeggiator::prepare() noexcept {
    held_ = 0;
    physical_ = 0;
    mode_ = ArpMode::Up;
    hold_ = false;
    repeatEndpoints_ = mw::cal::arp::kArpUandDRepeatEndpoints;   // §5.2 default
    cursor_ = 0;
    dir_ = +1;
}

void Arpeggiator::setMode(ArpMode m) noexcept {
    mode_ = m;
    // Reset the oscillation cursor so a direction change starts cleanly.
    cursor_ = 0;
    dir_ = +1;
}

void Arpeggiator::setHold(bool latched) noexcept {
    if (hold_ == latched) return;
    hold_ = latched;
    if (!hold_) {
        // HOLD released: the sounding set collapses to whatever is physically held;
        // a purely-latched set (no physical keys) clears [§5.1/C10].
        held_ = physical_;
    }
}

void Arpeggiator::setUandDRepeatEndpoints(bool b) noexcept {
    repeatEndpoints_ = b;
}

void Arpeggiator::noteOn(int key) noexcept {
    if (key < 0 || key > 31) return;

    // A fresh keypress that arrives while a set is purely latched (HOLD on, no
    // physical keys down) starts a NEW set, REPLACING the latched one [§5.1/C10].
    if (hold_ && physical_ == 0 && held_ != 0) {
        held_ = 0;
        cursor_ = 0;
        dir_ = +1;
    }
    const std::uint32_t bit = (std::uint32_t{1} << key);
    physical_ |= bit;
    held_ |= bit;
}

void Arpeggiator::noteOff(int key) noexcept {
    if (key < 0 || key > 31) return;

    const std::uint32_t bit = (std::uint32_t{1} << key);
    physical_ &= ~bit;
    // Under HOLD the sounding set retains the released key (the latch survives the
    // release) [§5.1]; otherwise the key stops sounding immediately.
    if (!hold_) {
        held_ &= ~bit;
    }
}

bool Arpeggiator::isEngaged() const noexcept {
    // Chord/legato => two or more keys held; or HOLD latched with a non-empty set.
    if (hold_ && held_ != 0) return true;
    return std::popcount(held_) >= 2;
}

int Arpeggiator::heldCount() const noexcept {
    return std::popcount(held_);
}

int Arpeggiator::keyAtOrderedIndex(int n) const noexcept {
    // Return the key index of the n-th (0-based) set bit, scanning ascending.
    std::uint32_t bits = held_;
    int seen = 0;
    while (bits != 0) {
        const int k = std::countr_zero(bits);
        if (seen == n) return k;
        bits &= (bits - 1);   // clear the lowest set bit
        ++seen;
    }
    return -1;
}

int Arpeggiator::advanceOnEdge() noexcept {
    const int n = std::popcount(held_);
    if (n == 0) return -1;

    // Clamp the cursor in case the held set shrank since the last edge.
    if (cursor_ >= n) {
        cursor_ = 0;
        dir_ = +1;
    }
    if (cursor_ < 0) cursor_ = 0;

    switch (mode_) {
        case ArpMode::Up: {
            const int out = cursor_;
            cursor_ = (cursor_ + 1) % n;   // ascending, wrap
            return keyAtOrderedIndex(out);
        }
        case ArpMode::Down: {
            // Emit top-to-bottom: ordered index counts down from the top, then wraps.
            const int out = (n - 1) - cursor_;
            cursor_ = (cursor_ + 1) % n;
            return keyAtOrderedIndex(out);
        }
        case ArpMode::UandD: {
            const int out = cursor_;
            // Advance the cursor with bounce, honoring the endpoint-repeat policy.
            if (n == 1) {
                cursor_ = 0;
                dir_ = +1;
            } else if (repeatEndpoints_) {
                // Repeat the endpoint: on hitting a boundary, flip direction and STAY
                // once so the boundary note sounds twice (1 2 3 4 4 3 2 1 ...) [§5.2].
                const int next = cursor_ + dir_;
                if (next > n - 1 || next < 0) {
                    dir_ = -dir_;        // turn around; cursor_ stays => endpoint repeats
                } else {
                    cursor_ = next;
                }
            } else {
                // Do NOT repeat the endpoint: reflect past the boundary
                // (1 2 3 4 3 2 ...) [§5.2].
                int next = cursor_ + dir_;
                if (next > n - 1) {
                    dir_ = -1;
                    next = n - 2;
                } else if (next < 0) {
                    dir_ = +1;
                    next = 1;
                }
                cursor_ = next;
            }
            return keyAtOrderedIndex(out);
        }
    }
    return -1;   // unreachable; all ArpMode values handled above
}

} // namespace mw::control
