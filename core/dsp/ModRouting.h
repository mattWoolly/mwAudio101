// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/ModRouting.h — the fixed-routing combiner PODs + combine API.
// (PODs + entry points DECLARED by task 053; the combine MATH is IMPLEMENTED by
//  task 057 in core/dsp/ModRouting.cpp.)
//
// Realizes docs/design/03 §5.1: SH-101 modulation is FIXED routing with
// per-destination depths, NOT a matrix [research/04 §3.6, §6.2]. This header
// DECLARES the POD depth/velocity/mod-bus structs the env/lfo/vca subsystem shares,
// the per-destination ModContributions result POD, and the combiner entry points
// (prepare + per-tick combine). It owns NO parameter IDs (doc 06) and NO destination
// DSP (VCO/VCF docs).
//
// HISTORY: task 053 declared these structs and the ModRoutingCombiner entry points
// and deliberately deferred the velocity / mod-bus-LPF MATH (docs/design/03 §5.2,
// §5.3) to task 057. Task 057 implements that MATH as the body of
// ModRoutingCombiner::combine in core/dsp/ModRouting.cpp. The combine RESULT was
// widened from the task-053 pitch-only float stub to the ModContributions POD below:
// the §5.2 routing produces four per-destination contributions (pitch / cutoff / pw /
// vca) that a single float cannot carry, so the DECLARED return type is extended
// minimally here rather than spawning a parallel combiner class.
//
// Real-time invariants (ADR-001, ADR-019 VT-01, ADR-020 S14): all routing state is
// POD; the ModBus one-pole state is sized/reset in prepare (off the audio thread,
// no heap members); the per-tick combine hot path is noexcept and allocation-free.
// Every (PI) scalar (kModBusLpHz, kVelToVca, …) is resolved from
// core/calibration/EnvLfoVcaConstants.h in the .cpp, NEVER inlined [ADR-020 S13].

#pragma once

namespace mw101::dsp {

// Per-destination modulation depths (docs/design/03 §5.1). Values are set per
// control-rate tick from de-zippered params; the exact V/oct, Hz/V and %/V scalings
// are (PI) measurement-required open gaps owned by the calibration table and folded
// in downstream (docs/design/03 §5.3) — this struct just carries the scalars.
struct ModDepths {
    float lfoToPitch  = 0.0f;   // VCO MOD depth        (V/oct (PI) scaled)
    float lfoToCutoff = 0.0f;   // VCF MOD depth        (Hz/V  (PI) scaled)
    float lfoToPw     = 0.0f;   // PWM depth (LFO src)  (%/V   (PI) scaled)
    float lfoToVca    = 0.0f;   // tremolo depth
    float envToCutoff = 0.0f;   // VCF ENV depth
    float envToPw     = 0.0f;   // PWM depth (ENV src)
    float keyFollow   = 0.0f;   // VCF Key Follow 0..1
};

// Default velocity routing (docs/design/03 §5.1; ADR-016 R-2: velocity is ON by
// default, routed to VCA level + VCF cutoff amount). The faithful no-velocity pole
// is one switch away (enabled=false); doc 06 mints the on/off control.
struct VelocityRouting {
    bool  enabled        = true;   // out-of-box ON (ADR-016 R-2)
    float toVcaAmount    = 1.0f;   // velocity -> VCA level   (PI) scale
    float toCutoffAmount = 1.0f;   // velocity -> VCF cutoff  (PI) scale
};

// Modulation-bus state (docs/design/03 §5.1, §3.5). A fixed (PI) ~16 kHz one-pole
// (kModBusLpHz) on the modulation signals; POD, sized in prepare (ADR-020 S14) —
// no heap members so the audio thread never allocates.
struct ModBus {
    float lpState = 0.0f;   // kModBusLpHz one-pole running state (research/04 §3.5)
    float lpCoeff = 0.0f;   // one-pole coefficient, derived in prepare
};

// Per-tick combine result (docs/design/03 §5.1, §5.2). POD; the downstream stage
// docs consume each field: pitchMod -> VCO doc, cutoffMod -> VCF doc, pwMod -> VCO
// PWM, vcaControl -> VCA doc. This struct supplies contributions only; no transfer
// function lives here (those are owned by their own docs). It is the widened result
// type of ModRoutingCombiner::combine (task 057): the §5.2 routing yields four
// per-destination contributions a single float cannot carry.
struct ModContributions {
    float pitchMod   = 0.0f;   // VCO pitch modulation (LFO * lfoToPitch)
    float cutoffMod  = 0.0f;   // VCF cutoff amount: env + LFO + velocity (additive)
    float pwMod      = 0.0f;   // pulse-width modulation: env + LFO sources
    float vcaControl = 0.0f;   // VCA control: baseAmp (ENV/GATE + tremolo) * velocity
};

// Fixed-routing combiner (docs/design/03 §5.1). Scales the one envelope and the one
// LFO value by the per-destination depths, applies the fixed (PI) kModBusLpHz
// mod-bus one-pole LPF (§3.5), and folds in the default velocity routing into VCA
// level + VCF cutoff amount (§5.2; velocity ON by default per ADR-016 R-2). The
// combiner OWNS the ModBus state so prepare can size/reset it off the audio thread.
//
// The combine MATH and the prepare-time coefficient derivation live out-of-line in
// core/dsp/ModRouting.cpp (task 057), where the (PI) calibration constants are read.
class ModRoutingCombiner {
public:
    ModRoutingCombiner() noexcept = default;

    // Size/reset the routing state off the audio thread (ADR-020 S14). Resets the
    // ModBus one-pole running state to zero and derives its coefficient from the
    // (PI) kModBusLpHz corner and the sample rate. No heap allocation, no locks.
    void prepare(double sampleRate) noexcept;

    // Reset only the running state (e.g. on note-on / voice steal); leaves the
    // coefficient sized by prepare. Hot-path-adjacent, noexcept, no allocation.
    void reset() noexcept;

    // Per-tick combine entry point (hot path). Runs the env/LFO through the mod-bus
    // LPF, scales by the per-destination depths (§5.1), and assembles the
    // per-destination contributions including the §5.2 velocity routing.
    //   envLevel : the ADSR contour level for this tick, [0,1].
    //   lfoValue : the bipolar LFO value for this tick, [-1,1].
    //   velNorm  : the shaped, normalized velocity for this note, [0,1].
    [[nodiscard]] ModContributions combine(const ModDepths& depths,
                                           const VelocityRouting& vel,
                                           float envLevel,
                                           float lfoValue,
                                           float velNorm) noexcept;

    [[nodiscard]] const ModBus& modBus() const noexcept { return bus_; }

private:
    double sampleRate_ = 48000.0;
    ModBus bus_{};
};

} // namespace mw101::dsp
