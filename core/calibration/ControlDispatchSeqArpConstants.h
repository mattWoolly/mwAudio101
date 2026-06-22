// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ControlDispatchSeqArpConstants.h — the seq/arp control-dispatch
// option-index -> engine-enum maps (task 181; ADR-030 part 1; ADR-028 dispatch idiom).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h; it #includes that root and APPENDS into the
// mw::cal::dispatch namespace (the same the rest of the ADR-028 dispatch maps use), so
// the headers compose additively without editing the shared root [docs/design/06 §3.10;
// ADR-020 S13]. The Engine's control-tick dispatch inlines NO literal choice index.
//
// These translate the seq.*/arp.* APVTS choice ENUM positions (owned by docs/design/06
// §2 / core/params/ParamDefs.h) into the mwcore control-core enums the SequencerEngine /
// Arp / Clock consume. The choice LABEL ORDER is the contract:
//   * mw101.arp.mode  = { Off=0, Up=1, Down=2, Up-Down=3 }  (ParamDefs detail::kArpMode)
//       -> mw::control::ArpMode { Up=0, UandD=1, Down=2 } + an "engaged" gate (Off => the
//          arp direction is inert; arp engagement is the HOLD latch / held-chord path).
//   * mw101.seq.mode  = { Off=0, Play=1, Record=2 }          (ParamDefs detail::kSeqMode)
//       -> the StepSequencer PLAY / RECORD toggles (not a ControlSnapshot field).
//   * mw101.{arp,seq}.sync_div choice (kSyncDiv = { 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 })
//       -> mw::control::HostRate (the host-sync musical division). The first SIX HostRate
//          positions line up 1:1 with the six kSyncDiv labels in order (Quarter, Eighth,
//          EighthT, Sixteenth, SixteenthT, ThirtySecond); the two trailing HostRate dotted
//          positions have no kSyncDiv label and are not reachable from this param.
//
// EVERY mapping here is the documented LABEL ORDER, not a measured circuit spec — a
// pragmatic invention bridging the param schema to the control-core enums (PI).

#pragma once

#include "Calibration.h"                 // composes into the same mw::cal root table
#include "control/ControlTypes.h"        // ArpMode / HostRate enums the maps target

namespace mw::cal::dispatch {

// arp.mode choice { Off=0, Up=1, Down=2, Up-Down=3 } -> control::ArpMode direction.
// "Off" has no direction enum (the arp engages via HOLD / a held chord, doc 05 §5.1), so
// it maps to the Up direction defensively but reports engaged=false via arpModeEngages().
// Defensive default = Up.
[[nodiscard]] inline constexpr mw::control::ArpMode arpModeFor(int choiceIdx) noexcept {
    switch (choiceIdx) {
        case 1:  return mw::control::ArpMode::Up;     // "Up"
        case 2:  return mw::control::ArpMode::Down;   // "Down"
        case 3:  return mw::control::ArpMode::UandD;  // "Up-Down"
        default: return mw::control::ArpMode::Up;     // "Off" (inert) / defensive
    }
}

// Whether the arp.mode choice selects an ACTIVE arp direction (anything but "Off"=0). The
// SequencerEngine has no arp on/off ControlSnapshot field (doc 05 §5.1 names arp.mode as
// the on/off + direction); the Engine's routing gate engages the arp on the HOLD latch, so
// this is advisory today — the param keeps "Off" expressible for a future enable field.
[[nodiscard]] inline constexpr bool arpModeEngages(int choiceIdx) noexcept {
    return choiceIdx != 0;
}

// seq.mode choice { Off=0, Play=1, Record=2 } decoded to the two StepSequencer toggles.
[[nodiscard]] inline constexpr bool seqModePlays(int choiceIdx)   noexcept { return choiceIdx == 1; }
[[nodiscard]] inline constexpr bool seqModeRecords(int choiceIdx) noexcept { return choiceIdx == 2; }

// {arp,seq}.sync_div choice (kSyncDiv) -> control::HostRate. The six kSyncDiv labels map
// 1:1, in order, onto the first six HostRate positions (Quarter..ThirtySecond). An
// out-of-range index defends to Sixteenth (the schema's common default).
[[nodiscard]] inline constexpr mw::control::HostRate hostRateForSyncDiv(int choiceIdx) noexcept {
    switch (choiceIdx) {
        case 0:  return mw::control::HostRate::Quarter;        // "1/4"
        case 1:  return mw::control::HostRate::Eighth;         // "1/8"
        case 2:  return mw::control::HostRate::EighthT;        // "1/8T"
        case 3:  return mw::control::HostRate::Sixteenth;      // "1/16"
        case 4:  return mw::control::HostRate::SixteenthT;     // "1/16T"
        case 5:  return mw::control::HostRate::ThirtySecond;   // "1/32"
        default: return mw::control::HostRate::Sixteenth;      // defensive
    }
}

} // namespace mw::cal::dispatch
