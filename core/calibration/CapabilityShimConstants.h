// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/CapabilityShimConstants.h — (PI) constants for the per-format
// CapabilityShim (task 112). Realizes docs/design/09 §7.2-7.4 / §8.1-8.2 and
// ADR-022 C5-C12.
//
// This is a NEW, shim-local calibration header (the shared core/calibration/
// Calibration.h is owned by task 005b and is NOT edited by leaf tasks). The shim has
// no measured SH-101 oracle (the stock instrument has zero host integration,
// docs/research/08 §2.1), so the few numeric defaults here are (PI) — *pragmatic
// inventions* with a tunable-default comment. No CapabilityShim call site inlines a
// (PI) literal [docs/design/06 §3.10; docs/design/00 §8.3].
//
// JUCE-free: the shim's .cpp links JUCE (it takes a juce::AudioPlayHead*), but this
// constants header is pure C++ so it compiles in either test binary.

#pragma once

namespace mw::cal::capshim {

// --- Transport-presence epsilon (Block-quantized phase re-lock; ADR-022 C6, C8) --
//
// When transport reappears after a Free-run window, the shim re-locks the clock phase
// from the host's absolute PPQ position [ADR-022 C8]. A reported PPQ position is
// considered a valid re-lock anchor only when it is finite and non-negative; this
// epsilon is the floor below which a tiny negative PPQ (host rounding near bar 0) is
// clamped to 0 rather than treated as "before the timeline". (PI) — a small tolerance,
// not an oracle value; tune if a host reports sub-tick negative PPQ at transport start.
inline constexpr double kPpqRelockEpsilon = 1.0e-9;  // (PI) — PPQ clamp floor at re-lock

} // namespace mw::cal::capshim
