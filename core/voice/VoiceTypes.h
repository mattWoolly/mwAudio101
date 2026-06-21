// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/VoiceTypes.h — shared voice/control PODs, enums, and pool constants
// (task 067). Header-only; no .cpp. Defines the types the whole voice/control
// stream consumes [docs/design/04-voice-and-control.md §3.1-§3.4].
//
// This header owns NO behavioral logic and NO classes (Voice / KeyAssigner /
// VoiceManager / ControlCore live in their own files). It owns ONLY: the
// compile-time pool-sizing constants (§3.1), the VoiceMode / GateTrigMode /
// VoiceState enums (§3.2), and the NoteEvent / NoteDecision PODs (§3.3). The (PI)
// pool caps themselves are NOT redefined here — they are referenced from the
// calibration table so the pool size stays centralized [§3.1; docs/design/00 §1.2].
//
// Parameter-ID strings and the APVTS schema are owned by doc 06 [ADR-008], not here.

#pragma once

#include <cstdint>

#include "../calibration/VoiceConstants.h"

namespace mw {

// ---------------------------------------------------------------------------
// §3.1 Compile-time pool sizing.
//
// The (PI) caps are referenced from core/calibration (NOT redefined here), so the
// pool size is centralized [§3.1; ADR-006 §Consequences]. kMaxVoices is derived
// from the product, satisfying the ADR-006 §3 invariant
// `kMaxVoices >= maxPoly x maxUnison` by construction.
// ---------------------------------------------------------------------------

inline constexpr int kMaxUnison = cal::voice::kMaxUnison;       // maxUnison cap [ADR-006 §3, C9]
inline constexpr int kMaxPoly   = cal::voice::kMaxPoly;         // (PI) v1 poly cap; calibration constant
inline constexpr int kMaxVoices = kMaxPoly * kMaxUnison;        // = 64 [ADR-006 §3]

// The ADR-006 §3 invariant, made mechanical [acceptance §3.1].
static_assert(kMaxVoices == kMaxPoly * kMaxUnison,
              "VoiceTypes: kMaxVoices MUST equal kMaxPoly * kMaxUnison [ADR-006 §3, C9].");

// ---------------------------------------------------------------------------
// §3.2 Modes and coupled selector.
// ---------------------------------------------------------------------------

// Voice-manager mode. MONO is default/bless target [ADR-016 R-3].
enum class VoiceMode : std::uint8_t { Mono = 0, Unison = 1, Poly = 2 };

// The single coupled S7 selector: priority AND retrigger are one control.
// There is deliberately NO separate priority param and NO separate retrigger
// toggle [research/07 §3.2-§3.3, §11; ADR-006 §Decision item 2, C].
enum class GateTrigMode : std::uint8_t {
    Gate     = 0,  // lowest-note priority, NO legato retrigger
    GateTrig = 1,  // last-note priority, retrigger every key
    Lfo      = 2   // lowest-note priority, ADSR (re)triggered by clock H->L edge
};

// Per-voice lifecycle state for the active-voice scan [ADR-006 §4].
enum class VoiceState : std::uint8_t {
    Idle      = 0, // not sounding; skipped by the render loop
    Active    = 1, // gate asserted, sounding
    Releasing = 2, // gate de-asserted, release tail finishing in place
    Stealing  = 3  // forced fast-fade before reuse (poly only) [ADR-006 C15]
};

// ---------------------------------------------------------------------------
// §3.3 Note events and decisions.
// ---------------------------------------------------------------------------

// `NoteEvent` is the engine-internal, already-demultiplexed MIDI event the
// VoiceManager consumes (translation from raw MIDI / MPE / arp-seq is owned by
// ADR-012/022).
struct NoteEvent {
    enum class Type : std::uint8_t { NoteOn, NoteOff, AllNotesOff };
    Type          type;
    std::uint8_t  note;         // 0..127 MIDI note
    float         velocity;     // 0..1 normalized; routed to VCA/VCF when velocity ON [ADR-016 R-2]
    int           sampleOffset; // sample-accurate offset within the block
};

// The ONLY thing the mono Voice consumes from note logic [ADR-006 §Decision item 1].
struct NoteDecision {
    int  activeNote = -1;    // resolved MIDI note, or -1 if no note held
    bool gate       = false; // gate asserted?
    bool retrigger  = false; // fire ADSR from trigger state this tick?
    bool clockReset = false; // assert CLOCK RESET (LFO/ARP re-phase) [research/07 §5.2]
    // The velocity [0,1] of the WINNING (active) note (task 162b velocity-ingress). Carried
    // from the held-key record through resolve() so the VoiceManager hands the REAL per-note
    // velocity to Voice::noteOn instead of the hardcoded 1.0; the 162 dispatch routes it to
    // VCA/VCF when mw101.vel.enable is on [ADR-016 R-2]. Neutral 1.0 when no note is held
    // (gate off) so a gate-off tick never disturbs the recorded velocity.
    float velocity  = 1.0f;
};

} // namespace mw
