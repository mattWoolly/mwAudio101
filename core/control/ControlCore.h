// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/ControlCore.h — the shared 6-bit-DAC/4052/S&H control object
// (docs/design/04-voice-and-control.md §7).
//
// This file is built in two atomic backlog steps:
//
//   TASK 070 (pitch assembly): the pure, static VINTAGE pitch pipeline —
//     assemblePitchCounts (key + range base + octave + key-shift in the integer
//     DAC-count domain) and countsToVolts (1 count == 1 semitone, 12 counts ==
//     1 octave == 1 V) — plus the D7:D6 4052 route model [§7.2, §7.3; ADR-005
//     §Decision items 1 & 2]. These are the only members §7.8 marks `static`.
//
//   TASK 071 (driver): the advance() control-tick loop and the VINTAGE/MODERN
//     pole model on TOP of that pitch pipeline [§7.4-§7.7; ADR-005 C1-C7;
//     ADR-016 R-1]:
//       - prepare / setPole / setJitterEnabled / advance / effectivePole (§7.8);
//       - a sample-counter-driven control tick inside processBlock (never a
//         wall-clock timer or a background thread) firing at VINTAGE ~2 ms / MODERN
//         clean sub-block boundaries (§7.4-§7.5);
//       - seeded loop-time jitter over the 1.5-3.5 ms envelope, OFF by default; the
//         jitter-OFF fixed-tick VINTAGE config is the bit-exact reference (§7.4; CC1/CC2);
//       - per-feature MODERN auto-engage (poly/unison/MPE/sub-cent automation) (§7.6);
//       - a branchless, alloc-free VINTAGE<->MODERN CV crossfade on macro automation (§7.7; CC7).
//
// EXPLICITLY OUT OF SCOPE (siblings own these): assemblePitchCounts/countsToVolts
// are owned/implemented by task 070 above (not re-minted here); the detailed arp/seq
// clock edges are ADR-007/022 (only CLOCK RESET flows through the KeyAssigner via the
// VoiceManager); the Vintage-Control / jitter parameter IDs are doc 06; VoiceManager
// internals — advance() only calls its controlTick().
//
// No `juce::*` type appears here; mwcore is JUCE-free [ADR-001/D2]. Every hot-path
// method is noexcept, allocation-free, and lock-free; all sizing is in prepare()
// [ADR-001 C2-C5; docs/design/04 §8 RT1-RT7].

#pragma once

#include <cstdint>

#include "../calibration/ControlDriverConstants.h"
#include "../calibration/PitchAssemblyConstants.h"
#include "../util/Prng.h"
#include "../voice/VoiceTypes.h"   // VoiceMode

namespace mw {

// The control core's MODERN/VINTAGE macro pole selector [docs/design/04 §7.8;
// ADR-016 R-1 default MODERN]. Modern == continuous float (quantizer bypassed);
// Vintage == 6-bit integer DAC-count quantization at the S/H boundary.
enum class VintageControlPole : std::uint8_t { Modern = 0, Vintage = 1 };

// Re-export of the D7:D6 4052 route, so call sites name `mw::DacRoute::Vco` rather
// than reaching into the calibration namespace [docs/design/04 §7.2].
using DacRoute = cal::pitch::DacRoute;

class ControlCore {
public:
    // -----------------------------------------------------------------------
    // §7.3 — VINTAGE integer DAC-count pitch assembly (task 070).
    //
    // Assembles the VCO pitch CV as INTEGER DAC counts:
    //     counts = midiNote + rangeBase + octaveOffset + keyShift
    // in the count domain (1 count == 1 semitone), so portamento and glide later
    // genuinely stair-step through 6-bit counts. Conversion to volts happens ONLY
    // at the S/H boundary, via countsToVolts [docs/design/04 §7.3; ADR-005
    // §Decision items 1 & 2; research/07 §4.2].
    //
    // Pure, static, noexcept, allocation-free, lock-free. Integer-only.
    // -----------------------------------------------------------------------
    static int assemblePitchCounts(int midiNote, int rangeBase,
                                   int octaveOffset, int keyShift) noexcept;

    // -----------------------------------------------------------------------
    // §7.3 — count -> volt conversion at the S/H boundary (task 070).
    // 1 count == 1 semitone; 12 counts == 1 octave == 1 V (1 V/octave) =>
    //     volts = counts / 12. Pure, static, noexcept [§7.3; ADR-005 item 2].
    // -----------------------------------------------------------------------
    static float countsToVolts(int counts) noexcept;

    // =======================================================================
    // §7.4-§7.7 — the advance() control-tick driver (task 071).
    // =======================================================================

    // Off-the-audio-thread sizing: derives the VINTAGE fixed-tick period in samples
    // from the host sample rate, seeds the deterministic loop-time-jitter PRNG, and
    // clears the sample counter / crossfade state. The ONLY place that touches the
    // sample rate. Re-callable; idempotent on sample-rate change [ADR-001 C2; §8 RT6].
    // The crossfade blend is initialized to the current macro pole (no transient on
    // first block).
    void prepare(double sampleRate) noexcept;

    // The Vintage Control macro pole (host parameter; ID owned by doc 06). Changing
    // it does NOT snap the CV — advance() crossfades both branches (§7.7, CC7).
    // Lock-free flag write; the audio thread reads it at tick boundaries. Default
    // Modern [ADR-016 R-1].
    void setPole(VintageControlPole p) noexcept;

    // The separately-toggleable loop-time jitter (§7.4). OFF by default; the
    // jitter-OFF fixed-tick VINTAGE config is the bit-exact reference [ADR-005 C1].
    void setJitterEnabled(bool on) noexcept;

    // -----------------------------------------------------------------------
    // §7.4-§7.7 — advance one processBlock chunk.
    //
    // Advances the sample counter by `numSamples`, fires every control tick whose
    // boundary lands within the chunk, and calls `vm.controlTick()` once per fired
    // tick (clocking the KeyAssigner via the VoiceManager — advance() owns NO
    // VoiceManager internals, §7.8). Tick spacing is:
    //     - VINTAGE, jitter OFF : the fixed ~2 ms period (deterministic, bit-exact
    //       reference; CC1).
    //     - VINTAGE, jitter ON  : a seeded-PRNG period over the 1.5-3.5 ms envelope,
    //       drawn once per tick; deterministic from the seed; the jitter-OFF path is
    //       unchanged (CC2).
    //     - MODERN (default)    : the clean fixed sub-block tick (PI 16-32 smp; CC3).
    // It also slews the macro crossfade blend toward the (effective) pole each tick
    // so an automated VINTAGE<->MODERN transition produces no zipper (§7.7, CC7).
    //
    // Templated on the VoiceManager type (duck-typed on `controlTick()`) so this task
    // stays atomic and does not depend on the concrete VoiceManager (task 074); the
    // §7.8 `VoiceManager&` intent is preserved — the only call into vm is controlTick().
    //
    // noexcept, allocation-free, lock-free, no wall-clock, no unbounded loop (the
    // per-chunk tick count is bounded by numSamples / minimum tick period)
    // [ADR-001 C3-C5; ADR-005 §Contract invariants; docs/design/04 §8 RT1-RT7].
    // -----------------------------------------------------------------------
    template <typename VoiceManagerT>
    void advance(int numSamples, VoiceManagerT& vm) noexcept {
        if (numSamples <= 0) return;

        int remaining = numSamples;
        while (remaining > 0) {
            // Drain the residual count toward the next tick boundary. The sample
            // counter advances by EVERY consumed sample (whether or not a tick fires
            // this step), so sampleCounter() is the true monotonic processBlock
            // position and tick boundaries land on the exact sample [§7.4].
            if (samplesToNextTick_ > remaining) {
                samplesToNextTick_ -= remaining;
                sampleCounter_ += remaining;
                remaining = 0;
                break;
            }
            remaining -= samplesToNextTick_;
            sampleCounter_ += samplesToNextTick_;

            // Fire one control tick: clock the KeyAssigner via the VoiceManager and
            // advance the macro crossfade one step (§7.7). advance() owns no VM
            // internals beyond this single call (§7.8).
            vm.controlTick();
            stepCrossfade();
            ++tickCount_;

            // Schedule the next tick boundary (fixed, jittered, or modern sub-block).
            samplesToNextTick_ = nextTickPeriodSamples();
        }
    }

    // -----------------------------------------------------------------------
    // §7.6 — effective pole after per-feature MODERN auto-engage.
    //
    // Returns Modern when poly/unison (mode != Mono) OR MPE per-note pitch is active
    // OR pitch is under sub-cent host automation — EVEN if the macro is Vintage —
    // because those modern features are musically incompatible with the hard 6-bit
    // ladder [docs/design/04 §7.6; ADR-005 §Decision item 5, C4-C6]. The mono,
    // single-voice, non-MPE, non-automated path honors the macro pole fully.
    //
    // Pure, const, noexcept (a function of the macro pole + the three feature flags).
    // -----------------------------------------------------------------------
    VintageControlPole effectivePole(VoiceMode mode, bool mpeActive,
                                     bool pitchAutomated) const noexcept;

    // -----------------------------------------------------------------------
    // §7.7 — the blended VCO pitch CV (volts) for an assembled DAC-count pitch.
    //
    // Precomputes BOTH CV branches and blends them by the current crossfade coeff:
    //   VINTAGE branch : countsToVolts(counts)         (stair-stepped, 6-bit S/H)
    //   MODERN  branch : counts / 12 as continuous float (quantizer bypassed)
    // (the two branches DIFFER only when `counts` carries a fractional pitch; for an
    // integer count they coincide, so a plain MONO+VINTAGE hold is bit-exact.)
    //
    // blend == 1 -> pure VINTAGE, blend == 0 -> pure MODERN; an automated macro
    // transition slews `blend` over kMacroCrossfadeSeconds so there is no zipper.
    // Branchless, allocation-free [docs/design/04 §7.7; ADR-005 C7].
    //
    // `pitchCountsFloat` is the pitch in DAC-count units (may be fractional for the
    // MODERN/continuous path); the VINTAGE branch rounds to the nearest integer count
    // (the 6-bit S/H quantization) before the volt conversion.
    // -----------------------------------------------------------------------
    float blendedPitchVolts(float pitchCountsFloat) const noexcept;

    // -----------------------------------------------------------------------
    // Observers (for tests / the surfacing UI; no audio-thread state mutation).
    // -----------------------------------------------------------------------
    VintageControlPole macroPole() const noexcept { return macroPole_; }
    bool   jitterEnabled() const noexcept { return jitterOn_; }
    long long sampleCounter() const noexcept { return sampleCounter_; }
    long long tickCount() const noexcept { return tickCount_; }
    int    samplesToNextTick() const noexcept { return samplesToNextTick_; }
    float  crossfadeBlend() const noexcept { return xfade_; }   // 1 == VINTAGE, 0 == MODERN
    double sampleRate() const noexcept { return sampleRate_; }

private:
    // Advance the crossfade blend one tick toward the macro pole target (§7.7). One
    // pole-IIR slew step + a snap-to-target so the steady-state branch is exact and
    // the "is-crossfading" state is deterministic. Branchless.
    void stepCrossfade() noexcept;

    // The sample count to the NEXT control tick, per the current pole + jitter state
    // (§7.4-§7.5). VINTAGE jitter draws once from the seeded PRNG over the envelope.
    int nextTickPeriodSamples() noexcept;

    // --- macro / toggles (lock-free flags; read at tick boundaries) ---
    VintageControlPole macroPole_ = VintageControlPole::Modern;  // [ADR-016 R-1]
    bool   jitterOn_   = false;                                  // [ADR-005 C1]

    // --- sample-counter-driven tick state (sized in prepare) ---
    double sampleRate_       = 0.0;
    int    vintageTickSamples_ = 0;    // fixed VINTAGE period (from kVintageTickSeconds)
    int    jitterMinSamples_   = 0;    // 1.5 ms envelope floor
    int    jitterMaxSamples_   = 0;    // 3.5 ms envelope ceiling
    int    samplesToNextTick_  = 1;    // residual to the next tick boundary
    long long sampleCounter_   = 0;    // total samples advanced (monotonic)
    long long tickCount_       = 0;    // total ticks fired (monotonic)

    // --- macro crossfade (§7.7) ---
    float  xfade_       = 0.0f;        // current blend: 1 == VINTAGE, 0 == MODERN
    float  xfadeCoeff_  = 0.0f;        // per-tick pole-IIR slew coefficient

    // --- deterministic loop-time-jitter PRNG (§7.4; never wall-clock) ---
    util::Prng jitterRng_{};

    // DAC/mux/S&H route state (00/01/10/11) is modeled by the pitch pipeline (§7.2);
    // RANDOM regeneration on the clock H->L edge is owned by the arp/seq clock task.
};

} // namespace mw
