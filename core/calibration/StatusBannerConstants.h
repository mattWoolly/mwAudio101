// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/StatusBannerConstants.h — the (PI) layout + severity-mapping
// constants for the non-modal StatusBanner (task 129) [docs/design/10-ui.md §9.4;
// ADR-021 L12]. The banner is laid out in DESIGN units (no pixel math, §2.1); the
// parent passes a design-space rectangle and the banner sub-divides it with the
// proportions named here.
//
// Per the calibration discipline (AGENTS.md "(PI)"), none of these layout literals is
// inlined at the draw / layout call site; they centralize here so the visual-design
// pass can retune the banner's proportions in one place. This is a NEW header (NOT
// core/calibration/Calibration.h) so parallel UI/DSP work does not collide on the
// orchestrator-owned aggregate.
//
// JUCE-FREE: these are plain float proportions / int enum-backing values; the banner
// (JUCE-built) applies them through juce::Rectangle math + DesignTokens at the draw
// seam. Nothing here references juce::* [ADR-001 C1].

#pragma once

namespace mw::cal::ui::status_banner {

// ---------------------------------------------------------------------------
// Layout (proportions of the banner's own design-space rectangle) (PI).
//
// The banner is a single horizontal strip: a leading severity swatch, the message
// text, then a trailing dismiss "x" hit-area. All fractions are of the banner's own
// rectangle so the surface is resolution- and pixel-independent
// [docs/design/10-ui.md §9.4; §2.1].
// ---------------------------------------------------------------------------

// Uniform inner padding around the banner content, as a fraction of the banner
// HEIGHT (keeps the swatch/text/dismiss off the rounded outline) (PI).
inline constexpr float kPaddingFraction = 0.18f;

// Width of the leading severity swatch, as a fraction of the banner HEIGHT (a small
// square-ish accent block whose colour encodes the severity) (PI).
inline constexpr float kSwatchWidthFraction = 0.60f;

// Width of the trailing dismiss "x" hit-area, as a fraction of the banner HEIGHT
// (a square-ish target at the right edge) (PI).
inline constexpr float kDismissWidthFraction = 0.80f;

// Stroke weight of the dismiss glyph, in design units (PI).
inline constexpr float kDismissStrokeWidth = 1.5f;

// Corner radius of the banner's rounded background, in design units (PI). The banner
// owns this rather than the DesignTokens cornerRadius because it is a distinct,
// chunkier affordance than a control.
inline constexpr float kBackgroundCornerRadius = 5.0f;

// ---------------------------------------------------------------------------
// Severity ordering (backing values for mw::ui::StatusBanner::Severity).
//
// Higher == more urgent. The banner uses this only to map a severity to a swatch
// colour ROLE in the injected DesignTokens; it never authors a concrete colour here
// [docs/design/10-ui.md §6.1 — palette owned by the token table]. Ordering is also
// what a future "coalesce to the most-urgent" policy would compare against
// [ADR-021 L12 coalescing], though authoring that policy is out of scope for 129.
// ---------------------------------------------------------------------------
inline constexpr int kSeverityInfo  = 0;
inline constexpr int kSeverityWarn  = 1;
inline constexpr int kSeverityError = 2;

} // namespace mw::cal::ui::status_banner
