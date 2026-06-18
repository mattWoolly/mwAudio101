// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Lfo.h — the single LFO ("MODULATOR / LFO·CLK") public API + POD state
// (task 051). Declares ONLY the layout from docs/design/03 §3.2 (the four-position
// LfoShape selector) and §3.3 (the Lfo class). The waveform DSP, the arp/seq clock
// edge logic, and the white-noise source are OUT OF SCOPE here — owned by later
// .cpp tasks and the control-rate / noise docs respectively [§1.2; task 051].
//
// Real-time invariants this layout commits to (asserted by tests/unit/LfoHeaderTest):
//   - all state is POD; coefficients are precomputed in prepare(...) [§3.1; ADR-020 S14]
//   - tick()/setRateHz/setShape/setNoiseSource (and the rest) are noexcept hot paths
//   - no heap allocation and no locks on the audio thread [ADR-001; ADR-019 VT-01]
// The LFO advances on the control-rate tick (ticksPerControl), never a wall-clock
// timer [§3.1, §6.2].

#pragma once

#include <cstdint>

namespace mw101::dsp {

// FOUR positions only. Do NOT add Sine/Saw: the six-position selector is a
// software-reissue (SH-01A) artifact, not 1982 hardware [docs/design/03 §3.2;
// research/04 §3.2]. The smooth ("sine"-symbol) position is a triangle rounded
// toward sine by a fixed shaper, NOT a pure sine and NOT a separate sine core
// [§3.3]. This is a stepped/choice selector: NOT value-smoothed [ADR-020 S7].
enum class LfoShape : std::uint8_t {
    SmoothTri = 0,   // triangle, fixed-shaped "rounded toward sine"
    Square    = 1,
    Random    = 2,   // CPU+DAC digital pseudo sample/hold (research/04 §3.4)
    Noise     = 3    // white noise from the audio-path generator (research/04 §3.5)
};

class Lfo
{
public:
    void  prepare (double sampleRate, int controlRateDivider) noexcept;
    void  reset() noexcept;                        // phase -> 0, S/H reload

    void  setRateHz (float hz) noexcept;           // clamped to [0.1, 30] Hz
    void  setShape  (LfoShape) noexcept;
    void  resetPhaseOnKey() noexcept;              // clock-reset-on-keypress hook

    // Hot path: advance one control-rate tick. Returns bipolar value in [-1, 1]
    // for SmoothTri/Square/Random; Noise returns the bandlimited noise sample.
    float tick() noexcept;

    // True for one tick on the H->L cycle edge; consumers (Envelope LFO-trigger,
    // arp/seq clock owned elsewhere) read this. Edge logic for arp lives in the
    // control-rate doc; we only flag the oscillator's own cycle boundary.
    bool  cycleEdge() const noexcept { return edge_; }

    float value() const noexcept { return value_; }

    // White-noise source is injected, never owned here (research/04 §3.5, §1.2).
    void  setNoiseSource (const float* sharedNoiseSample) noexcept;

private:
    double sampleRate_   = 48000.0;
    int    ticksPerCtl_  = 1;
    float  phase_        = 0.0f;        // [0,1)
    float  phaseInc_     = 0.0f;        // per-tick increment from rate
    LfoShape shape_      = LfoShape::SmoothTri;
    float  value_        = 0.0f;
    bool   edge_         = false;
    float  shReg_        = 0.0f;        // Random sample/hold register
    std::uint64_t rngState_ = 0;        // POD PRNG for Random (seeded, det.)
    const float* noiseSample_ = nullptr;
};

} // namespace mw101::dsp
