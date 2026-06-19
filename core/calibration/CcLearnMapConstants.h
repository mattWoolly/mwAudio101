// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/CcLearnMapConstants.h — (PI) constants for the RT-safe CC/learn
// map (task 100, plugin/midi/CcLearnMap.h/.cpp).
//
// This is a NEW per-module (PI) constants header. Per AGENTS.md every invented
// constant is tagged (PI) and centralized in a calibration header; to avoid
// serializing on the single shared Calibration.h while the development fleet runs in
// parallel, this module's constant lives in its OWN header in a NEW
// mw::cal::cclearn namespace. The orchestrator wires the include into Calibration.h
// later; this task does NOT edit the shared Calibration.h.
//
// WHAT THIS DECLARES. The default §6.2 CC map binds CC1/7/11/74/71/5 to doc-06
// parameter IDs (real indices into the kParamDefs registry), but CC64 sustain drives
// the HOLD / external-HOLD INPUT semantics — a real stock jack (DP-2 footswitch),
// NOT a doc-06 automatable parameter [docs/design/09 §6.2; ADR-012 C20;
// docs/research/08 §2.1, §12]. So CC64 cannot resolve to a registry index; it
// resolves instead to a dedicated HOLD sentinel that is distinct from BOTH the
// unmapped sentinel (-1) and every non-negative registry index.
//
// WHY THIS VALUE. The map's lookup() returns an int32 param index, -1 == unmapped.
// HOLD needs a value that (a) is < 0 so it can never collide with a real
// registry index [0, kParamDefs.size()) and (b) != -1 so it is distinguishable from
// "unmapped". -2 is the smallest-magnitude negative value satisfying both; the exact
// numeric value is arbitrary (PI) — only the two non-collision properties are
// load-bearing, and they are asserted at compile time below and in the acceptance
// test. The HOLD/external-HOLD DSP target itself is owned elsewhere (HOLD jack
// semantics, not this task's scope); this constant is only the routing tag the learn
// map emits for CC64.
//
// (PI) — a pragmatic sentinel value, no measured-SH-101 oracle. Full-precision integer
// constant; identical on every platform [docs/design/00 §9.1 RT-7].

#pragma once

#include <cstdint>

namespace mw::cal::cclearn {

// Param-index sentinel meaning "CC routes to the HOLD / external-HOLD input semantics"
// (CC64 sustain in the default §6.2 map), NOT a doc-06 registry index [§6.2; ADR-012
// C20]. (PI): any value that is < 0 and != -1 (the unmapped sentinel) works; -2 is the
// canonical choice. The two non-collision properties are the contract, not the digit.
inline constexpr std::int32_t kHoldParamIndex = -2;

static_assert(kHoldParamIndex < 0,
              "CcLearnMapConstants: the HOLD sentinel MUST be negative so it can never "
              "collide with a real [0, kParamDefs.size()) registry index [§6.2].");
static_assert(kHoldParamIndex != -1,
              "CcLearnMapConstants: the HOLD sentinel MUST differ from the unmapped "
              "sentinel (-1) so CC64 HOLD is distinguishable from 'unmapped' [§6.2].");

} // namespace mw::cal::cclearn
