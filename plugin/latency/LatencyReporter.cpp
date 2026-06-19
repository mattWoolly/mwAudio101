// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/latency/LatencyReporter.cpp — out-of-line definitions for the constant-PDC
// LatencyReporter (task 105). See LatencyReporter.h for the design + the ADR-017 /
// docs/design/09 §8.3 / docs/design/00 §7 citations.

#include "latency/LatencyReporter.h"

namespace mw::plugin {

int LatencyReporter::computeWorstCaseLatency(double /*sampleRate*/) const noexcept {
    // The single CONSTANT worst-case total group delay = the SUM of exactly the two
    // contributing sources [ADR-017 L1/L2; docs/design/09 §8.3 table], each declared
    // by core as a fixed group-delay constant in BASE-RATE samples:
    //
    //   L1  per-voice oversampled-zone group delay (IR3109 ladder + diode-clamp
    //       resonance + BA662 VCA drive; realtime polyphase IIR halfband, ADR-004),
    //   L2  FX Drive 2x-oversampling group delay (its own post-voice up/down pair),
    //       ALWAYS counted even when Drive is bypassed (constant-padded).
    //
    // The FX Delay musical time + Chorus modulation delay (L3) are user-set / intended
    // musical delay and DO NOT contribute. Because both contributors are declared in
    // base-rate samples and neither depends on the host rate, the returned value is the
    // SAME fixed integer for every sample rate (the argument is retained per the §8.3
    // signature) — and is invariant to FX bypass, Quality tier, and build-to-build
    // [ADR-017 L5, L7, L8, L11]. noexcept, no alloc, no lock.
    return mw::cal::latency::kVoiceZoneGroupDelaySamples   // L1 (per-voice IIR zone)
         + mw::cal::fxos::kReportedLatencySamples;          // L2 (FX Drive 2x OS)
}

void LatencyReporter::preparePadding(int worstCaseSamples, int numChannels) {
    // The ONLY allocation site [ADR-017 L10]. Preallocate one fixed-integer-delay
    // padding line per channel, each able to delay up to the worst case, so a shorter
    // configuration can be aligned UP TO worstCaseSamples on the audio thread without
    // ever reallocating [L5]. Called off the audio thread (processor prepareToPlay).
    worstCaseSamples_ = worstCaseSamples < 0 ? 0 : worstCaseSamples;
    numChannels_      = numChannels < 0 ? 0 : numChannels;

    padLines_.assign(static_cast<std::size_t>(numChannels_), mw::fx::FractionalDelayLine{});
    for (auto& line : padLines_) {
        // Size each ring to hold at least worstCaseSamples of history; the runtime
        // read tap in padBlock() is clamped to this. fixedIntegerDelay is left 0 — we
        // do NOT use the compile-time tap; padBlock() reads a runtime delay so the
        // same prepared line serves any short config up to the worst case.
        line.prepare(worstCaseSamples_);
    }

    prepared_ = true;
}

void LatencyReporter::padBlock(float* const* channels, int numChannels, int padSamples,
                               int numSamples) noexcept {
    // Audio-thread hot path: delay each channel by padSamples using the preallocated
    // lines, in place. NO heap allocation, NO lock, NO resize, never recomputes the
    // reported latency [ADR-017 L10]. padSamples is clamped to the prepared worst case
    // (the lines were sized to it); a delay of 0 is an identity pass-through (the line
    // still advances so a later pad change stays glitch-bounded).
    if (padSamples < 0) padSamples = 0;
    if (padSamples > worstCaseSamples_) padSamples = worstCaseSamples_;

    int chs = numChannels < numChannels_ ? numChannels : numChannels_;
    if (chs < 0) chs = 0;

    const float delay = static_cast<float>(padSamples);
    for (int ch = 0; ch < chs; ++ch) {
        float* x = channels[ch];
        if (x == nullptr) continue;
        auto& line = padLines_[static_cast<std::size_t>(ch)];
        for (int n = 0; n < numSamples; ++n) {
            line.write(x[n]);
            x[n] = line.read(delay);   // integer delay (frac == 0) -> exact tap
        }
    }
}

} // namespace mw::plugin
