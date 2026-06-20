// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/version/RenderProvenance.h — the prepare-time RENDER PROVENANCE record
// (task 143, integration): the JUCE-free engine-tag/MANIFEST provenance surface that
// captures, keyed by a session's stored renderVersion and the host sample rate, which
// frozen constant-set was bound, whether the host rate is in the blessed set, and the
// requested-vs-active oversampling factor under the OS_CEILING clamp.
//
// Single sources of truth (this header REFERENCES, never redefines):
//   * the legacy-render path + frozen-constant-set selection keyed by renderVersion,
//     chosen at prepareToPlay, NEVER at audio rate
//       [docs/design/00 §8.2/§8.3; ADR-023 V10, V18] — core/calibration/ConstantSetSelector.h;
//   * the blessed sample-rate set {44100,48000,88200,96000} Hz
//       [docs/design/00 §8.4; ADR-023 V12] — core/calibration/GoldenKeyConstants.h;
//   * the OS_CEILING clamp (2x -> 1x when factor*hostFs would exceed 192 kHz internal),
//     "recorded in engine-tag/MANIFEST provenance"
//       [docs/design/00 §8.5 V15/V16; ADR-023 V15/V16] — core/calibration/OversampledZoneConstants.h.
//
// WHAT THIS OWNS. ONLY the assembly of those already-decided facts into one
// value-typed provenance POD, computed at prepare time. It mints NO new policy: the
// blessed-set membership, the clamp predicate, and the frozen-set bind all come from
// the existing single sources above. §8.5 requires the clamp to be "recorded in
// provenance"; this is that record, JUCE-free so it lives in mwcore and is exercised
// headlessly [docs/design/00 §9.3]. The §6.4 / V18 threading rule holds by
// construction: this is a constexpr/noexcept pure function of (renderVersion, hostFs,
// requestedFactor) with no allocation, no lock, and no transcendental — it runs at
// prepareToPlay / on the message thread, never at audio rate.
//
// OUT OF SCOPE (owned elsewhere): the actual MANIFEST file emission / engine-tag string
// (golden-harness / bless tooling), the UI "running unblessed at this host rate"
// surfacing (ui stream, ADR-023 V16), renderVersion state I/O (state-presets), and the
// engine's own factor selection (core/Engine.cpp already applies the same clamp).
//
// mwcore is JUCE-free [ADR-001 C1/C14].

#pragma once

#include "EngineVersion.h"

#include "../calibration/ConstantSetSelector.h"      // frozen-set bind keyed by renderVersion (§8.3)
#include "../calibration/GoldenKeyConstants.h"        // blessed sample-rate set + ceiling (§8.4/§8.5)
#include "../calibration/OversampledZoneConstants.h"  // OS_CEILING clamp predicate + strides (§8.5)

namespace mw::version {

// The prepare-time render-provenance record. A trivially-copyable value type so it can
// be snapshotted on the message thread and stamped into an engine tag / MANIFEST entry
// without allocation; nothing here is read on the audio thread [docs/design/00 §6.4;
// ADR-023 V18].
struct RenderProvenance {
    // --- renderVersion / legacy-render path (§8.2/§8.3) ----------------------------
    // The renderVersion this prepare renders at (the session's pinned/stored version on
    // the legacy path, or CURRENT for a new/blank session). Echoes the request even on
    // a refusal so a refusal can never masquerade as CURRENT [§8.2 no silent fallback].
    int  renderVersion = mw101::version::kCurrentRenderVersion;
    // True iff a SHIPPED, retained frozen constant-set was bound for renderVersion.
    bool frozenSetBound = false;
    // True iff renderVersion == CURRENT (the blessed path). A legacy session is false.
    bool isCurrentRender = false;

    // --- blessed sample-rate set (§8.4) --------------------------------------------
    double hostSampleRateHz = 0.0;
    // True iff hostSampleRateHz is exactly one of {44100,48000,88200,96000} Hz: the
    // engine runs the normal blessed per-SR table path [§8.4; ADR-023 V12/V13].
    bool blessedSampleRate = false;

    // --- OS_CEILING clamp provenance (§8.5 V15/V16) --------------------------------
    // The oversampling factor REQUESTED (the per-voice quality tier, 2x by default).
    int  requestedOversampleFactor = mw::cal::oszone::kDefaultFactor;
    // The ACTIVE factor after the OS_CEILING clamp: 1x when requested*hostFs would push
    // the internal rate strictly above OS_CEILING_HZ (192 kHz internal), else requested.
    int  activeOversampleFactor = mw::cal::oszone::kDefaultFactor;
    // True iff a >1x request was forced down to 1x by the ceiling — i.e. running
    // UNBLESSED at this host rate. THIS is the §8.5 "clamp recorded in provenance" flag.
    bool oversampleClampedToEco = false;

    // Convenience: the configuration is blessed iff the host rate is in the blessed set
    // AND the current render path is bound AND no ceiling clamp was applied. An
    // above-set host rate (clamped or not) is supported-but-unblessed [§8.5 V14].
    [[nodiscard]] constexpr bool isBlessedConfiguration() const noexcept {
        return blessedSampleRate && isCurrentRender && frozenSetBound
            && !oversampleClampedToEco;
    }
};

// Capture the render provenance for one prepare. Pure: a constexpr/noexcept function of
// the inputs with no allocation, no lock, no table build — safe to evaluate at
// prepareToPlay / on the message thread, never on the audio thread [§6.4; ADR-023 V18].
//
//   renderVersion    — the session's pinned/stored render version (legacy path) or
//                       CURRENT (new/blank). Bound against the §8.3 frozen-set registry;
//                       an unshipped version is refused (frozenSetBound == false), never
//                       silently rendered as CURRENT [§8.2].
//   hostSampleRateHz — the host sample rate; blessed iff in the {44.1,48,88.2,96} kHz set.
//   requestedFactor  — the requested per-voice oversampling factor (default 2x).
[[nodiscard]] constexpr RenderProvenance captureRenderProvenance(
    int    renderVersion,
    double hostSampleRateHz,
    int    requestedFactor = mw::cal::oszone::kDefaultFactor) noexcept {
    RenderProvenance p{};

    // (1) Frozen constant-set bind keyed by renderVersion (§8.3). The selector is the
    // single source of truth for the legacy-render path; we record its verdict.
    const mw::cal::ConstantSetSelection sel = mw::cal::selectConstantSet(renderVersion);
    p.renderVersion    = sel.renderVersion;     // echoes the REQUEST, never rewritten to CURRENT
    p.frozenSetBound   = sel.ok;
    p.isCurrentRender  = sel.isCurrent;

    // (2) Blessed sample-rate set membership (§8.4).
    p.hostSampleRateHz  = hostSampleRateHz;
    p.blessedSampleRate = mw::cal::golden::isBlessedSampleRate(hostSampleRateHz);

    // (3) OS_CEILING clamp (§8.5). Clamp the request into the supported strides first,
    // then force to 1x when requested*hostFs would push the internal rate strictly above
    // the ceiling — the SAME predicate the engine and the oversampled-zone wrapper use.
    int req = requestedFactor < mw::cal::oszone::kFactor1x ? mw::cal::oszone::kFactor1x
                                                           : requestedFactor;
    p.requestedOversampleFactor = req;
    if (req > mw::cal::oszone::kFactor1x
        && mw::cal::oszone::wouldExceedCeiling(hostSampleRateHz, req)) {
        p.activeOversampleFactor   = mw::cal::oszone::kFactor1x;
        p.oversampleClampedToEco   = true;   // recorded: running unblessed at this host rate
    } else {
        p.activeOversampleFactor   = req;
        p.oversampleClampedToEco   = false;
    }
    return p;
}

} // namespace mw::version
