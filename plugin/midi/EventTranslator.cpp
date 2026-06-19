// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/midi/EventTranslator.cpp — implementation of the field-for-field, allocation-
// free HostEvent -> mw::MidiEvent translation (task 101). See EventTranslator.h and
// docs/design/09 §3.3 for the normative field map and the ProgramChange-consumed /
// CC-resolution rules.

#include "midi/EventTranslator.h"

namespace mw::plugin {

namespace {

// Enum remap (§3.3 row 1). Every forwarded HostEventType maps 1:1 to its core
// mw::NormalizedType; ProgramChange is signalled separately (consumed, not forwarded)
// by returning false from the caller. A switch (not a numeric cast) makes the mapping
// explicit and total, so a future enumerator must be handled deliberately rather than
// silently aliasing onto the wrong core type [docs/design/09 §3.3].
//
// `forwarded` is set false iff the type is consumed in plugin/ (ProgramChange).
mw::NormalizedType remapType(HostEventType t, bool& forwarded) noexcept {
    forwarded = true;
    switch (t) {
        case HostEventType::NoteOn:          return mw::NormalizedType::NoteOn;
        case HostEventType::NoteOff:         return mw::NormalizedType::NoteOff;
        case HostEventType::PitchBend:       return mw::NormalizedType::PitchBend;
        case HostEventType::ChannelPressure: return mw::NormalizedType::ChannelPressure;
        case HostEventType::PolyPressure:    return mw::NormalizedType::PolyPressure;
        case HostEventType::ControlChange:   return mw::NormalizedType::ControlChange;
        case HostEventType::ClockEdge:       return mw::NormalizedType::ClockEdge;
        case HostEventType::ParamValue:      return mw::NormalizedType::ParamValue;
        case HostEventType::ProgramChange:
            // Consumed in plugin/ (preset-recall hook); NOT forwarded [§3.3].
            forwarded = false;
            return mw::NormalizedType::ControlChange;   // unused — caller drops on !forwarded
    }
    // Unreachable for the closed HostEventType set; treat an unknown value as not
    // forwarded rather than aliasing onto a real engine type.
    forwarded = false;
    return mw::NormalizedType::ControlChange;
}

} // namespace

bool translateOne(const HostEvent& in, const CcLearnMap& ccMap, mw::MidiEvent& out) noexcept {
    bool forwarded = false;
    const mw::NormalizedType coreType = remapType(in.type, forwarded);
    if (! forwarded)
        return false;   // ProgramChange (or unknown): consumed in plugin/, not forwarded.

    // data0 widening with the §3.3 ControlChange special case: a raw CC number is
    // resolved through the §6 learn map to a param index BEFORE forwarding. An
    // unmapped/disabled CC has no engine target, so it is dropped at the boundary.
    float data0 = static_cast<float>(in.data0);
    if (in.type == HostEventType::ControlChange) {
        // in.data0 carries the raw CC number (0..127). Clamp into the map's CC domain
        // before the lock-free lookup() (uint8 index) — out-of-range CC -> unmapped.
        if (in.data0 < 0 || in.data0 > 127)
            return false;
        const std::int32_t paramIndex =
            ccMap.lookup(static_cast<std::uint8_t>(in.data0));
        if (paramIndex == CcLearnMap::kUnmapped)
            return false;   // CC mapped to nothing -> not forwarded [§3.3].
        data0 = static_cast<float>(paramIndex);
    }

    // Field-for-field copy with the §3.3 narrowings.
    out.type         = coreType;
    out.channel      = static_cast<std::int8_t>(in.channel);   // 1..16 narrowed; master = 1
    out.noteId       = static_cast<std::int16_t>(in.noteId);   // CLAP id; -1 preserved
    out.data0        = data0;                                  // note/CC->param/param idx as float
    out.value        = in.value;                               // copied verbatim
    out.sampleOffset = static_cast<int>(in.sampleOffset);      // sub-block offset copied
    return true;
}

int translateBlock(const HostEvent* first,
                   const HostEvent* last,
                   const CcLearnMap& ccMap,
                   mw::MidiEvent* out,
                   int outCapacity) noexcept {
    int written = 0;
    for (const HostEvent* it = first; it != last; ++it) {
        if (written >= outCapacity)
            break;   // drop-never-grow: surplus forwarded events are dropped [ADR-011 C9].
        mw::MidiEvent ev{};
        if (translateOne(*it, ccMap, ev)) {
            out[written] = ev;
            ++written;
        }
    }
    return written;
}

} // namespace mw::plugin
