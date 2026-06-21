// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ScopeMeterConstants.h — the (PI) layout / sizing constants for
// the telemetry-driven ScopeMeterOverlay (task 127). These are pure presentation
// geometry — meter-bar widths, gaps, the scope trace stroke, the cutoff-indicator
// arc proportions — that parameterize the overlay's vector drawing. Colours and
// stroke weights are read SOLELY from the injected DesignTokens (ui/DesignTokens.h);
// this header owns ONLY shape/space proportions [docs/design/10-ui.md §5.1, §6.2;
// ADR-015 C11].
//
// Per the calibration discipline these (PI) literals are centralized here and NEVER
// inlined at the draw site [AGENTS.md "(PI)"]. This is a NEW dedicated header (NOT
// core/calibration/Calibration.h, which the orchestrator wires later) so the parallel
// fleet does not serialize on the shared aggregate table.
//
// All values are expressed as PROPORTIONS of the overlay's own bounds (0..1) or as
// design-unit ratios, so the overlay lays out in DESIGN units under the editor's
// single AffineTransform — no absolute pixel coordinate is stored [§4; ADR-015 C1].
// JUCE-free plain floats: mwcore stays JUCE-free by construction [ADR-001 C1].

#pragma once

namespace mw::cal::scope_meter {

// ---------------------------------------------------------------------------
// Overall split between the scope trace region (left) and the meter column
// (right). The cutoff-indicator readout occupies a thin strip along the bottom.
// All fractions of the overlay's own width/height (PI).
// ---------------------------------------------------------------------------

// Fraction of the overlay WIDTH given to the scope trace; the remainder (minus the
// inter-region gap) is the level-meter column (PI).
inline constexpr float kScopeWidthFraction = 0.72f;

// Gap between the scope region and the meter column, as a fraction of width (PI).
inline constexpr float kRegionGapFraction = 0.02f;

// Fraction of the overlay HEIGHT reserved at the bottom for the cutoff indicator
// readout strip; the scope + meters occupy the height above it (PI).
inline constexpr float kCutoffStripHeightFraction = 0.16f;

// Uniform inner padding inset (fraction of the smaller bounds extent) applied before
// laying out the regions, so the trace/meters do not touch the overlay edge (PI).
inline constexpr float kInnerPadFraction = 0.04f;

// ---------------------------------------------------------------------------
// Level meters (two vertical bars: L then R) inside the meter column (PI).
// ---------------------------------------------------------------------------

// Number of meter bars rendered (post-VCA L / R) (PI).
inline constexpr int kNumMeterBars = 2;

// Gap between the two meter bars as a fraction of the meter-column width (PI).
inline constexpr float kMeterBarGapFraction = 0.18f;

// A peak level of 0 still paints a thin "floor" sliver so the meter reads as present
// rather than absent; expressed as a fraction of the meter height (PI).
inline constexpr float kMeterFloorFraction = 0.0f;

// ---------------------------------------------------------------------------
// Scope trace (PI).
// ---------------------------------------------------------------------------

// The scope trace stroke thickness as a MULTIPLE of the token controlStroke, so a
// token reskin still drives the absolute weight (PI; shape-only multiplier) [§6.2].
inline constexpr float kScopeStrokeFactor = 1.0f;

// Vertical amplitude of the scope trace as a fraction of the scope region height; the
// decimated wave (nominally -1..+1, but clamped) is mapped to +/- this about the
// region's vertical centre (PI).
inline constexpr float kScopeAmplitudeFraction = 0.45f;

// ---------------------------------------------------------------------------
// Cutoff indicator (a horizontal fill bar in the bottom strip) (PI).
// ---------------------------------------------------------------------------

// Height of the cutoff fill bar as a fraction of the cutoff strip height (PI).
inline constexpr float kCutoffBarHeightFraction = 0.5f;

// ---------------------------------------------------------------------------
// Reduce-motion / idle state (PI).
// ---------------------------------------------------------------------------

// In the static/idle (reduce-motion) state the scope renders a flat centre line
// rather than an animated wave; this is its stroke factor (multiple of controlStroke)
// (PI). The meters and cutoff indicator still show the last-known levels statically.
inline constexpr float kIdleBaselineStrokeFactor = 1.0f;

} // namespace mw::cal::scope_meter
