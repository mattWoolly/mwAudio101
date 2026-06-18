// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/SubOscillator.h — the 4013-derived phase-locked sub-oscillator
// (task 031). Realizes docs/design/01-dsp-oscillators.md §5.1-§5.6 / §10 and
// ADR-002 C4-C6.
//
// The sub is an EXACT integer divider of the VCO PHASE, never an independent
// oscillator [§5.1; ADR-002 C4; research/02 §3.4, §7.2]. A 4013 dual flip-flop,
// clocked from the sawtooth's single rising edge per VCO cycle, yields:
//   - Q1 (first flip-flop): VCO/2  -> the -1 octave square,
//   - Q2 (second flip-flop): VCO/4 -> the -2 octave square (toggles on Q1's rising edge).
// A 3-position shape control (the hardware S5 switch) selects the -1 oct square (Q1),
// the -2 oct square (Q2), or the -2 oct 25% pulse formed by the FIXED diode-OR of the
// two squares, out = Q1 OR Q2 (high 75% / low 25% at the -2 oct period) [§5.3, §5.4].
//
// Every transition the selected logic produces is a level STEP; PolyBLEP (default) or
// the minBLEP applicator (HQ / escalated) band-limits EVERY edge; no BLAMP [§5.5; C5,
// C6]. Each edge time is derived from the SAME master accumulator and scheduled in
// temporal order, so the sub edges are sample-accurate and drift-free relative to the
// saw wrap [§5.5; research/02 §7.2].
//
// The output is bipolar [-1, +1] PRE-level: the sub LEVEL (VR15) is applied by the
// source mixer, not here [§5.6, §8].
//
// Real-time invariants [§2.4; docs/design/00 §9.1; ADR-002 C11]:
//   - renderSample() is noexcept and performs no heap allocation and takes no locks;
//   - the minBLEP applicator ring is sized in prepare(), read/written arithmetically
//     only on the audio thread; the minBLEP table is read-only.

#pragma once

#include "OscAaMode.h"
#include "MinBlepTable.h"

namespace mw101::dsp {

// 3-way shape select matching the hardware S5 switch [§5.2; mw101.sub.mode is the
// schema-owned parameter ID — this enum is the DSP-internal mirror, ordered to match].
enum class SubShape { OctDownSquare = 0, TwoOctDownSquare = 1, TwoOctDown25Pulse = 2 };

class SubOscillator
{
public:
    void prepare (double sampleRate, const MinBlepTable& hqTable) noexcept;
    void reset() noexcept;                 // divider + applicator state -> 0

    void setShape (SubShape s) noexcept;   // from mw101.sub.mode (schema-owned ID)
    void setAaMode (OscAaMode m) noexcept; // structural; never per-sample [§2.2]

    // Drive from the master VCO each sample, exactly as docs/design/01 §7.3 sequences
    // it. `masterPhase` is the VCO's phase AFTER it advanced this sample, in [0, 1);
    // `wrapped` is true exactly on the sawtooth-wrap sample (the 4013 clock edge);
    // `freqHz` is the current VCO fundamental (the sub's edge timing tracks dt = f/fs).
    // Returns the bipolar, band-limited sub sample in [-1, +1], PRE-level.
    // noexcept, no allocation, no locks.
    [[nodiscard]] float renderSample (float masterPhase, bool wrapped, double freqHz) noexcept;

    // Introspection (test/§5 hooks).
    [[nodiscard]] SubShape  shape()  const noexcept { return shape_; }
    [[nodiscard]] OscAaMode aaMode() const noexcept { return aaMode_; }

private:
    // Naive (pre-band-limit) bipolar output level for the given divider state and shape.
    [[nodiscard]] static float levelFor (SubShape s, bool q1, bool q2) noexcept;

    bool q1_     = false;   // first flip-flop  (-1 oct), toggles each VCO wrap
    bool q2_     = false;   // second flip-flop (-2 oct), toggles each Q1 rising edge
    SubShape  shape_  = SubShape::OctDownSquare;
    OscAaMode aaMode_ = OscAaMode::PolyBlep;

    double sampleRate_ = 0.0;
    float  level_      = 0.0f;   // current naive held level (the value the steps settle to)

    MinBlepApplicator   blep_;            // used in HQ / escalated mode
    const MinBlepTable* hqTable_ = nullptr;
};

} // namespace mw101::dsp
