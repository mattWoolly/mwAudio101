// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/OscAaMode.h — the per-voice oscillator anti-aliasing mode enum
// (consumed by the sub-oscillator, task 031; the VCO and section, tasks 029/030,
// consume the same type).
//
// This mirrors the ADR-018 Quality enum exactly as docs/design/01 §2.2 specifies:
// the canonical definition lives in docs/design/06, and the oscillator section
// CONSUMES the derived mode (PolyBLEP vs minBLEP) only. It is a tiny POD enum with
// ZERO JUCE so it is safe in the freestanding mwcore [docs/design/00 §3.3].
//
// The mode is set in prepare() / on structural reconfiguration only, NEVER per-sample
// on the audio thread [docs/design/01 §2.2; ADR-018 Q5].
//
// It lives in its own header (rather than Oscillator.h) so the sub-oscillator and the
// VCO can each depend on the enum without depending on each other, and so the parallel
// fleet does not serialize on a single shared file.

#pragma once

namespace mw101::dsp {

// Mirrors the ADR-018 enum (canonical definition in docs/design/06):
//   Eco      -> PolyBlep
//   Standard -> PolyBlep
//   HQ       -> MinBlepHq
enum class OscAaMode { PolyBlep, MinBlepHq };

} // namespace mw101::dsp
