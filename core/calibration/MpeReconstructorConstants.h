// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/MpeReconstructorConstants.h — (PI) constants for the MPE-over-MIDI
// reconstruction parser (task 103). Realizes docs/design/09 §7.3 and ADR-022 C2 /
// ADR-012 §4, C10-C13.
//
// This is a NEW, parser-local calibration header (the shared core/calibration/
// Calibration.h is owned by task 005b and is NOT edited by leaf tasks). Every value
// here is a (PI) — a *pragmatic invention* with no measured SH-101 oracle (the stock
// instrument has zero MIDI, docs/research/08 §2.1) — so each constant carries a
// tunable-default comment. No MpeReconstructor call site inlines a (PI) literal
// [docs/design/06 §3.10; docs/design/00 §8.3].

#pragma once

#include <cstdint>

namespace mw::cal::mpe {

// --- MPE lower-zone channel layout (ADR-012 §4; docs/design/09 §7.3) ------------
//
// "Lite" = lower zone ONLY. The lower-zone MASTER is MIDI channel 1; the member
// channels are MIDI channels 2..16. Member-channel COUNT is configurable: default
// OFF (0 members), opt-in 1..15 members [ADR-012 C10]. These are NOT (PI) tunables —
// they are the fixed MPE lower-zone topology — but they live here as the single
// named home so no call site inlines the magic channel numbers.
inline constexpr std::uint8_t kMasterChannel    = 1;   // lower-zone master (1-based MIDI)
inline constexpr std::uint8_t kFirstMemberChannel = 2; // first member (1-based MIDI)
inline constexpr std::uint8_t kLastMemberChannel  = 16;// last member (1-based MIDI)
inline constexpr int          kMaxMembers       = 15;  // members 1..15 [ADR-012 C10]
inline constexpr int          kDefaultMembers   = 0;   // default OFF (0 members) [ADR-012 C10]
inline constexpr int          kNumMidiChannels  = 16;  // 1..16, sizes channelToVoice_

// --- Initial per-voice expression values ----------------------------------------
//
// Pre-quantizer pitch offset starts at 0 semitones (no bend); the assignable
// pressure destination starts at 0 (idle). (PI) defaults — there is no oracle for an
// MPE rest state on an instrument with no MIDI.
inline constexpr float kInitialPitchOffsetSemis = 0.0f;  // (PI) — no bend at rest
inline constexpr float kInitialPressureNorm     = 0.0f;  // (PI) — pressure idle at rest

// Sentinel for "no voice currently assigned to this channel" in channelToVoice_.
inline constexpr int kUnassignedVoice = -1;

} // namespace mw::cal::mpe
