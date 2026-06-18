// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/BlockContext.h — the POD processing seam aggregate + views (task 007).
//
// Realizes docs/design/00 §5.2/§5.3 and ADR-001 Decision (processing contract).
// BlockContext is the single value-typed argument to Engine::process. It is a
// non-owning, value-typed aggregate of PODs with NO JUCE type and NO owning
// allocation; the core dereferences borrowed pointers only — it never copies,
// grows, or frees them [ADR-001 C3, C14].

#pragma once

#include <cstdint>

namespace mw {

// Forward-declared: the normalized param POD is owned by the param-schema stream
// (core/params/ParamSnapshot.h, docs/design/06). The seam only holds a pointer to
// an immutable instance snapshotted once per block [docs/design/00 §5.4].
struct ParamSnapshot;

// Non-owning view over the host's channel pointers for this block [§5.3].
struct AudioBlockView {
    float* const* channels;     // borrowed; NOT owned by core
    int           numChannels;
    int           numFrames;
};

// POD transport snapshot for this block (host-decoded; no JUCE types) [§5.3].
struct TransportInfo {
    double bpm;
    double ppqPosition;
    bool   isPlaying;
    double sampleRate;
};

// Event kind (note/CC/MPE). The full enumeration is owned by docs/design/09; this
// is the seam-side placeholder so MidiEvent is a complete POD now [§5.3].
// TODO(task-099/doc-09): replace with the canonical mw::core::NormalizedType set.
enum class NormalizedType : std::uint8_t {
    NoteOn = 0,
    NoteOff,
    PitchBend,
    ChannelPressure,
    PolyPressure,
    ControlChange,
    ProgramChange,
    ClockEdge,
    ParamValue
};

// Host-decoded, sample-offset-timestamped event (note/CC/MPE). POD.
// == mw::core::MidiEvent, whose canonical definition + the HostEvent->MidiEvent
// translation live in docs/design/09 [§5.3].
struct MidiEvent {
    NormalizedType type;          // event kind
    std::int8_t    channel;
    std::int16_t   noteId;
    float          data0;
    float          value;
    int            sampleOffset;  // within the current block
};

// Non-owning span of events for this block, ordered by sampleOffset [§5.3].
struct MidiEventView {
    const MidiEvent* events;
    int              numEvents;
};

// The single argument to Engine::process [ADR-001 Decision]. POD aggregate; no
// owning allocation; the params pointer is immutable, snapshotted once per block.
struct BlockContext {
    AudioBlockView       audio;     // output target (and any aux), borrowed
    const ParamSnapshot* params;    // immutable, snapshotted once per block (§5.4)
    TransportInfo        transport;
    MidiEventView        midi;      // non-owning span; sample-accurate offsets
};

} // namespace mw
