// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/midi/EventTranslator.h — the field-for-field, allocation-free
// mw::plugin::HostEvent -> mw::MidiEvent translation (task 101). Realizes
// docs/design/09 §3.3 verbatim and ADR-001 C11 / ADR-011 C11 (cross-format
// determinism: identical HostEvent input yields identical core MidiEvent output, so
// the macOS arm64 bit-exact bless reference holds across all five wrappers).
//
// WHAT THIS IS. The plugin shell is the ONLY place that touches both the JUCE/format-
// shaped HostEvent (plugin/host/HostEvent.h, channel 1..16, CLAP note id, raw CC
// numbers) AND the JUCE-free core MidiEvent the engine ingests via BlockContext.midi
// (core/BlockContext.h). This translator performs that single boundary crossing: it
// erases format-specific shape so all wrappers emit IDENTICAL core event streams for
// identical input [docs/design/09 §3.3; ADR-011 C11].
//
// THE §3.3 FIELD MAP (this header implements EXACTLY this table):
//   type (HostEventType)  -> type (mw::NormalizedType): enum remap; ProgramChange is
//        CONSUMED in plugin/ (preset-recall hook) and is NOT forwarded; all other
//        types map 1:1.
//   channel (uint8 1..16) -> channel (int8): narrowing copy; lower-zone master = 1.
//   noteId (int32; CLAP id or -1) -> noteId (int16): -1 preserved for MIDI-derived.
//   data0 (int32)         -> data0 (float): note/CC/param index widened to float;
//        for ControlChange the raw CC number is resolved through the §6 CcLearnMap to
//        a param index BEFORE forwarding (a CC mapped to nothing is dropped).
//   value (float)         -> value (float): copied verbatim.
//   sampleOffset (int32)  -> sampleOffset (int): sub-block offset copied.
//
// REAL-TIME INVARIANTS [docs/design/09 §1.3, §3.3; ADR-011 C9]. translateOne() and
// translateBlock() are noexcept, take no lock, and perform ZERO heap allocation: the
// translator is STATELESS (owns no member, holds no heap), reads the CcLearnMap via
// its lock-free atomic-pointer lookup(), and writes into a caller-owned, pre-sized
// output buffer (NormalizedEventBuffer / mw::MidiEvent span) sized in prepareToPlay.
//
// This type carries NO JUCE dependency; it is pure C++ over the two PODs. It compiles
// into the JUCE-linked plugin target and the headless plugin test binary identically.

#pragma once

#include <cstdint>

#include "../host/HostEvent.h"   // mw::plugin::HostEvent / HostEventType
#include "CcLearnMap.h"          // mw::plugin::CcLearnMap (§6 learn map)

#include "BlockContext.h"        // mw::MidiEvent / mw::NormalizedType (core seam; JUCE-free)

namespace mw::plugin {

// Translate ONE HostEvent into the core mw::MidiEvent per the §3.3 table.
//
// Returns true and fills `out` for every forwarded type. Returns false (and leaves
// `out` unspecified) when the event is NOT forwarded to the engine:
//   * HostEventType::ProgramChange  — consumed in plugin/ (preset recall), §3.3.
//   * HostEventType::ControlChange whose CC number resolves to no param index in the
//     learn map (CcLearnMap::lookup == kUnmapped) — there is no engine target to
//     forward to, so the CC is dropped at the boundary (§3.3: "CC numbers resolved
//     through the §6 learn map to a param index before forwarding").
//
// `ccMap` is read through its lock-free atomic-pointer lookup() (§6.3); it is only
// consulted for ControlChange events. noexcept, lock-free, allocation-free.
[[nodiscard]] bool translateOne(const HostEvent& in,
                                const CcLearnMap& ccMap,
                                mw::MidiEvent& out) noexcept;

// Translate a contiguous block of HostEvents [first, last) into the caller-owned
// pre-sized output array `out` (capacity `outCapacity`, sized in prepareToPlay).
// Forwarded events are written in input order starting at out[0]; ProgramChange and
// unmapped ControlChange are skipped (§3.3). Returns the number of mw::MidiEvents
// written (<= outCapacity). If the output fills, surplus forwarded events are dropped
// (drop-never-grow) — the buffer is NEVER resized [ADR-011 C9]. noexcept, lock-free,
// allocation-free.
[[nodiscard]] int translateBlock(const HostEvent* first,
                                 const HostEvent* last,
                                 const CcLearnMap& ccMap,
                                 mw::MidiEvent* out,
                                 int outCapacity) noexcept;

} // namespace mw::plugin
