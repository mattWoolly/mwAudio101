// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/MidiFrontEndConstants.h — (PI) constants for the MIDI front-end
// note/gate/bend/pressure/CC translator (task 104, plugin/midi/MidiFrontEnd.h/.cpp).
// Realizes docs/design/09 §4.1-4.5 / §6.4 and ADR-012 C8/C9/C21-C24 / ADR-016 R-2.
//
// This is a NEW per-module (PI) constants header. Per AGENTS.md every invented
// constant is tagged (PI) and centralized in a calibration header; to avoid
// serializing on the single shared core/calibration/Calibration.h while the
// development fleet runs in parallel, this module's constants live in their OWN header
// in a NEW mw::cal::midifront namespace. The orchestrator wires the include into
// Calibration.h later; this task does NOT edit the shared Calibration.h.
//
// There is no SH-101 MIDI oracle — the stock instrument has zero MIDI
// [docs/research/08 §2.1, §2.3] — so the policy values here are disciplined clone-layer
// (PI) defaults. The FIXED MPE/MIDI bend-range bounds (0..24, 0..96 semitones) are the
// MPE-spec ranges named by doc-06/ADR-012, not tunables, but they live here as the
// single named home so no MidiFrontEnd call site inlines a magic number
// [docs/design/06 §3.10; docs/design/00 §8.3].

#pragma once

#include "Calibration.h"   // mw::cal::smoothing::* de-zipper time constants (task 005b/008)

namespace mw::cal::midifront {

// --- Pitch-bend wheel decode (docs/design/09 §4.4; ADR-012 C8) ------------------
//
// MIDI pitch-wheel is a 14-bit value 0..16383, centre = 8192 (no bend). The plugin
// shell decodes it to a signed unit offset in [-1, +1]; this front-end multiplies by
// the active channel bend range (semitones) to yield the continuous Pre-Q offset.
inline constexpr float kPitchWheelCentre   = 8192.0f;  // 14-bit centre (no bend)
inline constexpr float kPitchWheelHalfSpan = 8192.0f;  // divisor: max excursion each side

// 7-bit MIDI data normaliser (channel-pressure / CC value 0..127 -> 0..1).
inline constexpr float kSevenBitMax = 127.0f;

// --- Bend ranges (docs/design/09 §4.4; ADR-012 C8, C11) -------------------------
//
// Defaults: channel bend = ±2 semitones [ADR-012 C8]; MPE per-note and MPE master
// bend = ±48 semitones (the MPE-spec default) [ADR-012 C11]. The CLAMP bounds are the
// documented ranges: channel 0..24, MPE 0..96. (PI) only insofar as no SH-101 oracle
// exists; the numeric bounds are the doc-06/ADR-012 spec, not invented.
inline constexpr float kDefaultChannelBendSemis = 2.0f;   // ±2 [ADR-012 C8]
inline constexpr float kDefaultMpeNoteBendSemis = 48.0f;  // ±48 [ADR-012 C11]
inline constexpr float kDefaultMpeMasterBendSemis = 48.0f;// ±48 [ADR-012 C11]

inline constexpr float kChannelBendMinSemis = 0.0f;   // channel bend range floor [§4.4]
inline constexpr float kChannelBendMaxSemis = 24.0f;  // channel bend range ceiling [§4.4]
inline constexpr float kMpeBendMinSemis     = 0.0f;   // MPE bend range floor [§4.4]
inline constexpr float kMpeBendMaxSemis     = 96.0f;  // MPE bend range ceiling [§4.4]

// --- Tuning reference + TUNE (docs/design/09 §5; ADR-012 C21-C23) ---------------
//
// A4 reference: default 440 Hz over ~400..460 Hz [ADR-012 C21]. TUNE is a separate
// ±1.0-semitone fine control layered on top [ADR-012 C23]. These mirror the doc-06
// param ranges (mw101.tune.a4, mw101.vco.fine); stored here so the front-end's reset
// state is named, not inlined.
inline constexpr float kDefaultA4Hz       = 440.0f;  // [ADR-012 C21]
inline constexpr float kA4MinHz           = 400.0f;  // [ADR-012 C21]
inline constexpr float kA4MaxHz           = 460.0f;  // [ADR-012 C21]
inline constexpr float kDefaultTuneCents  = 0.0f;    // TUNE rest = 0 [ADR-012 C23]
inline constexpr float kTuneRangeSemis    = 1.0f;    // ±1.0 semitone [ADR-012 C23]

// --- Velocity routing (docs/design/09 §4.5; ADR-016 R-2) ------------------------
//
// Velocity is ON by default [ADR-016 R-2]. When ON it routes to BOTH documented
// physical nodes: VCA level and VCF cutoff amount [ADR-016 R-2; docs/research/08
// §7.2, §5.3]. Velocity is additive over the real circuitry, centred so that the
// nominal velocity (kVelocityCentre, ~mezzo-forte 64/127) contributes ZERO offset and
// the signed offset spans [-1,+1]*depth across the velocity range. The per-destination
// depth scalers are (PI) — no velocity oracle exists on an instrument with no velocity.
inline constexpr bool  kDefaultVelocityEnabled = true;   // ON by default [ADR-016 R-2]
inline constexpr float kVelocityCentre = 0.5f;           // (PI) float-velocity zero-offset point
inline constexpr float kVelToVcaLevelDepth  = 1.0f;      // (PI) velocity -> VCA level scaler
inline constexpr float kVelToVcfCutoffDepth = 1.0f;      // (PI) velocity -> VCF cutoff scaler

// --- De-zipper (docs/design/09 §6.4; ADR-012 C24; ADR-020) ----------------------
//
// Bend / pressure / CC value changes are de-zippered O(1)/sample with NO branch on
// message arrival [ADR-012 C24]. The time constants reuse the project-wide per-class
// de-zipper policy (ADR-020): bend rides the Pitch class (~2 ms) and pressure rides the
// Fast class (~10 ms). Reusing the central constants keeps the de-zipper behaviour in
// step with the rest of the engine rather than minting a parallel (PI) value.
inline constexpr double kBendSmoothSeconds     = mw::cal::smoothing::kPitchSeconds;  // ~2 ms
inline constexpr double kPressureSmoothSeconds = mw::cal::smoothing::kFastSeconds;   // ~10 ms

// Rest values the smoothers reset to (no bend / no pressure at init).
inline constexpr float kBendRestSemis    = 0.0f;
inline constexpr float kPressureRestNorm = 0.0f;

} // namespace mw::cal::midifront
