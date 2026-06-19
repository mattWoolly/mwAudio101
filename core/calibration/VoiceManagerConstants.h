// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/VoiceManagerConstants.h — the (PI) UNISON detune/stereo-spread
// distribution laws for VoiceManager (task 074).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h. It #includes that root and APPENDS its values into
// the SAME mw::cal::voice namespace that VoiceConstants.h (task 067) and
// VoiceDriftConstants.h (task 073) opened, so the voice-subsystem (PI) constants
// compose additively and no call site inlines a (PI) literal [docs/design/00 §1.2;
// docs/design/06 §3.10; ADR-008 §1; ADR-020 S13]. The calibration orchestrator wires
// this include from Calibration.h.
//
// WHAT THIS OWNS:
//   - unisonDetuneCents(i, U, spreadCents): the SYMMETRIC, CENTERED cents-detune law
//     for unison voice i of U [docs/design/04 §5.3 / §6.3; ADR-006 C10]:
//         detuneCents_i = spreadCents * (2*i/(U-1) - 1)   for U > 1,
//         0                                               for U == 1.
//     Voice 0 sits at -spreadCents, voice U-1 at +spreadCents, symmetric about 0.
//   - unisonPan(i, U, spreadAmount): the (PI) stereo-spread distribution law
//     [docs/design/04 §5.3 / §6.3; ADR-006 C10; ADR-013]:
//         pan_i = spreadAmount * (2*i/(U-1) - 1)          for U > 1,
//         0                                               for U == 1.
//     A symmetric linear fan across [-spreadAmount, +spreadAmount], centered. This is
//     a MODERN addition with no hardware analog [ADR-013]; it is labeled (PI), not
//     asserted as SH-101 fidelity.
//   - kDefaultUnisonDetuneCents / kDefaultUnisonSpread: (PI) tuning defaults the
//     VoiceManager uses until doc-06 parameters drive them (range/skew owned by
//     doc 06 [ADR-008], the behavioral law owned here [docs/design/04 §5.3]).
//
// Both laws are pure float arithmetic over integers => deterministic and identical
// across the macOS arm64 bless gate / Linux co-gate (no transcendental, no wall-clock)
// [ADR-006 C18; ADR-019 VT-04]. They are constexpr-evaluable.
//
// OUT OF SCOPE (other docs/ADRs own these): the host parameter IDs/ranges/skews that
// supply spreadCents / spreadAmount (doc 06 [ADR-008]); the per-voice drift seed mixer
// (VoiceDriftConstants.h, task 073); POLY allocation/stealing (task 075).

#pragma once

#include "Calibration.h"

namespace mw::cal::voice {

// Symmetric, centered unison cents-detune law for voice `i` of `unisonCount`
// [docs/design/04 §5.3, §6.3; ADR-006 C10]. Edges land at ±spreadCents; the law is
// centered on 0 so the stack stays centered on the resolved note. For unisonCount==1
// (or any degenerate count) the single voice is exactly on pitch (0 cents).
constexpr float unisonDetuneCents(int i, int unisonCount, float spreadCents) noexcept {
    if (unisonCount <= 1) {
        return 0.0f;
    }
    const float t = static_cast<float>(2 * i) / static_cast<float>(unisonCount - 1) - 1.0f;
    return spreadCents * t;
}

// (PI) symmetric stereo-spread fan for voice `i` of `unisonCount`
// [docs/design/04 §5.3, §6.3; ADR-006 C10; ADR-013]. pan in [-spreadAmount,
// +spreadAmount], centered; pan==0 for a single voice. A modern addition; no hardware
// analog. The structure mirrors the detune law so detune/pan move together
// symmetrically across the stack.
constexpr float unisonPan(int i, int unisonCount, float spreadAmount) noexcept {
    if (unisonCount <= 1) {
        return 0.0f;
    }
    const float t = static_cast<float>(2 * i) / static_cast<float>(unisonCount - 1) - 1.0f;
    return spreadAmount * t;
}

// (PI) unison tuning defaults the VoiceManager applies until doc-06 parameters drive
// them. spreadCents is the half-width of the detune fan in cents; spreadAmount is the
// half-width of the pan fan in normalized pan units [-1,+1]. Modern additions
// [ADR-013]; the canonical host ranges/skews are doc 06's [ADR-008].
inline constexpr float kDefaultUnisonDetuneCents = 7.0f;   // (PI) ±7 cents
inline constexpr float kDefaultUnisonSpread      = 1.0f;   // (PI) full stereo fan

} // namespace mw::cal::voice
