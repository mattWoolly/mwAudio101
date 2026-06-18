// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/VoiceConstants.h — the (PI) voice-pool sizing caps (task 067).
//
// These are the cross-module (PI) calibration constants for the voice/control
// subsystem's compile-time pool sizing [docs/design/04-voice-and-control.md §3.1].
// They live in the mw::cal namespace (a sibling of the central
// core/calibration/Calibration.h table) so the pool size is centralized and no
// call site inlines a (PI) literal [docs/design/00 §1.2 calibration policy;
// docs/design/06 §3.10]. The orchestrator wires this header into Calibration.h.
//
// Every constant here is (PI) — a *pragmatic invention*, NOT a measured SH-101
// spec. kMaxUnison is the maxUnison cap [ADR-006 §3, C9]; kMaxPoly is the (PI) v1
// poly cap, sized generously per ADR-006 (raising it is a recompile, not a runtime
// knob [ADR-006 §Consequences]). The ADR-006 §3 invariant
// `kMaxVoices >= maxPoly x maxUnison` is satisfied by deriving kMaxVoices from the
// product in VoiceTypes.h.

#pragma once

namespace mw::cal::voice {

// (PI) maxUnison cap [ADR-006 §3, C9].
inline constexpr int kMaxUnison = 8;

// (PI) v1 poly cap; calibration constant sized generously [ADR-006 §Consequences].
inline constexpr int kMaxPoly = 8;

} // namespace mw::cal::voice
