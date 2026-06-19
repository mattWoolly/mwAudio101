// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/OscillatorSection.cpp — implementation of the per-voice oscillator-section
// owner (task 032). See OscillatorSection.h. Realizes
// docs/design/01-dsp-oscillators.md §7.1-§7.3, §2.3, §8, §10 and
// ADR-002 C4/C7/C9, ADR-018 Q-table/Q5/Q6.

#include "OscillatorSection.h"

namespace mw101::dsp {

void OscillatorSection::prepare (double sampleRate, const MinBlepTable& hqTable) noexcept
{
    hqTable_ = &hqTable;            // shared, read-only across voices [§7.1; ADR-002 C8]

    // Each source pre-sizes its per-voice minBLEP applicator ring OFF the audio thread,
    // the ONLY place allocation may happen [§2.4; ADR-002 C8, C11]. The VCO accepts the
    // table by pointer (band-limiting hook), the sub by reference.
    vco_.prepare (sampleRate, hqTable_);
    sub_.prepare (sampleRate, hqTable);
    noise_.prepare (sampleRate);
}

void OscillatorSection::reset (std::uint64_t noiseSeed) noexcept
{
    vco_.reset();
    sub_.reset();
    noise_.reset (noiseSeed);      // per-voice reseed; xorshift folds a zero seed away
}

void OscillatorSection::setControls (const Controls& c) noexcept
{
    // The tier AA mode is applied to ALL sources [§7.2]. Force the VCO's control-block
    // aaMode to the section tier so a single quality decision drives the whole section.
    OscControls vcoControls = c.vco;
    vcoControls.aaMode = c.aaMode;
    vco_.setControls (vcoControls);   // re-derives freq/dt/shape AND the escalation decision

    // The VCO has already folded the HQ auto-escalation (off its fundamental vs the
    // sample-rate-scaled kHqEscalationHz, §2.3) into effectiveAaMode(). Push that ONE
    // per-block decision onto the sub so the whole section escalates coherently — the
    // sub band-limits its 4013 edges with the same applicator the VCO uses [§7.3; C9].
    sub_.setShape  (c.subShape);
    sub_.setAaMode (vco_.effectiveAaMode());
}

OscillatorSection::Sources OscillatorSection::renderSample() noexcept
{
    // §7.3 load-bearing ordering. The VCO MUST advance the master phase exactly once
    // before the sub reads it, so the sub's 4013 clock edge is consistent with the saw
    // wrap within the same sample [ADR-002 C4; research/02 §7.2].
    //
    // 1. VCO first: advances the master phase, records the wrap, emits saw + pulse.
    const Oscillator::Output v = vco_.renderSample();

    // 2. The effective AA mode for this block was decided in setControls() (the tier
    //    OR escalated minBLEP off the fundamental); it is NOT recomputed per sample.
    //
    // 3. Sub: clocks the 4013 divider off the VCO's saw wrap using the SAME advanced
    //    master phase, so it is exactly phase-locked and drift-free [§7.3; C4].
    const float s = sub_.renderSample (static_cast<float> (vco_.phase()),
                                       vco_.wrappedThisSample(),
                                       vco_.frequencyHz());

    // 4. Noise: independent per-voice white-noise stream, base sample rate.
    const float n = noise_.renderSample();

    // 5. Four raw bipolar sources, pre-level/pre-mix; the mixer (§9) applies the level
    //    sliders and sums. The section never oversamples [§8; ADR-002 C7].
    return Sources{ v.saw, v.pulse, s, n };
}

} // namespace mw101::dsp
