// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/LatencyReporterConstants.h — (PI) constants for the constant-PDC
// LatencyReporter (task 105, plugin/latency/LatencyReporter.h/.cpp).
//
// This is a NEW per-module (PI) constants header. Per AGENTS.md every invented
// constant is tagged (PI) and centralized in a calibration header; to avoid
// serializing on the single shared Calibration.h while the development fleet runs in
// parallel, this module's constant lives in its OWN header in a NEW
// mw::cal::latency namespace. The orchestrator wires the include into Calibration.h
// later; this task does NOT edit the shared Calibration.h.
//
// WHAT THIS DECLARES. The single missing group-delay constant the constant-PDC report
// must sum: the PER-VOICE oversampled-zone (IR3109 ladder + diode-clamp resonance +
// BA662 VCA drive; realtime polyphase IIR halfband, ADR-004) round-trip group delay,
// in BASE-RATE samples [ADR-017 L1; docs/design/00 §7.2; docs/design/09 §8.3].
//
// The OTHER contributor — the post-voice FX Drive 2x oversampler group delay
// [ADR-017 L2] — already has a named, core-owned constant
// (mw::cal::fxos::kReportedLatencySamples, FxOversampler2xConstants.h) and is sourced
// from there directly; it is NOT re-declared here.
//
// WHY THIS VALUE. The per-voice realtime IIR halfband (core/dsp/Oversampler.h, task
// 036) uses the SAME frozen elliptic-halfband coefficient set as the FX-rate halfband
// (core/dsp/fx/FxOversampler2x.h, task 090) — same structure (two crossed first-order
// allpass branches), same 5-sections-per-branch coefficients. Its energy-weighted
// round-trip impulse-response centroid (the robust IIR group-delay proxy used by
// FxOversampler2x::measureRoundTripLatency, also ADR-017 L1's "measured" basis)
// therefore lands on the SAME fixed integer: 10 base-rate samples. The acceptance
// test re-derives this from the frozen mw::cal::osiir coefficients and asserts the
// measured value equals this declared constant, so the number cannot silently drift
// from the coefficients it is derived from. Any future re-tune of the voice halfband
// that moves it ships a new renderVersion-keyed set [docs/design/00 §8.3; ADR-017
// L11].
//
// (PI) — a pragmatic engineering quantity (the group delay of a (PI) coefficient set),
// no measured-SH-101 oracle. It is NONZERO (the host actually compensates it) and
// CONSTANT for the instance lifetime, independent of input, block size, and sample
// rate (it is the halfband's fixed integer base-rate group delay) [ADR-017 L1, L4,
// L5, L10].
//
// Full-precision integer constant; identical on macOS arm64 (reference) and Linux x64
// [docs/design/00 §9.1 RT-7].

#pragma once

namespace mw::cal::latency {

// Per-voice oversampled-zone (ADR-004 realtime IIR halfband) round-trip group delay,
// in BASE-RATE samples. CONTRIBUTES to reported PDC [ADR-017 L1]. (PI), MEASURED from
// the round-trip impulse response of the frozen mw::cal::osiir coefficients; the
// acceptance test asserts measured == this.
inline constexpr int kVoiceZoneGroupDelaySamples = 10;

static_assert(kVoiceZoneGroupDelaySamples > 0,
              "LatencyReporterConstants: the per-voice oversampled-zone group delay "
              "MUST be nonzero so the host compensates it [ADR-017 L1].");

} // namespace mw::cal::latency
