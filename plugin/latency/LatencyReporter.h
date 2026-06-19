// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/latency/LatencyReporter.h — the constant plugin-delay-compensation (PDC)
// reporter (task 105).
//
// Single source of truth: ADR-017 (the constant / worst-case-padded latency policy +
// Drive placement), docs/design/09 §8.3 (the class signature, verbatim), and
// docs/design/00 §7 (Latency and PDC). This is the plugin-side object that the
// AudioProcessor will hand the value to the host via setLatencySamples — that host
// call site is task plugin-13 and is OUT OF SCOPE here [ADR-017 L4; §8.3].
//
// WHAT THIS OWNS. Two responsibilities, both fixed-once:
//
//   1. computeWorstCaseLatency(sampleRate): returns the SINGLE CONSTANT worst-case
//      total group delay the plugin can introduce across ANY FX on/off and ANY
//      Quality tier (1x/2x/4x), in BASE-RATE samples. It is the SUM of exactly two
//      contributors [ADR-017 L1/L2/L3; docs/design/09 §8.3 table]:
//        * the PER-VOICE oversampled-zone group delay (IR3109 ladder + diode-clamp
//          resonance + BA662 VCA drive; realtime polyphase IIR halfband, ADR-004) —
//          mw::cal::latency::kVoiceZoneGroupDelaySamples [L1], and
//        * the FX Drive 2x-oversampling group delay (its own post-voice up/down pair)
//          — mw::cal::fxos::kReportedLatencySamples [L2], ALWAYS counted even when
//          Drive is bypassed (that is what "constant, padded" means).
//      The FX Delay musical time + Chorus modulation delay DO NOT contribute [L3].
//      Both contributors are sourced from group-delay constants supplied by core
//      (the oversampler / FX-drive modules); nothing is inlined here [§8.3].
//      It is INVARIANT to FX bypass, Quality tier, and build-to-build [L5, L7, L8,
//      L11], and depends on the sample rate only in the trivial sense that both
//      contributors are declared in base-rate samples (so the returned value is the
//      same fixed integer for every rate); the argument is kept per the §8.3
//      signature and for a future rate-dependent contributor.
//
//   2. preparePadding(worstCaseSamples, numChannels): preallocates the fixed
//      integer-delay PADDING lines (one per channel) that delay-align a SHORTER
//      configuration up to the worst case, so the reported number never changes for
//      the instance lifetime [L5]. This is the ONLY allocation site [L10]. The
//      hot-path read (padBlock) touches only the preallocated lines: no alloc, no
//      lock, never recomputed/mutated on the audio thread [L10].
//
// RT-safety [ADR-017 L10; docs/design/00 §7.3, §9.1]: the worst-case value is
// computed and sized in prepare; the audio-thread read (padBlock) and the const
// computeWorstCaseLatency accessor perform ZERO heap allocation and take no lock; the
// reported number is never mutated from process.
//
// This header carries NO juce::* type — it is a plain C++ object the AudioProcessor
// composes; the host setLatencySamples marshalling lives in the processor [ADR-001].

#pragma once

#include "calibration/FxOversampler2xConstants.h"   // mw::cal::fxos::kReportedLatencySamples [L2]
#include "calibration/LatencyReporterConstants.h"    // mw::cal::latency::kVoiceZoneGroupDelaySamples [L1]
#include "dsp/fx/FractionalDelayLine.h"              // preallocated fixed-integer-delay padding line

namespace mw::plugin {

class LatencyReporter {
public:
    LatencyReporter() noexcept = default;

    // The single CONSTANT worst-case total latency in BASE-RATE samples [ADR-017 L4].
    // Computed and sized in prepare() ONLY (this accessor is pure / side-effect-free
    // and so is safe to call anywhere, but the processor caches it in prepare and
    // declares it to the host there). Never recomputed on the audio thread [L10].
    //
    // == per-voice IIR-zone group delay [L1] + FX Drive 2x OS group delay [L2].
    // FX Delay/Chorus musical time is excluded [L3]. Invariant to FX bypass, Quality
    // tier, and build [L5, L7, L8, L11]. noexcept, no alloc, no lock.
    [[nodiscard]] int computeWorstCaseLatency(double sampleRate) const noexcept;

    // Preallocate the per-channel fixed-integer-delay padding lines that align a
    // shorter (lower-latency) configuration UP TO worstCaseSamples [ADR-017 L5]. This
    // is the ONLY allocation site [L10]; call it off the audio thread (in the
    // processor's prepareToPlay). Idempotent / re-callable on channel-count or
    // worst-case change. A non-positive worstCaseSamples or numChannels prepares no
    // lines (the pad becomes an identity pass-through).
    void preparePadding(int worstCaseSamples, int numChannels);

    // --- Hot-path padding read (audio thread) -------------------------------------
    // Delay each of `numChannels` channels by `padSamples` using the preallocated
    // lines, in place, so a configuration whose ACTUAL latency is
    // (worstCaseSamples - padSamples) is aligned up to the reported worst case
    // [ADR-017 L5]. padSamples is clamped to the prepared worst case. noexcept, NO
    // heap allocation, NO lock; never resizes [L10]. A padSamples of 0 is an identity
    // pass-through (the line still advances so switching pad amounts stays glitch-
    // bounded). Channels beyond the prepared count are left untouched.
    void padBlock(float* const* channels, int numChannels, int padSamples, int numSamples) noexcept;

    // --- Introspection (no audio-rate cost) ---------------------------------------
    // The worst case sized into the padding lines by preparePadding (0 until prepared).
    [[nodiscard]] int  worstCaseSamples() const noexcept { return worstCaseSamples_; }
    [[nodiscard]] int  preparedChannels() const noexcept { return numChannels_; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // One fixed-integer-delay line per channel, preallocated to the worst case in
    // preparePadding(). Reuses the JUCE-free core ring buffer (mwcore); the dry-pad /
    // alignment use case is exactly the constant integer delay FractionalDelayLine
    // serves on the FX dry path [docs/design/07 §6.3]. padBlock() write()s then
    // read()s a RUNTIME integer delay (padSamples, clamped to the line's capacity)
    // rather than the line's compile-time fixed tap, so the same prepared lines serve
    // any short config up to the worst case without reallocation.
    std::vector<mw::fx::FractionalDelayLine> padLines_{};

    int  worstCaseSamples_ = 0;
    int  numChannels_      = 0;
    bool prepared_         = false;
};

} // namespace mw::plugin
