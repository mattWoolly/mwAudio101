// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Envelope.cpp — the one shared ADSR generator: prepare()/setParams()
// coefficient precompute and the Attack/Decay/Sustain/Release one-pole contour
// advancing on the control-rate tick (task 054). See Envelope.h and
// docs/design/03-dsp-envelope-lfo-vca.md §2.4 (segment curve law), §2.5 (trigger
// state machine), §2.3 (ranges) and §6.2 (control-rate cadence).
//
// Segment shape is INFERRED, UNMEASURED theory (exponential-RC charge/discharge)
// [docs/design/03 §2.4; research/04 §2.6, §5.1] — modeled as a per-stage one-pole
// approach toward a per-stage target with the law:
//   coeff  = exp(-1 / (max(T, kEnvTimeMin) * fc * kEnvTimeScale))      (per stage)
//   level += (target - level) * (1 - coeff)                            (per tick)
// where fc = sampleRate / ticksPerControl is the control-tick rate (§2.4, §6.2).
//
// Real-time invariants [docs/design/03 §2.1; docs/design/00 §9; ADR-001]: all the
// transcendental work (std::exp) happens in prepare()/setParams() only; tick() and
// the trigger entry points touch only POD members, do no heap allocation, take no
// lock, and are noexcept. Every (PI) constant resolves from
// core/calibration/EnvLfoVcaConstants.h (the mw::cal::env namespace); no literal is
// inlined at a call site here [docs/design/00 §8.3; ADR-020 S13].

#include "dsp/Envelope.h"

#include <algorithm>
#include <cmath>

#include "calibration/EnvLfoVcaConstants.h"

namespace mw101::dsp {

namespace {

// Per-stage one-pole coefficient for a segment time T (seconds) at control-tick rate
// fc (Hz): coeff = exp(-1 / (max(T, kEnvTimeMin) * fc * kEnvTimeScale)) [§2.4].
// Off-tick (prepare/setParams) only — the only place a transcendental runs.
[[nodiscard]] float stageCoeff(double timeSec, double fc) noexcept {
    using namespace mw::cal::env;
    if (fc <= 0.0) return 0.0f;
    const double t = std::max(timeSec, static_cast<double>(kEnvTimeMin));
    const double denom = t * fc * static_cast<double>(kEnvTimeScale);
    if (denom <= 0.0) return 0.0f;
    return static_cast<float>(std::exp(-1.0 / denom));
}

} // namespace

void Envelope::prepare(double sampleRate, int controlRateDivider) noexcept {
    sampleRate_      = sampleRate;
    ticksPerControl_ = (controlRateDivider > 0) ? controlRateDivider : 1;

    // Seed the per-stage coefficients to a well-defined floored value so they are
    // valid before the first setParams() call (the caller is expected to call
    // setParams() on each control-rate tick, which recomputes them against fc).
    // The transcendental is confined to here / setParams() — never tick() (§2.1).
    const double fc = sampleRate_ / static_cast<double>(ticksPerControl_);
    aCoeff_ = stageCoeff(0.0, fc);   // floored by kEnvTimeMin inside stageCoeff
    dCoeff_ = aCoeff_;
    rCoeff_ = aCoeff_;
    reset();
}

void Envelope::reset() noexcept {
    stage_  = EnvStage::Idle;
    level_  = 0.0f;
    target_ = 0.0f;
}

void Envelope::setParams(const EnvParams& p) noexcept {
    using namespace mw::cal::env;
    const double fc = sampleRate_ / static_cast<double>(ticksPerControl_);

    // Per-stage one-pole coefficients precomputed here (the only transcendental
    // site); tick() consumes them with no math beyond a multiply-add (§2.1, §2.4).
    aCoeff_ = stageCoeff(static_cast<double>(p.attackSec),  fc);
    dCoeff_ = stageCoeff(static_cast<double>(p.decaySec),   fc);
    rCoeff_ = stageCoeff(static_cast<double>(p.releaseSec), fc);

    // Sustain is a LEVEL (fraction of the attack peak), clamped to [0,1] (§2.3, §2.4).
    sustain_ = std::clamp(p.sustain, 0.0f, 1.0f);
    curve_   = p.curve;
    trig_    = p.trig;

    // If we are currently holding the sustain target or decaying toward it, keep the
    // live target consistent with the (possibly changed) sustain level.
    if (stage_ == EnvStage::Decay || stage_ == EnvStage::Sustain) {
        target_ = sustain_;
        if (stage_ == EnvStage::Sustain)
            level_ = sustain_;
    }
}

// --- Trigger state machine (§2.5) -------------------------------------------------
//
// GateTrig: every new note-on (incl. legato) fires a fresh Attack. Gate: one shot
// per held gate; a legato note-on is ignored while already non-Idle. Lfo: new key
// presses are honored only when Idle; the LFO clock (clockTrigger) drives retrigger
// while held. v1 re-attacks from the CURRENT level (no snap to 0) so there is no
// discontinuity — a (PI) choice, an open validation gap, not a measured fact
// [docs/design/03 §2.5; research/04 §5.3].

void Envelope::startAttack() noexcept {
    using namespace mw::cal::env;
    stage_  = EnvStage::Attack;
    target_ = kEnvAttackOvershoot;   // asymptote above unity; snaps to clamped 1.0
    coeff_  = aCoeff_;
}

void Envelope::noteOn(bool legato) noexcept {
    switch (trig_) {
        case EnvTrigMode::GateTrig:
            // Always restart Attack, including on a legato note-on (trills).
            startAttack();
            break;
        case EnvTrigMode::Gate:
            // One shot per held gate: ignore a legato note-on while sounding; only
            // the initial gate (from Idle) starts Attack.
            if (stage_ == EnvStage::Idle || !legato)
                startAttack();
            break;
        case EnvTrigMode::Lfo:
            // The LFO clocks the envelope; an initial key press from Idle still opens
            // it, but subsequent retriggers come from clockTrigger(), not new presses.
            if (stage_ == EnvStage::Idle)
                startAttack();
            break;
    }
}

void Envelope::noteOff() noexcept {
    // Gate release: head for 0 and the Idle terminus (§2.4). From Idle there is
    // nothing to release.
    if (stage_ == EnvStage::Idle)
        return;
    stage_  = EnvStage::Release;
    target_ = 0.0f;
    coeff_  = rCoeff_;
}

void Envelope::clockTrigger() noexcept {
    // LFO-mode retrigger on the Lfo cycle edge (§2.5, §3.6): restart Attack while a
    // key is held, independent of new key presses. Only meaningful in Lfo mode.
    if (trig_ != EnvTrigMode::Lfo)
        return;
    startAttack();
}

// --- Hot path: advance one control-rate tick of contour (§2.4) --------------------

float Envelope::tick() noexcept {
    using namespace mw::cal::env;

    switch (stage_) {
        case EnvStage::Idle:
            // Inactive: hold 0.
            break;

        case EnvStage::Attack: {
            // One-pole approach toward the >1 overshoot; transition to Decay when the
            // contour reaches unity, clamped to exactly 1.0 (§2.4).
            level_ += (target_ - level_) * (1.0f - coeff_);
            if (level_ >= 1.0f) {
                level_  = 1.0f;
                stage_  = EnvStage::Decay;
                target_ = sustain_;
                coeff_  = dCoeff_;
            }
            break;
        }

        case EnvStage::Decay: {
            // Approach the sustain level; snap and advance to Sustain within the snap
            // threshold (§2.4; ADR-020 S10/S12 deterministic stage bookkeeping).
            level_ += (target_ - level_) * (1.0f - coeff_);
            if (std::abs(level_ - sustain_) <= kEnvSnapThreshold) {
                level_ = sustain_;
                stage_ = EnvStage::Sustain;
            }
            break;
        }

        case EnvStage::Sustain:
            // Hold the sustain level while gated (no movement, §2.4).
            level_ = sustain_;
            break;

        case EnvStage::Release: {
            // Approach 0; snap and advance to Idle within the snap threshold (§2.4).
            level_ += (target_ - level_) * (1.0f - coeff_);
            if (level_ <= kEnvSnapThreshold) {
                level_ = 0.0f;
                stage_ = EnvStage::Idle;
            }
            break;
        }
    }

    return level_;
}

} // namespace mw101::dsp
