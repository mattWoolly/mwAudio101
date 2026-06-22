// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/EditorLayoutConstants.h — the (PI) DESIGN-UNIT PLACEMENT MAP for the
// editor root (MwAudioEditor, task 114c). This is the editor-level geometry seam: WHERE
// each panel module / component sits inside the 1000x640 logical design space (§4.1),
// expressed as named design-unit rectangles [docs/design/10-ui.md §4, §5.1, §5.3; ADR-015
// C1]. The per-module INTERNAL proportions (how a module sub-divides its own rectangle)
// are owned by each module's own constants header (ModulatorModuleConstants.h, ...,
// SequencerGridConstants.h); this header owns ONLY the outer placement of each module.
//
// The five signal-flow modules (MODULATOR -> VCO -> SOURCE MIXER -> VCF -> VCA) are placed
// to align 1:1 with the cached chrome the BackgroundLayer draws: the row origin / height /
// margin / gap fractions here are the SAME (PI) fractions BackgroundLayerConstants.h uses
// to stroke the module outlines + patch lines, so the live controls land exactly inside the
// drawn cells [docs/design/10-ui.md §5.1, §7.2]. The ControllerStrip, TransportModeBar,
// SequencerGrid and PresetBrowser occupy the band below the row; the StatusBanner is a slim
// top-left strip; the ScopeMeterOverlay is the TOP overlay over the top-right region (it
// deliberately overlays the chrome and is excluded from the layout disjointness contract,
// per §5.1 "ScopeMeterOverlay sits on top").
//
// Per the calibration discipline (AGENTS.md "(PI)") none of these literals is inlined at the
// MwAudioEditor::resized() layout site; they centralize here so the visual-design pass can
// retune the panel placement in one place without touching layout code [docs/design/10-ui.md
// §4.1]. This is a NEW dedicated header (NOT the orchestrator-owned Calibration.h, and
// distinct from UiConstants.h which owns the design EXTENT / scale presets) so the parallel
// UI fleet does not serialize on a shared table.
//
// JUCE-FREE: these are plain float proportions / design-unit literals; the editor (JUCE-
// built) applies them through juce::Rectangle math and its single design->pixels
// AffineTransform at the layout seam. Nothing here references juce::* — mwcore stays
// JUCE-free [ADR-001 C1].

#pragma once

#include <array>

#include "UiConstants.h"  // mw::cal::editor::kDesignWidth / kDesignHeight (the design extent)

namespace mw::cal::editor::layout {

// The frozen design extent the placement is expressed over (re-stated from UiConstants so a
// reader sees the canvas these literals live on; never re-minted).
inline constexpr float kW = mw::cal::editor::kDesignWidth;   // 1000 design units
inline constexpr float kH = mw::cal::editor::kDesignHeight;  // 640  design units

// ---------------------------------------------------------------------------
// Signal-flow module row [docs/design/10-ui.md §5.1; §7.2].
//
// These fractions MIRROR BackgroundLayerConstants.h (kRowMarginXFraction / kRowTopFraction
// / kRowHeightFraction / kModuleGapFraction / kModuleCount) so the live module rectangles
// land exactly inside the cells the cached chrome strokes. Kept here (not #included from the
// background header) so the editor's placement seam is self-contained and the two stay in
// lock-step by construction; a divergence would mis-register controls against the chrome.
// ---------------------------------------------------------------------------
inline constexpr int   kModuleCount       = 5;       // MODULATOR, VCO, SOURCE MIXER, VCF, VCA
inline constexpr float kRowMarginXFraction = 0.03f;  // left/right margin (fraction of width)
inline constexpr float kRowTopFraction     = 0.10f;  // row top edge (fraction of height)
inline constexpr float kRowHeightFraction  = 0.55f;  // row height (fraction of height)
inline constexpr float kModuleGapFraction  = 0.025f; // inter-cell gap (fraction of row width)

// Derived absolute design-unit geometry of the module row (one uniform horizontal band of
// kModuleCount equal-width cells). All values are pure compile-time arithmetic.
inline constexpr float kRowMarginX = kW * kRowMarginXFraction;          // 30
inline constexpr float kRowTop     = kH * kRowTopFraction;              // 64
inline constexpr float kRowHeight  = kH * kRowHeightFraction;          // 352
inline constexpr float kRowWidth   = kW - 2.0f * kRowMarginX;          // 940
inline constexpr float kModuleGap  = kRowWidth * kModuleGapFraction;   // 23.5
inline constexpr float kModuleWidth =
    (kRowWidth - static_cast<float>(kModuleCount - 1) * kModuleGap)
    / static_cast<float>(kModuleCount);                                // 169.2

// The left edge (design-unit x) of module cell `i` (0..kModuleCount-1). Cell i spans
// [moduleCellX(i), moduleCellX(i)+kModuleWidth) at [kRowTop, kRowTop+kRowHeight).
[[nodiscard]] constexpr float moduleCellX(int i) noexcept
{
    return kRowMarginX + static_cast<float>(i) * (kModuleWidth + kModuleGap);
}

// Named indices into the row (design order == signal-flow order).
inline constexpr int kIdxModulator   = 0;
inline constexpr int kIdxVco         = 1;
inline constexpr int kIdxSourceMixer = 2;
inline constexpr int kIdxVcf         = 3;
inline constexpr int kIdxVca         = 4;

// ---------------------------------------------------------------------------
// Status banner — a slim strip across the TOP-LEFT, above the module row [§9.4]. Kept clear
// of the top-right scope-overlay region (x stays < kScopeX) so the two top-strip surfaces do
// not collide. Disjoint from every functional module (it sits entirely above kRowTop).
// ---------------------------------------------------------------------------
inline constexpr float kBannerX = kRowMarginX;     // 30
inline constexpr float kBannerY = 8.0f;
inline constexpr float kBannerW = 560.0f;          // right edge 590 < kScopeX (620): no collision
inline constexpr float kBannerH = 44.0f;           // bottom 52 < kRowTop (64): above the row

// ---------------------------------------------------------------------------
// Scope / meter OVERLAY — the TOP Z-order layer over the top-right region [§5.1, §8.4]. The
// rectangle equals the (PI) scope dirty-rect the telemetry Timer already targets
// (TimerConstants.h kScopeRegion*), so the overlay the Timer repaints and the overlay the
// editor positions are ONE region. This is INTENTIONALLY an overlay: it is excluded from the
// module-disjointness contract (§5.1) and may sit over the chrome / the VCA cell corner.
// ---------------------------------------------------------------------------
inline constexpr float kScopeX = 620.0f;   // == mw::cal::timer::kScopeRegionX
inline constexpr float kScopeY = 40.0f;    // == mw::cal::timer::kScopeRegionY
inline constexpr float kScopeW = 340.0f;   // == mw::cal::timer::kScopeRegionW
inline constexpr float kScopeH = 180.0f;   // == mw::cal::timer::kScopeRegionH

// ---------------------------------------------------------------------------
// The band BELOW the module row [kRowTop+kRowHeight, kH) holds the controller strip, the
// transport/mode bar, and the bottom row (sequencer grid + preset browser), as disjoint
// horizontal bands. kRowTop + kRowHeight == 416, so the band is [416, 640] (224 units).
// ---------------------------------------------------------------------------

// Controller strip — a wide, short horizontal strip just under the module row [§5.3].
inline constexpr float kControllerX = kRowMarginX;                 // 30
inline constexpr float kControllerY = 424.0f;                      // below the row (top 416)
inline constexpr float kControllerW = kRowWidth;                   // 940 (full content width)
inline constexpr float kControllerH = 64.0f;                       // bottom 488

// Transport / mode bar — the full-width bar beneath the controller strip [§5.3, §4.4, §10].
inline constexpr float kTransportX = kRowMarginX;                  // 30
inline constexpr float kTransportY = 492.0f;                       // below the controller strip
inline constexpr float kTransportW = kRowWidth;                    // 940
inline constexpr float kTransportH = 56.0f;                        // bottom 548

// Bottom row: the 100-step sequencer grid (left) and the preset browser (right), side by
// side, disjoint in x, sharing the same vertical band [§5.3, §9.1].
inline constexpr float kBottomRowY = 552.0f;                       // below the transport bar
inline constexpr float kBottomRowH = 80.0f;                        // bottom 632 < kH (640)

inline constexpr float kSeqGridX = kRowMarginX;                    // 30
inline constexpr float kSeqGridY = kBottomRowY;
inline constexpr float kSeqGridW = 610.0f;                         // right edge 640
inline constexpr float kSeqGridH = kBottomRowH;

inline constexpr float kPresetX = 650.0f;                          // 10-unit gap after the grid
inline constexpr float kPresetY = kBottomRowY;
inline constexpr float kPresetW = kW - kRowMarginX - kPresetX;     // right edge 970
inline constexpr float kPresetH = kBottomRowH;

} // namespace mw::cal::editor::layout
