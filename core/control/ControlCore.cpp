// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/ControlCore.cpp — the ControlCore VINTAGE pitch pipeline (task 070)
// + the advance() control-tick driver and the VINTAGE/MODERN pole model (task 071).
//
// Task 070 implements the two pure static functions (assemblePitchCounts /
// countsToVolts) per docs/design/04 §7.3 and ADR-005 §Decision items 1 & 2.
//
// Task 071 implements the advance() control-tick driver: a sample-counter inside
// processBlock that fires control ticks (VINTAGE fixed ~2 ms / seeded jitter /
// MODERN clean sub-block tick), applies per-feature MODERN auto-engage, and
// crossfades the CV branches on macro automation [docs/design/04 §7.4-§7.7;
// ADR-005 C1-C7; ADR-016 R-1]. The control tick is driven ONLY by the sample
// counter (no wall-clock, no thread); every method is noexcept / alloc-free /
// lock-free and all sizing is in prepare() [ADR-001 C2-C5; docs/design/04 §8].

#include "ControlCore.h"

#include <cmath>

namespace mw {

// ===========================================================================
// Task 070 — VINTAGE integer DAC-count pitch pipeline.
// ===========================================================================

int ControlCore::assemblePitchCounts(int midiNote, int rangeBase,
                                     int octaveOffset, int keyShift) noexcept {
    // Pure integer additive assembly — key + range base + octave + key-shift, all
    // in the DAC-count domain (1 count == 1 semitone). No volts here: the count
    // domain is preserved until the S/H boundary so portamento/glide stair-step
    // [docs/design/04 §7.3; ADR-005 §Decision item 1 (counts->volts at S/H only)].
    return midiNote + rangeBase + octaveOffset + keyShift;
}

float ControlCore::countsToVolts(int counts) noexcept {
    // 1 count == 1 semitone, 12 counts == 1 octave == 1 V (1 V/octave). The single
    // count->volt conversion, at the S/H boundary [docs/design/04 §7.3; ADR-005
    // §Decision item 2]. kVoltsPerCount == 1.0/12.0.
    return static_cast<float>(static_cast<double>(counts) * cal::pitch::kVoltsPerCount);
}

// ===========================================================================
// Task 071 — advance() control-tick driver + VINTAGE/MODERN pole model.
// ===========================================================================

namespace {
// At least one sample between ticks (a tick period is never zero), so the per-chunk
// tick loop is bounded by numSamples / 1 and can never spin [ADR-001 C3 bounded loop].
constexpr int kMinTickSamples = 1;

// Fixed deterministic loop-time-jitter PRNG seed (never wall-clock) [§7.4; ADR-005 C1;
// docs/design/00 §9.2]. Shared by prepare() and reset() so a reset re-seeds to the same
// reproducible jitter stream as a fresh prepare — defined once here so the two known-start
// derivations cannot drift apart. The plugin overrides the per-instance seed off the
// audio thread (out of scope here). (PI) — an internal implementation constant, not a
// public calibration surface.
constexpr std::uint32_t kJitterSeed = 0x5ce10101u;
} // namespace

void ControlCore::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;

    // VINTAGE fixed-tick period and the jitter envelope, in samples, from the
    // documented seconds-domain constants (§7.4). Rounded to the nearest sample and
    // floored at one so a tick boundary always advances.
    auto toSamples = [sampleRate](double seconds) noexcept -> int {
        const int n = static_cast<int>(std::lround(seconds * sampleRate));
        return n < kMinTickSamples ? kMinTickSamples : n;
    };
    vintageTickSamples_ = toSamples(cal::control::kVintageTickSeconds);
    jitterMinSamples_   = toSamples(cal::control::kVintageJitterMinSeconds);
    jitterMaxSamples_   = toSamples(cal::control::kVintageJitterMaxSeconds);
    if (jitterMaxSamples_ < jitterMinSamples_) jitterMaxSamples_ = jitterMinSamples_;

    // Per-tick macro crossfade slew coefficient (§7.7). Computed once from the
    // crossfade time constant and a NOMINAL control-tick period (the VINTAGE ~2 ms
    // tick), so the blend rate is deterministic and independent of loop-time jitter:
    //   coeff = 1 - exp(-T_tick / tau).
    const double tickSeconds = (sampleRate > 0.0)
        ? static_cast<double>(vintageTickSamples_) / sampleRate
        : cal::control::kVintageTickSeconds;
    const double tau = cal::control::kMacroCrossfadeSeconds;
    xfadeCoeff_ = (tau > 0.0)
        ? static_cast<float>(1.0 - std::exp(-tickSeconds / tau))
        : 1.0f;  // tau == 0 => instant (still snaps via the epsilon)

    // Establish the post-sizing known start (counters, jitter PRNG seed, crossfade
    // blend, first tick boundary). Factored so reset() re-derives the IDENTICAL start
    // without repeating the sample-rate sizing above (§5.5; task 134b).
    establishKnownStart();
}

void ControlCore::reset() noexcept {
    // §5.5 runtime reset: return the tick driver to the SAME known start prepare()
    // establishes, but WITHOUT re-deriving the sample-rate sizing (sizing is a prepare
    // concern). The macro pole and the jitter toggle are preserved (a reset is not a
    // parameter change) — exactly as VoiceManager::reset() preserves the S7 selector.
    // Touches only state pre-sized in prepare(): noexcept, alloc-free, lock-free
    // [docs/design/00 §5.5; ADR-001 Decision / C2-C5]. This is what makes the assembled
    // Engine::reset() a deterministic fixed point (task 134b).
    establishKnownStart();
}

void ControlCore::establishKnownStart() noexcept {
    // Seed the deterministic loop-time-jitter PRNG (never wall-clock) [§7.4; ADR-005
    // C1; docs/design/00 §9.2]. A fixed seed keeps jitter-ON renders reproducible AND
    // makes a reset() re-seed to the same stream as a fresh prepare().
    jitterRng_.seed(kJitterSeed);

    // Reset the sample counter and schedule the first tick boundary for the current
    // pole. The crossfade blend starts AT the current macro pole so there is no
    // first-block transient.
    sampleCounter_     = 0;
    tickCount_         = 0;
    xfade_             = (macroPole_ == VintageControlPole::Vintage) ? 1.0f : 0.0f;
    samplesToNextTick_ = nextTickPeriodSamples();
}

void ControlCore::setPole(VintageControlPole p) noexcept {
    // Lock-free flag write. Does NOT snap the CV — advance() crossfades the blend
    // toward the new pole (§7.7, CC7).
    macroPole_ = p;
}

void ControlCore::setJitterEnabled(bool on) noexcept {
    // Lock-free flag write. The jitter-OFF path is the bit-exact reference (§7.4).
    jitterOn_ = on;
}

VintageControlPole ControlCore::effectivePole(VoiceMode mode, bool mpeActive,
                                              bool pitchAutomated) const noexcept {
    // §7.6 per-feature MODERN auto-engage: poly/unison, MPE per-note pitch, or
    // sub-cent host automation are musically incompatible with the hard 6-bit ladder
    // and force MODERN for the pitch path EVEN if the macro is Vintage [ADR-005 C4-C6].
    const bool autoEngageModern = (mode != VoiceMode::Mono) || mpeActive || pitchAutomated;
    if (autoEngageModern) return VintageControlPole::Modern;

    // The mono, single-voice, non-MPE, non-automated path honors the macro fully.
    return macroPole_;
}

float ControlCore::blendedPitchVolts(float pitchCountsFloat) const noexcept {
    // Precompute BOTH CV branches and blend, branchless (§7.7, CC7).
    //   VINTAGE : round to the nearest integer DAC count (the 6-bit S/H quantize),
    //             then counts -> volts (stair-stepped).
    //   MODERN  : the continuous-float count carried straight to volts (no quantize).
    const float kVoltsPerCount = static_cast<float>(cal::pitch::kVoltsPerCount);

    const int   quantized   = static_cast<int>(std::lround(pitchCountsFloat));
    const float vintageVolts = static_cast<float>(quantized) * kVoltsPerCount;
    const float modernVolts  = pitchCountsFloat * kVoltsPerCount;

    // blend == 1 -> pure VINTAGE, blend == 0 -> pure MODERN. No branch on the hot path.
    return modernVolts + xfade_ * (vintageVolts - modernVolts);
}

void ControlCore::stepCrossfade() noexcept {
    // One pole-IIR slew step toward the macro-pole target, then a snap so the
    // steady-state branch is exact and the "is-crossfading" state is deterministic
    // (§7.7). Branchless arithmetic; the only branch is the snap-to-target.
    const float target = (macroPole_ == VintageControlPole::Vintage) ? 1.0f : 0.0f;
    xfade_ += (target - xfade_) * xfadeCoeff_;
    if (std::fabs(target - xfade_) < cal::control::kMacroCrossfadeSnapEpsilon) {
        xfade_ = target;
    }
}

int ControlCore::nextTickPeriodSamples() noexcept {
    // MODERN (default): the clean fixed sub-block tick (PI 16-32 smp) — independent
    // of the host sample rate, deterministic [§7.5; ADR-005 C3].
    if (macroPole_ == VintageControlPole::Modern) {
        return cal::control::kModernTickSamples;
    }

    // VINTAGE, jitter OFF: the fixed ~2 ms period — the bit-exact reference (§7.4; CC1).
    if (!jitterOn_) {
        return vintageTickSamples_;
    }

    // VINTAGE, jitter ON: a seeded-PRNG period drawn once per tick over the
    // 1.5-3.5 ms envelope; deterministic from the seed (§7.4; CC2). The jitter-OFF
    // path above is left bit-identical — this branch is the only consumer of the PRNG.
    const int span = jitterMaxSamples_ - jitterMinSamples_;
    if (span <= 0) return jitterMinSamples_;
    // Uniform integer in [jitterMin, jitterMax] inclusive; pure-integer draw (no FP)
    // so the jittered tick stream is cross-platform bit-stable [docs/design/00 §9.2].
    const int draw = static_cast<int>(jitterRng_.nextU32() % static_cast<std::uint32_t>(span + 1));
    return jitterMinSamples_ + draw;
}

} // namespace mw
