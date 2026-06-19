// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/TuningBendWiringConstants.h — named constants for the tuning +
// bend-range param wiring (task 104b, plugin/midi/TuningBendWiring.h/.cpp). Realizes
// docs/design/09 §5 / §4.4 and ADR-012 C8 / C11 / C13 / C21-C23 / §Decision item 7.
//
// This is a NEW per-module constants header. Per AGENTS.md every invented constant is
// tagged (PI) and centralized in a calibration header; to avoid serializing on the
// single shared core/calibration/Calibration.h while the development fleet runs in
// parallel, this module's constants live in their OWN header in a NEW
// mw::cal::tunebend namespace. The orchestrator wires the include into Calibration.h
// later; this task does NOT edit the shared Calibration.h.
//
// NOTE ON (PI). Most values here are exact unit conversions (cents<->semitones) or the
// fixed doc-06 choice indices for mw101.voice.mode — these are documented facts, NOT
// invented tunables, but they live here as the single named home so no
// TuningBendWiring call site inlines a magic number [docs/design/06 §3.0, §3.10;
// docs/design/09 §4.4, §5].

#pragma once

namespace mw::cal::tunebend {

// --- Unit conversion (docs/design/09 §4.4, §5) ----------------------------------
//
// mw101.mod.bend_range_vco stores the channel bend range in CENTS (0..1200) [doc-06
// §3.0]; MidiFrontEnd::setBendRange takes SEMITONES. mw101.vco.fine stores the
// front-panel TUNE in SEMITONES (±1.0) [doc-06 §3.0]; MidiFrontEnd::setTuning takes
// CENTS. One semitone == 100 cents (the standard 12-TET cent definition) — an exact
// conversion, not a (PI) tunable.
inline constexpr float kCentsPerSemitone = 100.0f;

// --- Voice-mode collapse (docs/design/09 §4.4; ADR-012 C13) ---------------------
//
// mw101.voice.mode is a 3-choice param { Mono, Poly, Unison } [doc-06 §3.0]; index 0
// is Mono. In mono mode MPE collapses to channel pitch-bend + channel pressure
// [ADR-012 C13]. The index is the fixed doc-06 enumeration, not invented.
inline constexpr int kVoiceModeMonoIndex = 0;   // mw101.voice.mode: Mono == 0

} // namespace mw::cal::tunebend
