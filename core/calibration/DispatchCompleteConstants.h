// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/DispatchCompleteConstants.h — the (PI) measurement thresholds + the
// fixed reference frequencies used ONLY by the control-dispatch COMPLETENESS audit suite
// (task 165; tests/unit/DispatchCompleteTest.cpp; ADR-028 capstone).
//
// WHY A CALIBRATION HEADER FOR A TEST. AGENTS.md "ADRs & decisions" requires every invented
// constant be tagged (PI) and centralized in a calibration header rather than inlined at the
// call site. The completeness battery drives EACH live param low-vs-high and asserts the
// rendered output changes in the expected dimension; the per-dimension detection thresholds
// (how big a low-vs-high delta proves a wired param, and the band a disconnect would violate)
// are those invented numbers. They live here, named, so the assertions read declaratively and
// a future re-tune is a single edit. JUCE-FREE pure constexpr data (no juce::*; mwcore-clean).
//
// These are TEST thresholds — they gate the audit's pass/fail, NOT any audio path. None is
// read by the engine; the production dispatch laws live in the ControlDispatch*Constants.h /
// FxDispatchConstants.h headers this suite exercises THROUGH the assembled Engine.

#pragma once

namespace mw::cal::dispatchcomplete {

// --- analysis reference frequencies (Hz) ------------------------------------------------
// The 12-TET A4 = 440 reference the Goertzel fundamental probes predicate against (the test
// drives MIDI notes; midiHz maps note -> Hz at this reference). The character/tune.a4 legs
// predicate against the 442 hardware home, sourced from cal::vco::kPitchRefHz at the call site.
inline constexpr double kA4Hz = 440.0;

// --- low/high drive points (normalized 0..1 unless noted) -------------------------------
// The audit drives each continuous param at a LOW and a HIGH point (others at INIT) and
// asserts the rendered dimension differs. These are the generic endpoints; a few legs use
// engineering values set explicitly at the call site (e.g. tune.a4 in Hz, glide.time in s).
inline constexpr float kLow01  = 0.05f;   // (PI) — a low-but-non-zero normalized drive point
inline constexpr float kHigh01 = 0.95f;   // (PI) — a high-but-non-clipping normalized drive point

// --- ratio thresholds: "the param visibly moved its dimension" --------------------------
// A covered param's low-vs-high render must differ by AT LEAST this factor in its measured
// dimension; a DISCONNECT (low == high, the bug this audit catches) would collapse the ratio
// to ~1.0 and FAIL the assertion. The margins sit well above measurement noise (~1-2%) yet
// below the real effect, so the test is non-vacuous (proven by the deliberate-disconnect case).
inline constexpr double kMinRatioStrong = 2.0;    // (PI) — strong dimensional change (spectrum/amp/echo)
inline constexpr double kMinRatioClear  = 1.30;   // (PI) — a clear, robust change (subtler legs)

// --- pitch-shift detection (relative) ---------------------------------------------------
// A pitch leg (tune/fine/a4/range/bend) must move the rendered fundamental by at least this
// relative amount; below it the two renders are "the same pitch" and the leg is disconnected.
inline constexpr double kMinPitchRelShift = 0.01;  // (PI) — >=1% fundamental shift == real

// --- bit-difference detection for the frozen-at-note-on / model-observable legs ----------
// var.* spread + drift are per-voice perturbations frozen at note-on: a low-vs-high render is
// not a simple amplitude/pitch ratio but a DIFFERENT note personality (a changed sample
// stream). The audit asserts the two renders are NOT bit-identical AND both still sound above
// this RMS floor (the perturbation is a change, never a mute).
inline constexpr double kSoundingRmsFloor = 1.0e-4;  // (PI) — "the voice still sounds"

// --- silence / identity ceilings --------------------------------------------------------
// An amplitude leg driven to its zero point (vca.level 0, expression 0) must fall below this
// ceiling (effectively silent); an inert/exempt-decode leg (mpe.*, the disconnected-finding
// params) must render BIT-IDENTICAL low-vs-high — asserted by exact equality, not a ceiling.
inline constexpr double kSilenceCeiling = 1.0e-6;   // (PI) — below this == silent

// --- channel-difference threshold (stereo FX legs) --------------------------------------
// A stereo-widening FX leg (chorus width, delay width) makes L diverge from R by at least this
// RMS(L-R); a mono/bypassed path holds L==R below the identity ceiling.
inline constexpr double kStereoDiffFloor   = 1.0e-3;  // (PI) — genuine stereo image
inline constexpr double kMonoDiffCeiling   = 1.0e-7;  // (PI) — phase-coherent mono (L==R)

} // namespace mw::cal::dispatchcomplete
