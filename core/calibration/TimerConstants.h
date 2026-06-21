// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/TimerConstants.h — (PI) timing constants for the editor's single
// coalescing telemetry Timer (task 115).
//
// Per docs/design/10-ui.md §8.4 the editor runs ONE juce::Timer that drains the
// audio->GUI SPSC telemetry ring and triggers targeted repaints at 30-60 Hz. The
// default rate, the floor, and the reduce-motion downsample rate are (PI) pragmatic
// inventions within the ADR-mandated band — NOT measured SH-101 specs — and must
// centralize here rather than be inlined at the Timer call site [docs/design/10-ui.md
// §8.4; ADR-015 C5/C8; AGENTS.md "(PI)"]. This is a NEW dedicated header (NOT the
// orchestrator-owned aggregate Calibration.h) so the parallel UI fleet does not
// serialize on a shared table.
//
// kDefaultTimerHz       : the (PI) default refresh rate, within the 30-60 Hz band.
// kMinTimerHz           : the ADR-mandated floor of the allowed band.
// kMaxTimerHz           : the ADR-mandated ceiling of the allowed band.
// kReduceMotionTimerHz  : the downsampled rate the Timer drops to when reduce-motion
//                         is enabled (a low, still-non-zero rate so the scope idles
//                         to a static frame without a fully-frozen indicator) [§10].

#pragma once

namespace mw::cal::timer {

// (PI) — default editor Timer refresh rate (Hz), within the 30-60 Hz band [§8.4].
inline constexpr int kDefaultTimerHz = 60;

// The ADR-mandated allowed band [docs/design/10-ui.md §8.4; ADR-015 C5].
inline constexpr int kMinTimerHz = 30;   // floor
inline constexpr int kMaxTimerHz = 60;   // ceiling

// (PI) — reduce-motion / low-CPU downsampled rate (Hz). Low, still non-zero, so the
// scope settles to a static idle frame on weak GPUs / very high-res displays without a
// per-frame animation cost [docs/design/10-ui.md §10; ADR-015 C8].
inline constexpr int kReduceMotionTimerHz = 8;

static_assert(kMinTimerHz <= kDefaultTimerHz && kDefaultTimerHz <= kMaxTimerHz,
              "Default Timer rate must sit within the 30-60 Hz band [§8.4].");
static_assert(kReduceMotionTimerHz > 0 && kReduceMotionTimerHz < kMinTimerHz,
              "Reduce-motion rate is a downsampled rate below the normal floor [§10].");

// ---------------------------------------------------------------------------
// (PI) — the scope / mod-source INDICATOR region the Timer repaints, expressed in
// DESIGN units over the 1000x640 design space [docs/design/10-ui.md §4.1, §8.4]. The
// editor maps this through its single design->pixels AffineTransform to a dirty-rect
// so the Timer repaints ONLY this region, never the whole editor [ADR-015 C7]. The
// actual scope painting / exact placement is the ScopeMeterOverlay's (ui-15, out of
// scope); these are a provisional placeholder until that layout pass, kept here (not
// inlined) so retuning the canvas never touches Timer code.
// ---------------------------------------------------------------------------
inline constexpr float kScopeRegionX = 620.0f;  // (PI) design-unit left
inline constexpr float kScopeRegionY = 40.0f;   // (PI) design-unit top
inline constexpr float kScopeRegionW = 340.0f;  // (PI) design-unit width
inline constexpr float kScopeRegionH = 180.0f;  // (PI) design-unit height

} // namespace mw::cal::timer
