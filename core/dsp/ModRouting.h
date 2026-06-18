// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/ModRouting.h — the fixed-routing combiner PODs (task 053).
//
// Realizes docs/design/03 §5.1: SH-101 modulation is FIXED routing with
// per-destination depths, NOT a matrix [research/04 §3.6, §6.2]. This header
// DECLARES the POD depth/velocity/mod-bus structs the env/lfo/vca subsystem shares,
// plus the combiner entry points (prepare + per-tick combine). It owns NO parameter
// IDs (doc 06) and NO destination DSP (VCO/VCF docs).
//
// SCOPE (task 053): declare the structs and the entry points. The velocity / mod-bus
// LPF MATH is task 12's scope (docs/design/03 §5.2, §5.3) and is intentionally NOT
// implemented here — the combine entry point assembles only the contributions that
// need no calibrated (PI) scaling.
//
// Real-time invariants (ADR-001, ADR-019 VT-01, ADR-020 S14): all routing state is
// POD; the ModBus one-pole state is sized/reset in prepare (off the audio thread,
// no heap members); the per-tick combine hot path is noexcept and allocation-free.

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

// Fixed-routing combiner (docs/design/03 §5.1). Scales the one envelope and the one
// LFO value by the per-destination depths and (downstream, task 12) folds in the
// default velocity routing and the mod-bus LPF. The combiner OWNS the ModBus state
// so prepare can size/reset it off the audio thread.
class ModRoutingCombiner {
public:
    ModRoutingCombiner() noexcept = default;

    // Size/reset the routing state off the audio thread (ADR-020 S14). Resets the
    // ModBus one-pole state to zero and derives its coefficient from the sample
    // rate. No heap allocation, no locks. The exact kModBusLpHz corner is a (PI)
    // calibration constant folded in by task 12; until then prepare establishes a
    // benign, valid (0 <= lpCoeff < 1) reset state.
    void prepare(double sampleRate) noexcept {
        sampleRate_   = (sampleRate > 0.0) ? sampleRate : 48000.0;
        bus_.lpState  = 0.0f;
        bus_.lpCoeff  = 0.0f;   // (PI) kModBusLpHz coefficient wired by task 12
    }

    // Reset only the running state (e.g. on note-on / voice steal); leaves the
    // coefficient sized by prepare. Hot-path-adjacent, noexcept, no allocation.
    void reset() noexcept { bus_.lpState = 0.0f; }

    // Per-tick combine entry point (hot path). Returns the VCO-pitch modulation
    // contribution (the single MOD depth, docs/design/03 §3.6). The velocity /
    // mod-bus-LPF MATH and the other destinations' assembly are task 12's scope
    // (docs/design/03 §5.2, §5.3); here the entry point performs only the plain
    // depth scaling that needs no calibrated (PI) constant.
    [[nodiscard]] float combine(const ModDepths& depths,
                                const VelocityRouting& /*vel*/,
                                float /*envLevel*/,
                                float lfoValue,
                                float /*velNorm*/) noexcept {
        return depths.lfoToPitch * lfoValue;
    }

    [[nodiscard]] const ModBus& modBus() const noexcept { return bus_; }

private:
    double sampleRate_ = 48000.0;
    ModBus bus_{};
};

} // namespace mw101::dsp
