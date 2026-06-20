// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/state/SeqPatternCodec.h — read the <extras><seq> 100-step pattern back out
// of a canonical MW101_STATE ValueTree into the JUCE-free mw::state::Extras POD
// (task 111c). The exact inverse of StateSerializer::captureState's <seq> write
// (note/gate/tie/rest; NO accent) [docs/design/06 §5.5; ADR-025].
//
// This is the read side that lets the processor restore the EDITED sequencer pattern
// on setStateInformation so it both feeds the audio-thread handoff and round-trips
// through capture/recoverState. Message-thread only: it parses a juce::ValueTree
// (never touched by the audio thread) [docs/design/06 §5.2; ADR-008 C19].

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/Extras.h"   // mwcore (JUCE-free): mw::state::Extras / SeqStep / kMaxSeqSteps

namespace mw::plugin::state {

// Decode the <extras><seq> pattern of a canonical tree into an Extras POD. Missing /
// malformed nodes yield a default-constructed Extras (empty pattern); stepCount is
// clamped to [0, kMaxSeqSteps] and only that many <step> children are read, so a
// recovered tree never overflows the fixed-capacity POD [docs/design/06 §5.4; ADR-021
// fallback discipline]. arpLatch / driftSeed / seedLocked are read from <extras> too,
// so a full <extras> round-trips. Message-thread only.
[[nodiscard]] mw::state::Extras readSeqPattern(const juce::ValueTree& canonical);

} // namespace mw::plugin::state
