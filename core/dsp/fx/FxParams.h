// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/FxParams.h — the POD FxParams snapshot consumed lock-free by the FX
// audio thread (task 088).
//
// Realizes docs/design/07 §7 (FxParams snapshot layout) and §3.1 (the FxChain
// interface that reads it). plugin/ fills this from the APVTS (parameter IDs from
// docs/design/06 §2) on the message/control thread and publishes it via
// FxChain::setParams; the audio thread reads the active snapshot lock-free
// (double-buffer / atomic pointer swap) [ADR-010 FX-10].
//
// Every field is a plain decoded value — no parameter IDs, ranges, skews, or APVTS
// types live here (those are owned by docs/design/06 §2 per ADR-008). The struct is
// a trivially-copyable POD so the cross-thread publish is a flat memcpy with no
// allocation and no locks on the audio thread [docs/design/07 §3.1, §7; ADR-010
// FX-10].
//
// Engine defaults are FX OFF / dry: masterBypass==true, monoOutput==false,
// drive.on==false, chorus.mode==Off(0), delay.on==false, hostBpm==120.0
// [docs/design/07 §7, §8; ADR-010 FX-13, FX-9].

#pragma once

#include <type_traits>

namespace mw::fx {

// Plain decoded FX parameter snapshot. See file header for the threading/ID
// contract. Field layout is fixed verbatim by docs/design/07 §7.
struct FxParams
{
    // Drive stage decoded values [docs/design/07 §4.6]. `on` is the per-block
    // bypass early-out; amount/tone/output are normalized decoded knob values.
    struct DriveP  { bool on; float amount, tone, output; };

    // Chorus stage decoded values [docs/design/07 §5.1.5]. `mode` is the
    // Chorus::Mode enum decoded to int (Off=0, I=1, II=2, I+II=3).
    struct ChorusP { int mode; float rate, depth, width, mix; }; // mode: enum int

    // Delay stage decoded values [docs/design/07 §5.2.7]. `division` is the
    // tempo-sync note-division choice decoded to int (order owned by doc 06).
    struct DelayP  { bool on, sync, pingpong;
                     int division; float timeMs, feedback, damp, width, mix; };

    bool   masterBypass = true;   // engine default OFF -> bypass [ADR-010 FX-13]
    bool   monoOutput   = false;  // global Mono Output collapse [ADR-010 FX-9]
    double hostBpm      = 120.0;  // from plugin/ AudioPlayHead [ADR-001]

    DriveP  drive  {};
    ChorusP chorus {};
    DelayP  delay  {};
};

// The cross-thread publish (double-buffer / atomic pointer swap, §3.1) relies on
// FxParams being a flat, copyable value type — assert it at compile time so the
// lock-free, allocation-free contract cannot silently regress [docs/design/07 §7].
static_assert(std::is_trivially_copyable_v<FxParams>,
              "FxParams must be a trivially-copyable POD (docs/design/07 §7).");

} // namespace mw::fx
