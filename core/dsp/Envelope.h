// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Envelope.h — the one shared ADSR generator's public API + POD state.
// The surface is DECLARED here verbatim from docs/design/03-dsp-envelope-lfo-vca.md
// §2.2 (task 050); the coefficient/curve math and the trigger state machine are
// DEFINED out-of-line in Envelope.cpp (task 054) per §2.4 / §2.5, and the (PI)
// calibration constant VALUES live in core/calibration/EnvLfoVcaConstants.h
// (task 049).
//
// One ADSR per voice, shared across VCF cutoff / VCA gain / VCO pulse width; there
// is no separate filter or amp EG [docs/design/03 §2.1; research/04 §2.1]. The
// envelope produces a single normalized contour [0, 1]; per-destination depth
// scaling is applied downstream by ModRouting, not here [docs/design/03 §2.1].
//
// Real-time invariants [docs/design/03 §2.1; docs/design/00 §9; ADR-001, ADR-020
// S14]: all state is POD; coefficients are precomputed in prepare()/setParams(); no
// heap allocation and no locks on the audio thread; the hot path (tick) and the
// trigger entry points are noexcept. The envelope advances on the control-rate tick
// cadence and never reads a wall-clock timer [docs/design/03 §2.1; ADR-005].
//
// The audible ADSR contour (one-pole-toward-target per stage, attack overshoot,
// snap threshold) and the GATE / GATE+TRIG / LFO trigger logic are implemented in
// Envelope.cpp; no (PI) curve/time constant is inlined at a call site [docs/design/00
// §8.3; ADR-020 S13]. Only the const accessors are inline here (they are constexpr).

#pragma once

#include <cstdint>

namespace mw101::dsp {

enum class EnvStage : std::uint8_t { Idle, Attack, Decay, Sustain, Release };

// GATE+TRIG / GATE / LFO trigger source [research/04 §2.3]. The LFO mode is driven
// by Lfo's clock edge (docs/design/03 §3.6); the envelope only consumes triggers.
enum class EnvTrigMode : std::uint8_t { GateTrig, Gate, Lfo };

struct EnvParams           // snapshot pushed from the control-rate update; POD
{
    float attackSec  = 0.003f;   // see docs/design/03 §2.3 table for ranges
    float decaySec   = 0.060f;
    float sustain    = 0.7f;     // 0..1 fraction of attack peak
    float releaseSec = 0.100f;
    EnvTrigMode trig = EnvTrigMode::GateTrig;
    float curve      = 1.0f;     // (PI) shaping constant; see docs/design/03 §2.4
};

class Envelope
{
public:
    void  prepare (double sampleRate, int controlRateDivider) noexcept;
    void  reset() noexcept;                       // -> Idle, level 0

    void  setParams (const EnvParams&) noexcept;  // called on control-rate tick

    // Triggering. gateOn=true on note-on; the trig mode decides retrigger.
    void  noteOn  (bool legato) noexcept;         // honors GATE vs GATE+TRIG
    void  noteOff() noexcept;                      // -> Release
    void  clockTrigger() noexcept;                // LFO-mode retrigger (§3.6)

    // Hot path. Advances one control-rate tick worth of contour and returns the
    // current normalized level; the caller upsamples/holds across the block.
    float tick() noexcept;                         // returns level in [0,1]

    constexpr EnvStage stage()  const noexcept { return stage_; }
    constexpr bool     active() const noexcept { return stage_ != EnvStage::Idle; }
    constexpr float    level()  const noexcept { return level_; }

private:
    // Enter the Attack stage from the current level (the >1 overshoot target and the
    // attack coefficient). Shared by noteOn()/clockTrigger() per §2.5; defined in
    // Envelope.cpp. v1 re-attacks from the current level (no snap to 0) — a (PI)
    // choice / open validation gap [docs/design/03 §2.5; research/04 §5.3].
    void startAttack() noexcept;

    double sampleRate_      = 48000.0;
    int    ticksPerControl_ = 1;
    EnvStage stage_         = EnvStage::Idle;
    float  level_           = 0.0f;     // current normalized output
    float  target_          = 0.0f;     // stage asymptote (§2.4)
    float  coeff_           = 0.0f;     // per-tick one-pole coefficient
    float  sustain_         = 0.7f;
    float  curve_           = 1.0f;
    EnvTrigMode trig_       = EnvTrigMode::GateTrig;
    // precomputed per-stage coefficients refreshed in setParams() (§2.4)
    float  aCoeff_ = 0, dCoeff_ = 0, rCoeff_ = 0;
};

} // namespace mw101::dsp
