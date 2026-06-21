// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/SignpostConstants.h — the (PI) trigger thresholds + STATIC notice
// strings for the two non-modal honesty signposts (task 129b) surfaced via the
// StatusBanner [docs/design/09 §5; docs/design/00 §8.5; ADR-012 §Consequences;
// ADR-023 V16].
//
// WHY THIS HEADER. Per the calibration discipline (AGENTS.md "(PI)"), neither the
// 442-active tuning threshold nor the notice strings are inlined at the component call
// site; they centralize here so the visual-design/legal pass can retune the wording or
// the trigger in one place. This is a NEW dedicated header (NOT the orchestrator-owned
// core/calibration/Calibration.h aggregate) so the parallel development fleet does not
// collide [AGENTS.md "ADRs & decisions"].
//
// SINGLE SOURCES OF TRUTH (referenced, never redefined):
//   * the 440-default vs 442-hardware-accurate tuning duality and the mandate that 442
//     is only ever a recalled "hardware-accurate" preset, never the default
//       [docs/design/09 §5; ADR-012 §Consequences C21/C22] — the A4 default lives in
//       core/calibration/MidiFrontEndConstants.h (kDefaultA4Hz);
//   * the blessed sample-rate set + OS_CEILING clamp provenance ("running unblessed at
//     this host rate") [docs/design/00 §8.5; ADR-023 V15/V16] — the membership /
//     clamp predicate live in core/version/RenderProvenance.h + GoldenKeyConstants.h.
//
// JUCE-FREE: plain floats + const char* literals; the component (JUCE-built) lifts the
// strings into juce::String at the StatusBanner seam. Nothing here references juce::*
// [ADR-001 C1].

#pragma once

#include "MidiFrontEndConstants.h"   // mw::cal::midifront::kDefaultA4Hz (the 440 default)

namespace mw::cal::ui::signpost {

// ---------------------------------------------------------------------------
// Tuning-duality signpost trigger (PI).
//
// The signpost is shown CONTEXTUALLY: only when the active A4 reference has been moved
// off the 440 default (e.g. the "hardware-accurate" 442 preset is recalled), so a user
// who never touches tuning is not nagged. The threshold is "A4 differs from the 440
// default by more than this epsilon" — an exact-equality test would be brittle against
// the continuous param, so we use a small Hz tolerance well below one cent at A4
// [docs/design/09 §5; ADR-012 §Consequences C21/C22].
// ---------------------------------------------------------------------------

// The 440-default reference the signpost compares against — re-exported from the single
// MIDI-front-end source so this header never re-mints the default [ADR-012 C21].
inline constexpr float kDefaultA4Hz = mw::cal::midifront::kDefaultA4Hz;   // 440.0 Hz

// The documented "hardware-accurate" VR-7 reference [ADR-012 C22]. Used only to frame
// the static notice text; the signpost triggers on "off the 440 default", not on this
// exact value, so any non-default A4 (442 or otherwise) is honestly surfaced.
inline constexpr float kHardwareA4Hz = 442.0f;   // [ADR-012 C22; docs/design/09 §5.2]

// Hz tolerance for "A4 is still at the 440 default" (PI). < 0.01 Hz is ~0.04 cent at
// A4 — far finer than any audible or param-quantized step, so only a deliberate move
// off 440 trips the signpost.
inline constexpr float kA4DefaultEpsilonHz = 0.01f;

// ---------------------------------------------------------------------------
// Static notice strings (PI). The StatusBanner HOSTS — does not author — text; these
// are the signpost notices the component injects. Kept terse and non-alarming: both are
// honesty notices, not error conditions [docs/design/09 §5; ADR-023 V16]. No audio-
// thread work touches these — they are compile-time literals surfaced on the message
// thread only.
// ---------------------------------------------------------------------------

// Tuning-duality note (info severity). Shown when A4 is off the 440 default so users do
// not mistrust the pitch [docs/design/09 §5; ADR-012 §Consequences "must be surfaced in
// UI ... or users will mistrust tuning"].
inline constexpr const char* kTuningDualityNotice =
    "A4 reference is off the 440 default (e.g. the 442 Hz hardware-accurate value) "
    "\xe2\x80\x94 pitch is intentional, not a bug.";

// Unblessed-rate note (warn severity). Shown when the host sample rate is above the
// blessed set OR 2x oversampling was clamped to 1x at OS_CEILING [docs/design/00 §8.5;
// ADR-023 V16 — the exact UI phrase is normative].
inline constexpr const char* kUnblessedRateNotice =
    "Running unblessed at this host rate.";

} // namespace mw::cal::ui::signpost
