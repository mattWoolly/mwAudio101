// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ControlDispatchCcIngressConstants.h — the (PI) constant set for the
// continuous-controller INGRESS leg of the ParamSnapshot -> DSP control-dispatch seam
// (task 162c, extending the ADR-028 keystone built by tasks 160 / 161 / 162 / 163).
//
// Per the parallel-fleet conflict-avoidance rule a module's (PI) constants land in a
// DEDICATED calibration header rather than being appended to the shared orchestrator
// core/calibration/Calibration.h, and a sibling task's header (162's
// ControlDispatchLfoConstants.h) is NOT edited [docs/design/00 §1.2; AGENTS.md "(PI)
// discipline"; ADR-028]. The dispatch seam references these by name and NEVER inlines a
// literal at a call site [ADR-020 S13].
//
// WHAT THIS COVERS. The 162 dispatch wired pitch-bend->{VCO,VCF} and mod-wheel->LFO-depth
// but the LIVE controller POSITION never reached the engine (BlockContext carried no
// continuous-controller state; PitchBend/ControlChange MidiEvents were dropped). 162c
// carries that position through the seam (BlockContext::controllers) and APPLIES it. These
// constants are the (PI) ingress/normalization + apply-law anchors for that leg:
//   * the CC1 mod-wheel raw->normalized divisor (7-bit value / 127 -> [0,1]);
//   * the mod-wheel number the engine recognizes as the mod-wheel CC (CC1);
//   * the bend/wheel clamp bounds (defensive: a stray controller value never inverts a
//     sign or runs away);
//   * the mod-wheel -> LFO-depth boost law's neutral/coverage anchors.
// Every numeric figure here is (PI) — a pragmatic integration anchor bounded by the cited
// MIDI/circuit-behavior frames, NOT a measured SH-101 oracle.

#pragma once

#include <cstdint>

namespace mw::cal::ccingress {

// ---------------------------------------------------------------------------
// The MIDI controller number the engine recognizes as the MOD WHEEL (CC1, the GM/MIDI
// Modulation controller). A ControlChange MidiEvent whose controller number is this one
// updates the live mod-wheel position; any other CC number is ignored by this ingress leg
// (other CCs reach the engine as ParamValue events through the plugin's CcLearnMap, a
// SEPARATE path — see Engine.cpp / docs/design/09 §6.2). (PI) — the fixed MIDI assignment.
inline constexpr std::int32_t kModWheelCcNumber = 1;   // CC1 == Modulation (mod wheel)

// ---------------------------------------------------------------------------
// CC value normalization: a 7-bit MIDI controller value (0..127) maps to [0,1] by dividing
// by the full-scale 127. The mod-wheel reaches the engine as the CC1 ControlChange value
// (already 0..127 in the standard MIDI domain); the ingress normalizes it to the [0,1]
// position the LFO-depth boost law consumes. (PI) — the standard 7-bit full-scale divisor.
inline constexpr float kSevenBitMax = 127.0f;

// ---------------------------------------------------------------------------
// Defensive clamp bounds. A stray/out-of-range controller value (a malformed host event, a
// future bridge that snapshots BlockContext::controllers directly) is clamped to the valid
// domain so the apply law never inverts a sign (bend) or runs the LFO depth negative (wheel).
inline constexpr float kPitchBendMin = -1.0f;   // centered wheel hard-left
inline constexpr float kPitchBendMax =  1.0f;   // centered wheel hard-right
inline constexpr float kModWheelMin  =  0.0f;   // wheel fully down (neutral)
inline constexpr float kModWheelMax  =  1.0f;   // wheel fully up

// ---------------------------------------------------------------------------
// Mod-wheel -> LFO-depth boost law (task 162c reconciliation with the 162 leg).
//
// The 162 dispatch read mod.lfo_mod_wheel as a STATIC additive depth offset
// (depthBoost = 1 + modWheelRouting) because no LIVE wheel position was in the seam. 162c
// supplies the live position, so the boost becomes a LIVE multiplier on the LFO depth:
//
//   effectiveBoost = kModWheelBoostBase + (mod.lfo_mod_wheel) * (live modWheel) * kModWheelBoostSpan
//
// At the neutral wheel-down position (modWheel == 0) the boost is exactly kModWheelBoostBase
// (== 1, identity) regardless of the routing param, so a routed patch with the wheel down is
// IDENTICAL to no routing — the wheel, not the routing knob alone, opens the modulation. As
// the wheel rises the boost grows toward kModWheelBoostBase + routing*kModWheelBoostSpan
// (up to 1 + 1*kModWheelBoostSpan at full routing + full wheel), deepening the LFO vibrato/
// wobble/PWM-sweep audibly. (PI) anchors [docs/design/03 §3.x MOD WHEEL; ADR-028 item 3].
inline constexpr float kModWheelBoostBase = 1.0f;   // (PI) — wheel-down identity (no boost)
inline constexpr float kModWheelBoostSpan = 1.0f;   // (PI) — full wheel+routing adds 1x depth

} // namespace mw::cal::ccingress
