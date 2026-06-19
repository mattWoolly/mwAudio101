// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Lfo.cpp — prepare / rate-clamp + phase-increment / phase accumulation, the
// two CONTINUOUS waveform cores SmoothTri (triangle rounded toward sine) and Square
// (intentionally hard-edged) [task 055], and the digital uniform sample/hold "Random"
// core, the injected shared-noise "Noise" core, and the H->L cycle-edge flag [task
// 059]. See core/dsp/Lfo.h (task 051 declared the API + POD layout). Realizes
// docs/design/03-dsp-envelope-lfo-vca.md §3.4 (rate range + phase), §3.5 (all four
// waveform cores) and §3.6 (the oscillator's own cycle-edge flag).
//
// STILL OUT OF SCOPE here: the cycleEdge()->Envelope::clockTrigger wiring (Envelope /
// control-rate doc), the arp/seq edge-advance logic + EXT CLK IN override ([ADR-005] /
// control-rate doc), the white-noise GENERATOR (owned by the NoiseSource doc — this
// core only CONSUMES the injected sample), and the mod-bus kModBusLpHz LPF (owned by
// ModRouting, §3.5). cycleEdge() flags only the LFO's own phase wrap; what consumes it
// is owned elsewhere [docs/design/03 §3.6; task 059 ## Out of scope].
//
// Random core [docs/design/03 §3.5; research/04 §3.4]: a sample/hold register shReg_
// reloaded with a UNIFORM pseudo-random value in [-1, 1) on each LFO cycle edge — a
// digital CPU+DAC pseudo-S/H, NOT an analog noise S/H. The PRNG is a SEEDED POD
// (rngState_, seeded in reset()/prepare() to the fixed (PI) kLfoRandomSeed) so the
// value stream is deterministic / golden-reproducible [docs/design/03 §3.3, §3.5;
// docs/design/00 §9.2; ADR-013]. The step is PCG-XSH-RR mirroring mw::util::Prng:
// pure fixed-width unsigned integer arithmetic (no FP, no libm), bit-identical across
// macOS arm64 / Linux x64.
//
// Real-time invariants [docs/design/03 §3.1; docs/design/00 §9; ADR-001, ADR-019
// VT-01, ADR-020 S14]: all state is POD and sized at construction; the rate-derived
// phase increment is computed in setRateHz (called off the audio thread on the
// control-rate tick); tick() touches only members, allocates nothing, takes no lock,
// and is noexcept. No (PI) literal is inlined: kLfoSmoothShape and the Random seed /
// LCG constants resolve from the calibration table [docs/design/00 §8.3; ADR-020 S13].

#include "Lfo.h"

#include "../calibration/EnvLfoVcaConstants.h"
#include "../calibration/LfoShConstants.h"

#include <cstdint>

namespace mw101::dsp {

namespace {
// Rate band, FROZEN fact (high confidence), NOT a (PI) tunable: the LFO runs
// 0.1–30 Hz; the disputed 0.35 Hz minimum is a clone artifact and MUST NOT be
// enforced [docs/design/03 §3.4; research/04 §3.1, §5.1].
constexpr float kLfoRateMinHz = 0.1f;
constexpr float kLfoRateMaxHz = 30.0f;

constexpr float clampRate (float hz) noexcept
{
    if (hz < kLfoRateMinHz) return kLfoRateMinHz;
    if (hz > kLfoRateMaxHz) return kLfoRateMaxHz;
    return hz;
}

// Native bipolar triangle from a phase in [0,1): -1 at phase 0, rising to +1 at the
// half-cycle, falling back to -1. This is the "native triangle" the §3.5 SmoothTri
// shaper rounds toward sine.
constexpr float bipolarTriangle (float phase) noexcept
{
    return (phase < 0.5f) ? (4.0f * phase - 1.0f)    // -1 -> +1 over [0, 0.5)
                          : (3.0f - 4.0f * phase);   // +1 -> -1 over [0.5, 1)
}

// Cubic sine approximant on [-1,1]: maps the triangle value through a smooth,
// monotonic, bound-preserving (±1 -> ±1, 0 -> 0) curve that ROUNDS the triangle's
// hard corners toward a sine shape. This is the "sineApprox" of §3.5 — explicitly a
// rounding, not a mathematically pure sine.
constexpr float sineApprox (float t) noexcept
{
    return t * (1.5f - 0.5f * t * t);
}

// --- Random core (digital uniform sample/hold) PRNG -------------------------------
// One PCG-XSH-RR 64/32 step on the seeded uint64 state. Pure fixed-width unsigned
// integer arithmetic (wraparound is defined) so the stream is identical on every
// conforming compiler/platform — no FP, no transcendentals, no platform libm — and
// the golden harness stays bit-stable [core/util/Prng.h; docs/design/03 §3.5;
// docs/design/00 §9.2]. Mirrors mw::util::Prng so the (PI) seed reproduces the same
// stream a test oracle derives from those same constants.
constexpr std::uint32_t pcgStep (std::uint64_t& state) noexcept
{
    const std::uint64_t old = state;
    state = old * mw::cal::lfo::kLfoRandomLcgMultiplier
          + mw::cal::lfo::kLfoRandomLcgIncrement;
    const std::uint32_t xorshifted =
        static_cast<std::uint32_t> (((old >> 18) ^ old) >> 27);
    const std::uint32_t rot = static_cast<std::uint32_t> (old >> 59);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

// PCG seed init (state=0, step, add seed, step) — identical to mw::util::Prng::seed so
// the seeded stream matches an independent re-derivation. Leaves state primed for the
// FIRST reload draw.
constexpr void pcgSeed (std::uint64_t& state, std::uint64_t s) noexcept
{
    state = 0;
    pcgStep (state);
    state += s;
    pcgStep (state);
}

// One uniform draw in [-1, 1): top 24 bits -> [0,1) then *2 - 1. The fixed integer ->
// float scaling matches mw::util::Prng::nextFloat (2^-24) so the value is reproducible
// and bipolar-symmetric. This is the §3.5 "[-1, 1] (mapped from the original 0..+10 V
// 6-bit pseudo-S/H)" range; the half-open upper bound mirrors the noise mapping.
inline float pcgBipolar (std::uint64_t& state) noexcept
{
    const float u01 = static_cast<float> (pcgStep (state) >> 8) * (1.0f / 16777216.0f);
    return 2.0f * u01 - 1.0f;
}
} // namespace

void Lfo::prepare (double sampleRate, int controlRateDivider) noexcept
{
    sampleRate_  = sampleRate;
    ticksPerCtl_ = (controlRateDivider > 0) ? controlRateDivider : 1;
    reset();
    // phaseInc_ is (re)derived from the last requested rate against the new control
    // rate fc; setRateHz is normally called after prepare, but recompute defensively
    // so a prepare-without-setRateHz still has a coherent (rate-0) increment.
}

void Lfo::reset() noexcept
{
    phase_ = 0.0f;
    value_ = 0.0f;
    edge_  = false;

    // Random S/H reload [docs/design/03 §3.3 "phase -> 0, S/H reload", §3.5]: re-seed
    // the deterministic POD PRNG to the fixed (PI) golden seed and reload the S/H
    // register with the FIRST uniform draw, so a reset restarts an identical,
    // golden-reproducible value stream (the cycle-0 held value) [docs/design/00 §9.2].
    pcgSeed (rngState_, mw::cal::lfo::kLfoRandomSeed);
    shReg_ = pcgBipolar (rngState_);
}

void Lfo::setRateHz (float hz) noexcept
{
    // Clamp into the FROZEN [0.1, 30] Hz band; 0.35 Hz is NOT enforced as a floor.
    const float clamped = clampRate (hz);

    // fc = sampleRate / ticksPerControl is the control-rate the phase advances at;
    // phaseInc_ = rateHz / fc so one full cycle takes fc/rate ticks [§3.4].
    const double fc = (ticksPerCtl_ > 0) ? (sampleRate_ / static_cast<double> (ticksPerCtl_))
                                         : sampleRate_;
    phaseInc_ = (fc > 0.0) ? static_cast<float> (static_cast<double> (clamped) / fc)
                           : 0.0f;
}

void Lfo::setShape (LfoShape s) noexcept
{
    shape_ = s;
}

void Lfo::resetPhaseOnKey() noexcept
{
    // Clock-reset-on-keypress hook (§3.6): restart the oscillator's phase ONLY. The
    // arp/seq edge-advance logic that consumes this is owned by the control-rate doc.
    // This is a phase hook, NOT a full reset: it deliberately does NOT re-seed the
    // Random PRNG nor reload the S/H — a key press realigns the LFO/clock phase, it
    // does not rewind the deterministic random stream (full reset() does that).
    phase_ = 0.0f;
    edge_  = false;
}

void Lfo::setNoiseSource (const float* sharedNoiseSample) noexcept
{
    // The white-noise source is INJECTED, never owned here [§3.5, §1.2]: the Noise
    // shape reads this borrowed pointer (the same shared sample the audio mixer/voice
    // uses), never a private generator. Storing nullptr means Noise emits silence.
    noiseSample_ = sharedNoiseSample;
}

float Lfo::tick() noexcept
{
    // Emit the waveform at the CURRENT phase, then advance and detect the H->L wrap.
    switch (shape_)
    {
        case LfoShape::SmoothTri:
        {
            // Native triangle, then round toward sine by the (PI) blend [§3.5]:
            //   out = lerp(tri, sineApprox(tri), kLfoSmoothShape)
            // Labeled "rounded toward sine," never a mathematically pure sine.
            const float tri   = bipolarTriangle (phase_);
            const float k     = mw::cal::lfo::kLfoSmoothShape;
            value_ = tri + k * (sineApprox (tri) - tri);
            break;
        }
        case LfoShape::Square:
        {
            // Intentionally hard-edged [§3.5]: out = (phase < 0.5) ? +1 : -1. The
            // raw square LFO is NOT smoothed here (only a PWM destination would be).
            value_ = (phase_ < 0.5f) ? 1.0f : -1.0f;
            break;
        }
        case LfoShape::Random:
        {
            // Digital uniform sample/hold [§3.5; research/04 §3.4]: emit the HELD
            // register. The register stays constant for the whole cycle and is
            // reloaded only on the H->L cycle edge below, so the output changes ONLY
            // on cycle edges. The reload value is a seeded uniform draw (deterministic).
            value_ = shReg_;
            break;
        }
        case LfoShape::Noise:
        {
            // Routes the INJECTED shared white-noise sample (the same source as the
            // audio mixer) [§3.5, §1.2]; never a private generator. nullptr -> silence.
            // The fixed kModBusLpHz mod-bus LPF is applied downstream by ModRouting,
            // NOT here (§3.5), so this core stays a pure pass-through of the shared
            // sample. The Random PRNG is untouched in this branch.
            value_ = (noiseSample_ != nullptr) ? *noiseSample_ : 0.0f;
            break;
        }
    }

    // Advance the phase accumulator and wrap into [0,1); flag the H->L cycle edge for
    // one tick on the wrap [§3.4, §3.6].
    phase_ += phaseInc_;
    edge_ = false;
    if (phase_ >= 1.0f)
    {
        phase_ -= 1.0f;
        // Guard a pathological increment > 1 (rate/fc never produces this in-band,
        // but keep phase bounded without an unbounded loop on the hot path).
        if (phase_ >= 1.0f)
            phase_ -= static_cast<float> (static_cast<int> (phase_));
        edge_ = true;

        // Random S/H reload on the cycle edge [§3.5]: draw the NEXT uniform value into
        // the hold register so it takes effect from the next tick (the next cycle).
        // Done unconditionally on every edge — the draw advances the deterministic
        // PRNG regardless of the active shape, so the Random stream tracks elapsed
        // cycles (a later shape switch resumes the same golden sequence). Pure integer
        // PRNG: no allocation, no lock, noexcept hot path.
        shReg_ = pcgBipolar (rngState_);
    }

    return value_;
}

} // namespace mw101::dsp
