// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/midi/CcLearnMap.h — the double-buffered, single-writer, atomic-swap CC/learn
// map (task 100). Realizes docs/design/09 §6.3 verbatim and ADR-012 C16.
//
// CONCURRENCY MODEL (single-writer / multi-reader, lock-free):
//   - MESSAGE THREAD (single writer): edit a private inactive copy via editableCopy(),
//     then publish() to atomically store the live pointer. No mutex, no allocation.
//   - AUDIO THREAD (reader): lookup(cc) reads the current live pointer once
//     (acquire) and does a branch-free array index. It never locks, never allocates,
//     and never observes a half-edited buffer because the writer only ever mutates
//     the buffer the audio thread is NOT reading, then swaps the pointer in one
//     atomic store [docs/design/09 §6.3; ADR-012 C16].
//
// The map holds two fixed std::array<CcBinding,128> buffers (A/B) and an atomic
// pointer that always names one of them as "live". editableCopy() returns the OTHER
// (inactive) buffer, pre-seeded with the live buffer's current contents so an
// unedited publish() is a faithful no-op; publish() flips live_ to that buffer. The
// next editableCopy() returns the now-inactive buffer (ping-pong). All storage is
// owned inline — no heap, sized once at construction.
//
// SEED: the default-constructed map is seeded with the §6.2 CC table
// (CC1/7/11/74/71/5 -> doc-06 parameter indices; CC64 -> HOLD sentinel) so a fresh
// instance behaves per the documented default before any MIDI-learn edit
// [docs/design/09 §6.2; ADR-012 C15, C20].
//
// WHY plugin/ AND NOT core/: this type is the wrapper-side MIDI surface
// [docs/design/09 §1.1, §6.3]. It is JUCE-free, but conceptually belongs to mwplugin
// and (with MidiFrontEnd, task 104) it resolves raw CC numbers to param indices
// before the engine ever sees them.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace mw::plugin {

// POD binding row [docs/design/09 §6.3]. One entry per CC number (0..127).
struct CcBinding {
    std::uint8_t ccNumber{ 0 };       // 0..127 (the row's own CC number)
    std::int32_t paramIndex{ -1 };    // index into the doc-06 kParamDefs registry; -1 unmapped
    bool         enabled{ false };
};

static_assert(std::is_trivially_copyable_v<CcBinding>,
              "CcBinding MUST be a trivially copyable POD [docs/design/09 §6.3].");

class CcLearnMap {
public:
    static constexpr int kNumCc = 128;            // CC 0..127

    // Sentinel returned by lookup() for an unmapped / disabled CC [docs/design/09 §6.3].
    static constexpr std::int32_t kUnmapped = -1;

    // Construct double-buffered, seed buffer A with the §6.2 default map, and publish
    // A as live. Off the audio thread (construction is a prepare-time event).
    CcLearnMap() noexcept;

    CcLearnMap(const CcLearnMap&) = delete;
    CcLearnMap& operator=(const CcLearnMap&) = delete;

    // --- MESSAGE THREAD (single writer) ---------------------------------------
    // Return the INACTIVE buffer, pre-seeded with a copy of the current live buffer,
    // for the caller to edit in place. The audio thread never reads this buffer, so
    // the edit cannot race a concurrent lookup(). No allocation, no lock.
    [[nodiscard]] CcBinding* editableCopy() noexcept;

    // Atomically store the (previously editableCopy()'d) inactive buffer as the new
    // live pointer. A single release store — lock-free, allocation-free. After this,
    // the audio thread's next lookup() observes the edited map.
    void publish() noexcept;

    // --- AUDIO THREAD (reader) ------------------------------------------------
    // Branch-free param-index lookup for a CC number. Returns the bound param index, or
    // kUnmapped (-1) if the CC is out of range / disabled / unmapped. Reads the live
    // pointer once (acquire) then a single array index multiplied by the enabled flag.
    // No lock, no allocation [docs/design/09 §6.3; ADR-012 C16].
    [[nodiscard]] std::int32_t lookup(std::uint8_t ccNumber) const noexcept;

    // --- Test / introspection helpers -----------------------------------------
    // The buffer the audio thread currently reads (for tests asserting the swap does
    // not mutate the live buffer). Reads the live pointer (acquire).
    [[nodiscard]] const CcBinding* liveBuffer() const noexcept;

    // True iff the live atomic pointer is always lock-free on this platform (it is on
    // every supported target; pointer atomics are lock-free). Asserts the lock-free
    // publish contract [ADR-012 C16].
    [[nodiscard]] static bool liveIsAlwaysLockFree() noexcept {
        return std::atomic<const std::array<CcBinding, kNumCc>*>::is_always_lock_free;
    }

private:
    using Buffer = std::array<CcBinding, kNumCc>;

    // The inactive buffer (the one live_ does NOT point at).
    [[nodiscard]] Buffer* inactiveBuffer() noexcept;

    std::array<Buffer, 2> buffers_{};                  // A == buffers_[0], B == buffers_[1]
    std::atomic<const Buffer*> live_{ nullptr };       // points at one of buffers_[0/1]
};

} // namespace mw::plugin
