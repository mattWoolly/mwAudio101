// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/HostSmokeConstants.h — (PI) constants for the headless host-smoke
// matrix (task 140). Realizes docs/design/09 §2.1 (the per-format validator matrix) and
// docs/design/11 §5.2 / ADR-023 V12 (the blessed sample-rate set).
//
// The host-smoke test drives the real MwAudioProcessor through the full host lifecycle
// with NO audio device, across the matrix of (sample rate x block size) below. None of
// these literals are inlined at the call site — they live here as TUNABLE DEFAULTS (PI),
// exactly like every other cross-module constant set [docs/design/06 §3.10; docs/design/
// 11 §9 F-15; ADR-008 §1].
//
// NB: this header is JUCE-free (plain scalars), so it costs the JUCE-free core nothing;
// it is consumed only by tests/plugin/HostSmokeTest.cpp (which links JUCE).

#pragma once

#include <array>
#include <cstddef>

namespace mw::cal::host_smoke {

// The BLESSED sample-rate set the golden corpora are keyed against [docs/design/11 §5.2;
// ADR-023 V12 — {44100, 48000, 88200, 96000} Hz]. The host-smoke matrix prepares the
// processor at each so the headless lifecycle is exercised at every blessed rate.
inline constexpr std::array<double, 4> kBlessedSampleRatesHz{ 44100.0, 48000.0, 88200.0, 96000.0 };

// Representative host block sizes spanning a tiny block (a worst-case automation-dense
// host), a common DAW default, and a large offline-render block. The lifecycle must hold
// across all of them (the §3.2 event surface is sized per-block in prepare) [docs/design/
// 09 §1.3/§3.2]. (PI) — a pragmatic spread, not a measured spec.
inline constexpr std::array<int, 3> kBlockSizes{ 32, 512, 2048 };

// Number of processBlock calls per (rate, block) cell. A handful of blocks proves the
// steady-state render path holds without the test running long; enough to cover a noteOn
// block, a sustaining block, and a noteOff/silence block. (PI).
inline constexpr int kBlocksPerCell = 4;

// The output channel count the headless smoke drives (stereo out; synth has no input bus).
inline constexpr int kNumOutputChannels = 2;

} // namespace mw::cal::host_smoke
