// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/Clock.cpp — the single H->L edge node + 3-way source wrapper + swing
// (task 086). Realizes docs/design/05 §7.1-§7.8 / ADR-007 §Decision 5, C17-C24, C27.
//
// All hot paths are noexcept and allocation-free: renderEdges() writes only into the
// caller's pre-sized span and clamps to its capacity [§7.7; §10; ADR-007 C26].

#include "Clock.h"

#include <algorithm>
#include <cmath>

#include "../calibration/ClockConstants.h"

namespace mw::control {

namespace {

// §7.8 — HostRate -> quarter-notes-per-step table [ADR-007 C23]. Pure function so
// the table lives in exactly one place and is bit-stable.
constexpr double quarterNotesPerStep(HostRate r) noexcept {
    switch (r) {
        case HostRate::Quarter:         return 1.0;
        case HostRate::Eighth:          return 0.5;
        case HostRate::EighthT:         return 1.0 / 3.0;
        case HostRate::Sixteenth:       return 0.25;
        case HostRate::SixteenthT:      return 0.25 / 1.5;     // = 1/6
        case HostRate::ThirtySecond:    return 0.125;
        case HostRate::DottedEighth:    return 0.75;
        case HostRate::DottedSixteenth: return 0.375;
    }
    return 0.25;   // unreachable; defensive default
}

} // namespace

void Clock::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
    internalNextEdge_ = 0.0;   // first Internal edge at the very next block start
    pendingKeypress_ = -1;
}

void Clock::setSource(ClockSource s) noexcept {
    source_ = s;
}

void Clock::setInternalRateHz(float hz) noexcept {
    internalRateHz_ = std::clamp(hz, mw::cal::clock::kInternalRateMinHz,
                                 mw::cal::clock::kInternalRateMaxHz);
}

void Clock::setHostRate(HostRate r) noexcept {
    hostRate_ = r;
}

void Clock::setSwing(float fraction) noexcept {
    swing_ = std::clamp(fraction, mw::cal::clock::kSwingMin, mw::cal::clock::kSwingMax);
}

void Clock::setClockResetOnKeypress(bool on) noexcept {
    resetOnKeypress_ = on;
}

void Clock::resetToKeypress(int sampleOffset) noexcept {
    // Re-phase only when the feature is enabled [§7.5, C22]. The same reset path
    // serves Internal phase reset and Host/Ext next-boundary reference reset.
    if (!resetOnKeypress_) {
        return;
    }
    pendingKeypress_ = sampleOffset;
    // Internal: the phase accumulator re-locks so the next edge lands AT the keypress.
    internalNextEdge_ = static_cast<double>(sampleOffset);
}

void Clock::renderEdges(const mw::TransportInfo& t,
                        std::span<const int> extPulseOffsets,
                        std::span<ClockEdge> out,
                        int numFrames,
                        int& outCount) noexcept {
    outCount = 0;
    const int capacity = static_cast<int>(out.size());
    if (capacity <= 0 || numFrames <= 0) {
        pendingKeypress_ = -1;
        return;
    }

    auto push = [&](int sampleOffset) noexcept -> bool {
        if (sampleOffset < 0 || sampleOffset >= numFrames) {
            return true;   // outside the block; skip but keep producing
        }
        if (outCount >= capacity) {
            return false;  // span full; stop (no overrun, no alloc)
        }
        out[outCount].sampleOffset = sampleOffset;
        ++outCount;
        return true;
    };

    switch (source_) {
        case ClockSource::Internal: {
            // Free-running LFO/CLK at internalRateHz_; RATE sets tempo [§7.3, C18].
            const double inc = static_cast<double>(internalRateHz_) / sampleRate_;
            if (inc <= 0.0) { pendingKeypress_ = -1; return; }
            const double period = 1.0 / inc;   // samples per cycle (per edge)

            // internalNextEdge_ is the (fractional) sample at which the next edge
            // fires, measured from this block's start. A pending keypress has already
            // re-locked it to the keypress sample in resetToKeypress().
            double next = internalNextEdge_;
            while (next < static_cast<double>(numFrames)) {
                // push() no-ops once the span is full; we keep advancing the phase so
                // the next block stays phase-accurate (no drift) even if we stopped
                // emitting edges this block [§7.4 drift-free intent].
                push(static_cast<int>(std::lround(next)));
                next += period;
            }
            // Carry the remainder into the next block (subtract this block's length).
            internalNextEdge_ = next - static_cast<double>(numFrames);
            pendingKeypress_ = -1;
            break;
        }

        case ClockSource::HostSync: {
            // Edges derived from ABSOLUTE PPQ each block, so tempo automation / loop
            // wrap / scrub re-derive the next edge with no cumulative drift [§7.4, C19].
            if (!t.isPlaying) { pendingKeypress_ = -1; return; }

            const double bpm = (t.bpm > 0.0) ? t.bpm : 120.0;
            const double sr = (t.sampleRate > 0.0) ? t.sampleRate : sampleRate_;
            const double ppqPerSample = (bpm / 60.0) / sr;
            if (ppqPerSample <= 0.0) { pendingKeypress_ = -1; return; }

            const double qnPerStep = quarterNotesPerStep(hostRate_);
            const double blockStartPpq = t.ppqPosition;
            const double blockEndPpq = blockStartPpq + numFrames * ppqPerSample;

            // SWING (host-sync only): delay even-numbered (2nd, 4th, ... -> odd 0-based
            // index) step edges by kSwingTaper(s) * stepPeriodSamples [§7.6, C24].
            const double stepSamples = qnPerStep / ppqPerSample;
            const double swingFrac = mw::cal::clock::kSwingTaper(swing_);   // 0 at 50%
            const double swingOffset = swingActive() ? swingFrac * stepSamples : 0.0;

            // First step index at or after block start: stepIndex = round(ppq/qnPerStep).
            // Use ceil on the index so we only place boundaries inside [start, end).
            long firstIdx = static_cast<long>(std::ceil(blockStartPpq / qnPerStep - 1e-9));
            if (firstIdx < 0) firstIdx = 0;

            for (long idx = firstIdx;; ++idx) {
                const double boundaryPpq = static_cast<double>(idx) * qnPerStep;
                if (boundaryPpq >= blockEndPpq) break;
                double offset = (boundaryPpq - blockStartPpq) / ppqPerSample;
                if ((idx & 1L) != 0L) {        // odd 0-based index == the swung step
                    offset += swingOffset;
                }
                const int s = static_cast<int>(std::lround(offset));
                if (!push(s)) break;            // span full
            }

            // Keypress re-phase: force an edge at the keypress sample and re-anchor the
            // grid there (next-boundary reference reset, §7.5). Replaces grid edges so
            // the pattern locks to the keypress.
            if (pendingKeypress_ >= 0 && pendingKeypress_ < numFrames) {
                outCount = 0;
                long k = 0;
                for (double s = static_cast<double>(pendingKeypress_);
                     s < static_cast<double>(numFrames); s += stepSamples, ++k) {
                    double off = s;
                    if ((k & 1L) != 0L) off += swingOffset;
                    if (!push(static_cast<int>(std::lround(off)))) break;
                }
            }
            pendingKeypress_ = -1;
            break;
        }

        case ClockSource::Ext: {
            // One supplied pulse == one step; RATE/swing are inert [§7.2, §7.6].
            for (int p : extPulseOffsets) {
                if (!push(p)) break;
            }
            pendingKeypress_ = -1;
            break;
        }
    }
}

} // namespace mw::control
