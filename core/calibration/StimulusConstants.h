// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/StimulusConstants.h — the (PI) defaults for the offline golden
// render-input stimuli (task 042).
//
// Per the conflict-avoidance rule for the parallel development fleet, this module's
// constants land in a dedicated header that #includes (and extends the mw::cal
// namespace of) the shared core/calibration/Calibration.h, rather than being
// appended directly to it [AGENTS.md "ADRs & decisions"; docs/design/00 §8.3].
//
// Every value here is (PI) — a pragmatic invention chosen so the canonical stimulus
// builders (sustained note / gate burst / randomized sweep) produce musically
// sensible, deterministic event streams. There is NO measured-oracle backing: a
// stimulus is a synthetic test signal, not an SH-101 fact [docs/design/11 §1.3, §5.4].
// These feed the OFFLINE render harness only; no audio-thread / RT concern is in play
// [docs/design/11 §2.2].

#pragma once

#include <cstdint>

#include "Calibration.h"

namespace mw::cal::stim {

// --- Canonical pitch / velocity / CC poles ----------------------------------------
// A single middle-C-ish note number and a mezzo-forte velocity so the sustained-note
// and gate-burst builders are reproducible without each call site inventing literals.
inline constexpr std::int16_t kDefaultNote     = 60;     // (PI) — middle C (MIDI 60)
inline constexpr float        kDefaultVelocity = 0.8f;   // (PI) — mf, normalized [0,1]

// The canonical CC the sweep builder animates (a filter-cutoff-style controller). The
// numeric controller index is a stimulus-side (PI) choice; the engine-side CC->param
// mapping is owned by docs/design/06/09 and is NOT referenced here [task 042 Out-of-scope].
inline constexpr float        kSweepCcData0    = 74.0f;  // (PI) — CC#74-style controller index

// --- Gate-burst shape --------------------------------------------------------------
inline constexpr int kGateBurstCount       = 8;      // (PI) — number of on/off note pairs
inline constexpr int kGateBurstOnFrames    = 480;    // (PI) — gate-ON duration per pulse
inline constexpr int kGateBurstOffFrames   = 480;    // (PI) — gate-OFF gap per pulse

// --- Sustained-note shape ----------------------------------------------------------
inline constexpr int kSustainOnsetFrame    = 0;      // (PI) — note-on at block start
inline constexpr int kSustainReleaseMargin = 480;    // (PI) — note-off this many frames before end

// --- Randomized sweep shape --------------------------------------------------------
inline constexpr int kSweepSteps           = 16;     // (PI) — number of randomized CC steps

// --- Default durations (frames) ----------------------------------------------------
inline constexpr int kDefaultDurationFrames = 8192;  // (PI) — ~enough to settle a tail

} // namespace mw::cal::stim
