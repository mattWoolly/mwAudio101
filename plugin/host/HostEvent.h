// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/host/HostEvent.h — plugin-side HostEvent POD + fixed-capacity lock-free
// NormalizedEventBuffer (task 099). Realizes docs/design/09 §3.2 and ADR-011 C9 /
// ADR-024 C7.
//
// Pure C++ (no JUCE), so it compiles and unit-tests in the headless mwcore test
// binary. prepare() is the SOLE allocator; push() drops-never-grows and never
// allocates on the audio thread; capacity is sized
// maxEvents = kEventBufferBlockFactor*maxBlockSize + kEventBufferSlack, with the
// (PI) factors centralized in core/calibration/Calibration.h.

#pragma once

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "../../core/calibration/Calibration.h"

namespace mw::plugin {

enum class HostEventType : std::uint8_t {
    NoteOn = 0, NoteOff, PitchBend, ChannelPressure, PolyPressure,
    ControlChange, ProgramChange, ClockEdge, ParamValue
};

// POD; trivially copyable [docs/design/09 §3.2].
struct HostEvent {
    HostEventType type;
    std::uint8_t  channel;       // 1..16 (MPE member or global)
    std::int32_t  sampleOffset;  // sub-block sample position (>= 0)
    std::int32_t  data0;         // note number / CC number / param index
    float         value;         // normalized value or signed offset
    std::int32_t  noteId;        // CLAP note id; -1 if none / MIDI-derived
};

static_assert(std::is_trivially_copyable_v<HostEvent>,
              "HostEvent MUST be a trivially copyable POD [docs/design/09 §3.2].");

// Compute the fixed capacity for a given max block size [docs/design/09 §3.2].
inline constexpr int eventBufferCapacityFor(int maxBlockSize) noexcept {
    return mw::cal::host::kEventBufferBlockFactor * maxBlockSize
         + mw::cal::host::kEventBufferSlack;
}

// Pre-sized; no alloc on the audio thread. prepare() allocates once;
// push()/clear()/iteration are allocation-free [ADR-011 C9].
class NormalizedEventBuffer {
public:
    // Allocate to the §3.2 capacity for maxBlockSize. Off the audio thread.
    void prepare(int maxBlockSize) noexcept {
        const int cap = eventBufferCapacityFor(maxBlockSize);
        storage_.assign(static_cast<std::size_t>(cap), HostEvent{});
        count_ = 0;
    }

    // Append an event. Returns false if full (drop, NEVER grow). Never allocates.
    bool push(const HostEvent& e) noexcept {
        if (count_ >= static_cast<int>(storage_.size())) {
            // Overflow: drop and assert in debug; the lowest-priority drop policy
            // (ParamValue before NoteOn/Off) is applied by the drain layer (task 113).
            assert(false && "NormalizedEventBuffer overflow — event dropped [ADR-011 C9]");
            return false;
        }
        storage_[static_cast<std::size_t>(count_)] = e;
        ++count_;
        return true;
    }

    void clear() noexcept { count_ = 0; }

    [[nodiscard]] const HostEvent* begin() const noexcept { return storage_.data(); }
    [[nodiscard]] const HostEvent* end()   const noexcept { return storage_.data() + count_; }
    [[nodiscard]] int  size()     const noexcept { return count_; }
    [[nodiscard]] int  capacity() const noexcept { return static_cast<int>(storage_.size()); }

private:
    std::vector<HostEvent> storage_;   // capacity fixed in prepare()
    int                    count_ = 0;
};

} // namespace mw::plugin
