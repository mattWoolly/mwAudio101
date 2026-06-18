// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/Calibration.h — THE single cross-module (PI) constants table +
// the per-renderVersion FROZEN constant-set registry (task 005b).
//
// This is the one header every downstream DSP module reads its tunable defaults
// from; no call site inlines a (PI) literal [docs/design/06 §3.10; docs/design/00
// §8.3; ADR-008 §1; ADR-020 S13]. Every constant here is (PI) — a *pragmatic
// invention*, NOT a measured SH-101 spec — and carries a tunable-default comment.
// Later tasks (029 VCO, 033 FastTanh, 035 FilterTables, 049 env/lfo/vca, 099
// event-buffer capacity) APPEND their constants into the matching namespace below
// rather than inlining them at a DSP call site [docs/design/11 §9 F-15].
//
// The per-renderVersion frozen constant-set registry (§8.3) associates each
// SHIPPED renderVersion with the frozen DSP constant set used to reproduce its
// audio on the legacy-render path; selection happens at prepareToPlay, never at
// audio rate [ADR-023 V10, V18, §Consequences]. Only shipped renderVersions are
// retained.

#pragma once

#include <array>
#include <cstddef>

#include "../version/EngineVersion.h"

namespace mw::cal {

// ---------------------------------------------------------------------------
// (PI) constants table, organized by subsystem namespace.
//
// Each value is a TUNABLE DEFAULT (PI) with no measured-oracle backing. A re-tune
// is one localized edit here; a de-zipper time-constant change forces a re-bless
// (the testing doc) [docs/design/06 §3.10; ADR-020 Consequences].
// ---------------------------------------------------------------------------

// Per-class parameter de-zipper time constants (seconds) [docs/design/06 §3.9/§3.10;
// ADR-020]. NoSmooth has no smoother (zero). S1..S5 values below.
namespace smoothing {
    inline constexpr double kNoSmoothSeconds   = 0.0;     // (PI) — structural/stepped; no de-zipper
    inline constexpr double kPitchSeconds      = 0.002;   // (PI) S1 ~2 ms
    inline constexpr double kFastSeconds       = 0.010;   // (PI) S2 ~10 ms
    inline constexpr double kPulseWidthSeconds = 0.005;   // (PI) S3 ~5 ms
    inline constexpr double kLevelSeconds      = 0.015;   // (PI) S4 ~15 ms
    inline constexpr double kGlideSeconds      = 0.020;   // (PI) S5 ~20 ms (glide-time CONTROL value only)

    // Snap-to-target threshold so the integer "is-smoothing" state is deterministic
    // [docs/design/06 §3.9; ADR-020 S10]. (PI).
    inline constexpr double kSnapThreshold     = 1.0e-5;  // (PI) — de-zipper snap epsilon
}

// Subsystem namespaces — populated by later DSP tasks. Declared now so those tasks
// have one home to append into. (No measured-spec assertions are attached.)
namespace vco   { /* (PI) oscillator constants appended by task 029. */ }
namespace vcf   { /* (PI) filter/ladder constants appended by tasks 033/035. */ }
namespace env   { /* (PI) ADSR constants appended by task 049. */ }
namespace lfo   { /* (PI) LFO constants appended by task 049. */ }
namespace vca   { /* (PI) VCA taper constants appended by task 049. */ }
namespace vel   { /* (PI) velocity-routing constants appended by the MIDI stream. */ }
namespace drift { /* (PI) drift/variance band constants appended by the vintage stream. */ }
namespace warmup{ /* (PI) warm-up transient constants appended by the vintage stream. */ }

// Plugin-side fixed-capacity event-buffer head room [docs/design/09 §3.2]. The
// buffer is sized maxEvents = kEventBufferBlockFactor*maxBlockSize + kEventBufferSlack.
namespace host {
    inline constexpr int kEventBufferBlockFactor = 4;     // (PI) — per-sample MPE head room
    inline constexpr int kEventBufferSlack       = 256;   // (PI) — dense-automation slack
}

// ---------------------------------------------------------------------------
// Per-renderVersion FROZEN constant-set registry (§8.3; ADR-023 V10).
//
// Each entry pins, for one SHIPPED renderVersion, the frozen DSP constant set that
// reproduces that version's audio. In this foundation the constant set is a thin
// placeholder POD; tasks 033/035/049c append the real tanh/decimator/compensation
// coefficients. The invariant the registry MUST hold from day one: it is keyed by
// renderVersion, exactly ONE entry is CURRENT (== kCurrentRenderVersion), and ONLY
// shipped renderVersions appear.
// ---------------------------------------------------------------------------

struct FrozenConstantSet {
    int  renderVersion = 0;
    bool isCurrent     = false;
    // Placeholder for the frozen DSP coefficients (tanh approx, decimator/halfband,
    // compensation-table source constants). Appended by tasks 033/035/049c.
    // (PI) — added per renderVersion at bless time.
};

// The registry: one entry per SHIPPED renderVersion. renderVersion 1 is CURRENT.
inline constexpr std::array<FrozenConstantSet, 1> kFrozenConstantSets = {{
    FrozenConstantSet{ /*renderVersion=*/1, /*isCurrent=*/true },
}};

// --- Compile-time invariants on the registry (the (PI) discipline made mechanical).

// Exactly one CURRENT entry, and it matches kCurrentRenderVersion.
consteval int mwCountCurrentFrozenSets() {
    int n = 0;
    for (const auto& e : kFrozenConstantSets) {
        if (e.isCurrent) ++n;
    }
    return n;
}
static_assert(mwCountCurrentFrozenSets() == 1,
              "Calibration: the frozen-constant-set registry MUST have exactly one CURRENT entry [ADR-023 V10].");

consteval int mwCurrentFrozenRenderVersion() {
    for (const auto& e : kFrozenConstantSets) {
        if (e.isCurrent) return e.renderVersion;
    }
    return -1;
}
static_assert(mwCurrentFrozenRenderVersion() == mw101::version::kCurrentRenderVersion,
              "Calibration: the CURRENT frozen-constant-set renderVersion MUST equal kCurrentRenderVersion [ADR-023 V10].");

// Only shipped renderVersions are retained: every entry's renderVersion is in
// [1, kCurrentRenderVersion]. Adding an unshipped (e.g. future or 0) entry FAILS
// this assertion [ADR-023 §Consequences].
consteval bool mwAllFrozenSetsShipped() {
    for (const auto& e : kFrozenConstantSets) {
        if (e.renderVersion < 1 || e.renderVersion > mw101::version::kCurrentRenderVersion) {
            return false;
        }
    }
    return true;
}
static_assert(mwAllFrozenSetsShipped(),
              "Calibration: only SHIPPED renderVersions [1..CURRENT] may appear in the registry [ADR-023 §Consequences].");

// Look up the frozen set for a renderVersion; returns nullptr if not retained.
inline constexpr const FrozenConstantSet* frozenSetFor(int renderVersion) noexcept {
    for (const auto& e : kFrozenConstantSets) {
        if (e.renderVersion == renderVersion) return &e;
    }
    return nullptr;
}

} // namespace mw::cal
