// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/state/SeqPatternHandoff.h — the RT-safe message-thread -> audio-thread
// handoff for the editable 100-step <extras> sequencer pattern (task 111c).
//
// Realizes docs/design/10-ui.md §9.3 (seq pattern editing), docs/design/06 §5.4
// (the <extras> SeqStep[kMaxSeqSteps] POD), docs/design/00 §5.4 (the audio thread
// only ADOPTS a published POD — it never parses or allocates) and ADR-008 C19-C20 /
// ADR-021 L7 (the pre-allocated lock-free double-buffer swap).
//
// The editor edits the pattern on the MESSAGE thread (read the current pattern via
// pattern(), write an edited one via publish()). publish() copies the edited POD into
// the inactive double-buffer slot and stores the new pointer with RELEASE ordering.
// The AUDIO thread calls adopt() once per block: a single ACQUIRE atomic-pointer load
// that returns the latest published pattern by value — NO parse, NO heap allocation,
// NO lock [docs/design/00 §5.4; ADR-008 C19; ADR-021 L7].
//
// This is the SAME lock-free double-buffer scheme the SequencerEngine uses for its
// control snapshot (core/control/SequencerEngine.h §9.2): two storage slots owned by
// the message thread, an atomically-published pointer the audio thread reads. Both
// slots are members (allocated once, by construction, off the audio thread); no member
// ever re-allocates.
//
// JUCE-free: this header consumes only mw::state::Extras (a trivially-copyable mwcore
// POD). It lives plugin-side purely because it is wired into the JUCE PluginProcessor;
// it is exercised by the JUCE-linked plugin test target.

#pragma once

#include <atomic>

#include "state/Extras.h"   // mwcore (JUCE-free): mw::state::Extras / SeqStep / kMaxSeqSteps

namespace mw::plugin::state {

// A lock-free single-producer (message thread) / single-consumer (audio thread)
// double-buffer for the editable sequencer pattern POD. No method allocates after
// construction; publish()/adopt() take no lock and never block [ADR-021 L7].
class SeqPatternHandoff {
public:
    SeqPatternHandoff() noexcept
    {
        // Seed both slots to INIT defaults and publish slot 0 so adopt() never sees a
        // null pointer (mirrors SequencerEngine's prepare() seeding). Constructed off
        // the audio thread.
        live_.store(&slots_[0], std::memory_order_release);
    }

    // --- Message thread -------------------------------------------------------

    // Publish an edited pattern: copy the POD into the inactive slot and atomically
    // swap the live pointer with RELEASE ordering. The audio thread's next adopt()
    // observes the new pattern. A trivial byte copy; no allocation, no lock.
    void publish(const mw::state::Extras& edited) noexcept
    {
        const int next = 1 - activeSlot_;       // the slot the audio thread is NOT reading
        slots_[next] = edited;                   // trivial POD copy (no heap)
        live_.store(&slots_[next], std::memory_order_release);
        activeSlot_ = next;
    }

    // The pattern most recently published from the message thread (for the editor to
    // read + re-edit). Message-thread only; returns a copy of the live POD.
    [[nodiscard]] mw::state::Extras pattern() const noexcept
    {
        return slots_[activeSlot_];
    }

    // --- Audio thread ---------------------------------------------------------

    // Adopt the latest published pattern by value: ONE ACQUIRE atomic-pointer load +
    // a trivial byte copy of the POD. The audio thread NEVER parses a tree, allocates,
    // or locks [docs/design/00 §5.4; ADR-008 C19]. Never null after construction.
    [[nodiscard]] mw::state::Extras adopt() const noexcept
    {
        return *live_.load(std::memory_order_acquire);
    }

private:
    mw::state::Extras slots_[2]{};
    int activeSlot_ = 0;   // index of the slot most recently published as live
    std::atomic<const mw::state::Extras*> live_{ nullptr };
};

} // namespace mw::plugin::state
