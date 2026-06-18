// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Vca.h — BA662A-class OTA VCA public API + POD state (task 052).
//
// Realizes docs/design/03 §4.1 (responsibilities / RT invariants) and §4.2 (class
// layout) verbatim: a linear-control-current OTA model `out = taper(control) * in`
// whose control is the selected amplitude source (ENV or GATE) summed with an LFO
// tremolo contribution. The BA662 internal architecture is reverse-engineered, not
// datasheet-documented [research/04 §4.2]; this model is behavioral (transfer
// taper), NOT a transistor-level netlist.
//
// This task declares the API surface and the POD state ONLY:
//   - the ENV/GATE-only VcaMode enum (HOLD is an AMSynths-clone extension that is
//     NOT confirmed on original hardware [research/04 §4.4, §5.3]; v1 models the
//     documented ENV/GATE pair only — do NOT add HOLD without an ADR);
//   - prepare / reset / setMode / setDrive / process / processBlock.
// The taper/tanh math is owned by task 009 (§4.3) and the anti-thump fade by task
// 010 (§4.6); those tasks define the method bodies. The (PI) calibration constants
// (kVcaTaperExp, kVcaOtaDrive, kVcaAntiThumpMs, kVcaOffsetNull — §4.3/§4.6) live in
// core/calibration/ per [ADR-020] S13, not inlined here.
//
// RT invariants [ADR-001, ADR-019 VT-01, ADR-020 S14]: all state is POD; coeffs are
// precomputed in prepare(); no heap allocation and no locks on the audio thread;
// process() / processBlock() are noexcept hot paths.

#pragma once

#include <cstdint>

namespace mw101::dsp {

// VCA amplitude source switch (research/04 §4.4). HOLD is an AMSynths-clone
// extension and is NOT confirmed on original hardware (research/04 §4.4, §5.3);
// v1 models the documented ENV/GATE pair only. Do not add HOLD without an ADR.
enum class VcaMode : std::uint8_t { Env = 0, Gate = 1 };

class Vca
{
public:
    void  prepare (double sampleRate) noexcept;
    void  reset() noexcept;

    void  setMode (VcaMode) noexcept;
    void  setDrive (float driveNorm) noexcept;     // optional OTA character (§4.5)

    // control = amplitude source (ENV level or GATE 0/1) summed with LFO tremolo,
    // velocity already folded in by ModRouting (§5). Range [0,1].
    // Returns the gained sample; processBlock variant for the hot loop.
    float process (float in, float control) noexcept;
    void  processBlock (float* buffer, const float* control, int n) noexcept;

private:
    double sampleRate_ = 48000.0;
    VcaMode mode_      = VcaMode::Env;
    float  drive_      = 0.0f;
    // anti-thump state (§4.6)
    float  offsetNull_ = 0.0f;
    float  gateFade_   = 0.0f;        // smoothed gate open/close
    float  gateFadeCoeff_ = 0.0f;
};

} // namespace mw101::dsp
