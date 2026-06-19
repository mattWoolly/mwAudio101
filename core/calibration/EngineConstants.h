// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/EngineConstants.h — the (PI) engine-assembly constants for the
// Engine seam's internal sub-blocking (task 118).
//
// This header EXTENDS the central calibration table (mw::cal) with the constants the
// Engine::process internal chunker needs, rather than inlining a literal at the call
// site [docs/design/00 §1.2, §4.4; AGENTS.md "(PI) discipline"]. It #includes
// Calibration.h so a single include pulls the whole (PI) table; the new constants live
// in a new mw::cal::engine namespace so this file conflicts with no sibling task.
//
// kRenderBlock is the §4.4 fixed-size internal render-chunk cap (~32 frames): process
// splits the host block at MIDI/event sample-offsets and renders each segment in
// fixed-size internal chunks capped at this value, modeling the SH-101's coarse
// control-loop cadence [docs/design/00 §4.4; ADR-001 Decision, C6]. It is a (PI)
// pragmatic invention — NOT a measured spec — so it is centralized here.

#pragma once

#include "Calibration.h"

namespace mw::cal {

// Engine-assembly (PI) constants for the Engine::process internal sub-blocking
// (§4.4). The chunk cap aligns the control-rate parameter ticks to chunk boundaries.
namespace engine {

// The fixed internal render-chunk cap, in frames [docs/design/00 §4.4; ADR-001 C6].
// (PI) ~32 frames — a pragmatic default for the coarse control-loop cadence. A host
// block is split at MIDI/event offsets first, then each resulting segment is further
// capped at kRenderBlock so no internal audio-rate render runs longer than this.
inline constexpr int kRenderBlock = 32;  // (PI) — internal render-chunk frame cap

static_assert(kRenderBlock > 0,
              "EngineConstants: kRenderBlock MUST be a positive frame count (§4.4).");

} // namespace engine

} // namespace mw::cal
