// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/SequencerRoutingConstants.h — the (PI) seam constants the Engine
// uses to route SequencerEngine note output into the VoiceManager's keyboard note path
// (task 118c).
//
// The SequencerEngine emits ControlEvents whose `pitch` field is in the control-core's
// own pitch space: the StepSequencer stores a 6-bit DAC pitch (0..63 semitone counts,
// doc 05 §6.2) and the Arpeggiator returns a held-key index (0..31, the 32-key bitmap,
// doc 05 §5.1). The MONO/UNISON voice path (the sole KeyAssigner) is keyed on full-range
// MIDI notes 0..127 (doc 04 §5.2). This header centralizes the (PI) base MIDI note that
// bridges the two spaces so the Engine inlines no literal [docs/design/00 §1.2;
// AGENTS.md "(PI) discipline"]. The same base is used in BOTH directions: held MIDI
// notes are folded into the arp/trigger 0..31 key space by subtracting it, and an
// emitted seq/arp pitch is mapped back to a MIDI note by adding it — so an arp step
// recovers the exact MIDI note that was played.
//
// This is a pragmatic invention (NOT measured circuit behavior): the SH-101's keyboard
// addressing and the control-core pitch base are owned by doc 06's range/transpose
// params, which the Engine does not reach into here (task 118c is core-only wiring). The
// base is chosen so the low edge of the 6-bit range sits near the bottom of the SH-101's
// keyboard so the full 0..63 step range stays inside the MIDI 0..127 range.

#pragma once

#include "Calibration.h"

namespace mw::cal::seqroute {

// The base MIDI note the arp/seq pitch space is folded onto (PI). MIDI note 36 == C2,
// near the bottom of the SH-101's 32-key bed; adding the 6-bit (0..63) seq pitch keeps
// the whole range inside MIDI 0..127, and adding the 0..31 arp key likewise [PI].
inline constexpr int kSeqVoiceBaseMidi = 36;

static_assert(kSeqVoiceBaseMidi >= 0 && kSeqVoiceBaseMidi + 63 <= 127,
              "SequencerRouting: the 6-bit seq pitch range MUST stay inside MIDI 0..127.");

} // namespace mw::cal::seqroute
