// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/PolyAllocatorConstants.h — the (PI) POLY-allocator tuning
// constant(s) for the deterministic per-note allocator + voice-stealing scan
// (task 075).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h. It #includes that root and APPENDS its value(s)
// into the SAME mw::cal::voice namespace that VoiceConstants.h (task 067),
// VoiceDriftConstants.h (task 073) and VoiceManagerConstants.h (task 074) opened, so
// the voice-subsystem (PI) constants compose additively and no call site inlines a
// (PI) literal [docs/design/00 §1.2; docs/design/06 §3.10; ADR-008 §1; ADR-020 S13].
//
// WHAT THIS OWNS:
//   - kRestrikeWindowSemitones: the POLY re-strike "window" within which an incoming
//     note-on that matches a still-held voice's currentNote reuses that voice instead
//     of allocating a fresh one (no doubling) [docs/design/04 §6.4; ADR-006 C13].
//     A re-strike is an EXACT MIDI-note match in v1 (one semitone == one MIDI step),
//     so the window is 0 semitones — a poly note-on doubles a held key only if it is a
//     different note. This is the (PI) interpretation of §6.4's "re-strike window"
//     for the integer-MIDI poly path; widening it (e.g. for MPE-fine pitch) is a
//     deliberate future change recorded against this constant, not an inlined literal.
//
// The constant is a compile-time integer => deterministic and identical across the
// macOS arm64 bless gate / Linux co-gate (no FP, no wall-clock) [ADR-006 C18;
// ADR-019 VT-04].
//
// OUT OF SCOPE (other docs/ADRs own these): the steal-fade length (kStealFadeMs,
// VoiceDriftConstants.h, task 073); the unison detune/spread laws
// (VoiceManagerConstants.h, task 074); the pool caps (VoiceConstants.h, task 067);
// MPE per-note pitch (ADR-012/022).

#pragma once

#include "Calibration.h"

namespace mw::cal::voice {

// (PI) POLY re-strike window in semitones [docs/design/04 §6.4; ADR-006 C13]. An
// incoming poly note-on reuses an already-held voice (no doubling) when the absolute
// MIDI-note difference to that voice's currentNote is <= this window. With v1's
// integer-MIDI poly path a re-strike is an exact-note match, so the window is 0.
inline constexpr int kRestrikeWindowSemitones = 0;

} // namespace mw::cal::voice
