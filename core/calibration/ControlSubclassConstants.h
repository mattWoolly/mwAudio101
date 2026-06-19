// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ControlSubclassConstants.h — the (PI) constants the custom control
// subclasses (ui/controls/*, task 109) need that are NOT already owned by the
// design-token table (ui/DesignTokens.h), the token-default header
// (UiTokenConstants.h), or the LookAndFeel drawing-geometry header
// (LookAndFeelConstants.h).
//
// Scope of this header: the small set of pragmatic-invention defaults the thin
// Slider/Button/ComboBox subclasses carry — the rotary drag sensitivity / pixel
// span, the number of decimal places a value read-out shows, and the brightness
// floor applied when a sound_ext choice entry is visually fenced with the
// extensionTag token [docs/design/10-ui.md §6.3, §7.3; ADR-008 §7/C6/C15].
//
// Per the calibration discipline (AGENTS.md "(PI)"), none of these literals is
// inlined at a control call site; they centralize here so the visual-design pass can
// retune them in one place. This is a NEW header (NOT core/calibration/Calibration.h)
// so parallel UI/DSP work does not collide on the orchestrator-owned aggregate.
//
// These are JUCE-free plain values; the control subclasses apply them through JUCE
// calls at the seam.

#pragma once

#include <cstdint>

namespace mw::cal::control {

// ---------------------------------------------------------------------------
// Rotary slider drag ergonomics (PI). The SH-101's slider-per-parameter feel is
// retained, but the rotary controls use a vertical/horizontal drag rather than a
// circular drag for predictability [docs/design/10-ui.md §6.3].
// ---------------------------------------------------------------------------
namespace rotary {
    // Pixels of vertical drag to sweep the full 0..1 range (PI). Larger == finer.
    inline constexpr int kDragPixelSpan = 200;
}

// ---------------------------------------------------------------------------
// Value read-out formatting (PI). The displayed value text is derived from the bound
// parameter's display string; when no parameter is attached the subclass falls back
// to a numeric read-out with this many decimal places.
// ---------------------------------------------------------------------------
namespace readout {
    inline constexpr int kFallbackDecimalPlaces = 2;  // (PI) decimals w/o a param
}

// ---------------------------------------------------------------------------
// Software-extension fencing (PI). A sound_ext choice entry is visually marked with
// the DesignTokens.extensionTag colour; the marker glyph is appended to the entry
// label so the fencing survives any LookAndFeel that ignores per-item colour. The
// glyph is intentionally a plain ASCII tag (no trademark, no Roland mark).
// ---------------------------------------------------------------------------
namespace extension {
    // Suffix appended to a software-only choice label so it reads as an extension,
    // never as 1982 hardware behaviour [ADR-008 §7, C6, C15; research/12 §3.1].
    inline constexpr const char* kLabelSuffix = "  [ext]";
}

} // namespace mw::cal::control
