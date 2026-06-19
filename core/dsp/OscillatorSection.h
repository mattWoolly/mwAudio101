// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/OscillatorSection.h — the per-voice oscillator-section owner (task 032).
// Realizes docs/design/01-dsp-oscillators.md §7.1-§7.3, §2.3, §8, §10 and
// ADR-002 C4/C7/C9, ADR-018 Q-table/Q5/Q6.
//
// A per-voice aggregate that OWNS one Oscillator (VCO), one SubOscillator, and one
// NoiseSource, sequences them with the load-bearing per-sample ordering of §7.3, and
// emits the four raw band-limited source signals for the source mixer. It holds the
// reference to the shared, read-only MinBlepTable that the engine builds once and
// shares across voices [§7.1].
//
// Per-sample ordering (load-bearing) [§7.3; ADR-002 C4; research/02 §7.2]:
//   1. vco.renderSample() advances the master phase ONCE and records the saw wrap;
//   2. the effective AA mode for this block is the requested tier OR minBLEP if the
//      VCO fundamental escalated above kHqEscalationHz (decided per block, never per
//      sample) — the VCO already folds this into effectiveAaMode(); the section pushes
//      that single decision onto the sub so the WHOLE section escalates coherently;
//   3. sub.renderSample(vco.phase(), vco.wrappedThisSample(), vco.frequencyHz())
//      clocks the 4013 divider off the VCO's saw wrap so it is exactly phase-locked;
//   4. noise.renderSample();
//   5. return { saw, pulse, sub, noise }.
//
// HQ auto-escalation (§2.3; ADR-002 C9; ADR-018 Q6): a voice whose VCO fundamental
// exceeds kHqEscalationHz(fs) switches from the closed-form PolyBLEP residual to the
// minBLEP applicator while the condition holds. This is internal model behavior keyed
// off pitch (the Valimaki limit), NEVER a user parameter — Controls carries only the
// tier aaMode, no escalation toggle. The threshold is read from the calibration header
// (mw::cal::vco::hqEscalationHzAt), not duplicated [§2.3, §10].
//
// Output contract (§8; ADR-002 C7): all four sources are bipolar floats nominally in
// [-1, +1] (noise [-1, 1)), pre-level and pre-mix. The section runs at BASE sample
// rate only; it never oversamples and never reads the filter's oversample stride.
//
// Real-time invariants [§2.4; docs/design/00 §9.1; ADR-002 C11]:
//   - renderSample()/reset()/setControls()/effectiveAaMode()/fundamentalHz() are
//     noexcept and perform no heap allocation and take no locks;
//   - all sizing/allocation happens off the audio thread in prepare() (the per-voice
//     minBLEP applicator rings inside the VCO and sub are pre-sized there from the
//     shared read-only table);
//   - the AA mode is selected in prepare()/setControls() only; the per-sample HQ
//     auto-escalation is keyed off the per-block fundamental, not a per-sample read
//     [§2.2-§2.3; ADR-018 Q5].

#pragma once

#include "Oscillator.h"        // VCO: master phase core + band-limited saw/pulse (029/030)
#include "SubOscillator.h"     // 4013 phase-locked divider + diode-OR 25% pulse (031)
#include "NoiseSource.h"       // xorshift32 white noise, [-1, 1) (028)
#include "OscAaMode.h"         // canonical mw101::dsp::OscAaMode (031)
#include "MinBlepTable.h"      // shared read-only minBLEP table + applicator (027)

namespace mw101::dsp {

class OscillatorSection
{
public:
    // Off-the-audio-thread setup; the ONLY place allocation may happen. `hqTable` is
    // the shared, read-only minBLEP table the engine builds once and shares across all
    // voices [§7.1, §2.4; ADR-002 C8]. The VCO and sub pre-size their per-voice minBLEP
    // applicator rings from it here.
    void prepare (double sampleRate, const MinBlepTable& hqTable) noexcept;

    // Clear all source state to a known start and reseed the noise PRNG for this voice.
    // No allocation [docs/design/00 §5.5]. A zero seed is folded to a nonzero fallback
    // by NoiseSource (xorshift cannot escape 0).
    void reset (std::uint64_t noiseSeed) noexcept;

    // Per-voice control snapshot, supplied PER BLOCK (not per sample) [§7.2]. The
    // tier `aaMode` (derived from mw101.quality per ADR-018) is applied to ALL sources;
    // escalation is decided internally off the VCO fundamental and is NOT a field here.
    struct Controls
    {
        OscControls vco;        // §4.2 (summed pitch CV, footage, PWM CV, tier aaMode)
        SubShape    subShape;   // 4013 shape select (mw101.sub.mode, schema-owned ID)
        OscAaMode   aaMode;     // tier, derived from mw101.quality; applied to all sources
    };

    // Per-block control update. Re-derives the VCO freq/dt/shape (which folds in the
    // HQ auto-escalation off the fundamental) and pushes the resulting effective AA
    // mode onto the sub so the whole section escalates coherently. noexcept, no
    // allocation, no locks. NEVER called per sample on the audio thread [§2.2; Q5].
    void setControls (const Controls& c) noexcept;

    // The four raw band-limited source signals, bipolar, pre-level/pre-mix [§8].
    struct Sources { float saw; float pulse; float sub; float noise; };

    // Render one sample with the load-bearing §7.3 ordering: VCO first (advances the
    // master phase), then the phase-locked sub, then noise. noexcept, no allocation.
    [[nodiscard]] Sources renderSample() noexcept;

    // The current VCO fundamental (for the escalation crossover) [§4.2, §2.3].
    [[nodiscard]] double fundamentalHz() const noexcept { return vco_.frequencyHz(); }

    // The AA mode actually applied to the whole section this block: the requested tier
    // OR minBLEP if the fundamental escalated above kHqEscalationHz(fs) [§2.3; C9].
    [[nodiscard]] OscAaMode effectiveAaMode() const noexcept { return vco_.effectiveAaMode(); }

private:
    Oscillator          vco_;
    SubOscillator       sub_;
    NoiseSource         noise_;
    const MinBlepTable* hqTable_ = nullptr;   // shared, read-only (non-owning)
};

} // namespace mw101::dsp
