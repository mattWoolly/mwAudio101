// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ParamDefsConstants.h — the (PI) skew / default / ceiling block
// the declarative parameter registry (core/params/ParamDefs.h, task 019) reads from
// (docs/design/06 §3.3, §3.10).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h (it #includes that root and APPENDS into the same
// mw::cal subsystem namespaces, so the two compose additively). ParamDefs references
// these named constants so NO skew factor, NO (PI) default, and NO (PI) ceiling is
// inlined at the registry's declaration site [docs/design/06 §3.10; ADR-020 S13;
// ADR-008 §1].
//
// EVERY constant here is (PI) — a *pragmatic invention* / tunable default, NOT a
// measured SH-101 spec. The contract surface (which params are log vs linear, which
// defaults are PI) traces to docs/design/06 §3.3 / §3.4 / §3.10. A re-skew of a
// shipped range is additionally a migration event (§7); a re-tune is one localized
// edit here.

#pragma once

#include "Calibration.h"

namespace mw::cal {

// ---------------------------------------------------------------------------
// §3.3 — NormalisableRange skew factors for the continuous params (all (PI)).
//
// A juce::NormalisableRange skew of 1.0 is linear. A skew < 1.0 expands the lower
// end of the range (the "log-ish" feel the doc asks for on cutoff / time / rate
// pots). Symmetric-skew params (vco.fine) keep skew 1.0 and set symmetricSkew so the
// taper mirrors about the centre. These are musical-feel choices with no physical
// oracle [docs/design/06 §3.3].
// ---------------------------------------------------------------------------
namespace skew {
    // Linear taper — the overwhelming majority of params. 1.0 == juce linear range.
    inline constexpr float kLinear        = 1.0f;   // (PI) — linear NormalisableRange

    // Log-ish frequency / time tapers. Sub-1.0 skew packs resolution into the low end.
    inline constexpr float kCutoff        = 0.30f;  // (PI) — vcf.cutoff log-ish
    inline constexpr float kEnvTime       = 0.35f;  // (PI) — env attack/decay/release log
    inline constexpr float kLfoRate       = 0.40f;  // (PI) — lfo.rate 0.1..30 Hz log
    inline constexpr float kLfoDelay      = 0.35f;  // (PI) — lfo.delay log
    inline constexpr float kGlideTime     = 0.30f;  // (PI) — glide.time 0..5 s log
    inline constexpr float kDriftRate     = 0.40f;  // (PI) — drift.rate 0.01..1 Hz log
    inline constexpr float kDelayTime     = 0.35f;  // (PI) — fx.delay_time log
}

// ---------------------------------------------------------------------------
// §3.3 — (PI) continuous-param defaults that have NO measured oracle and are flagged
// (PI) in the §3.0 / §3.3 tables. The non-(PI) defaults (e.g. tune 0, cutoff 1.0) are
// contract values quoted directly from §3.0 and are not duplicated here.
// ---------------------------------------------------------------------------
namespace paramdefault {
    // mw101.vel.depth default — velocity->(VCA+VCF) amount, flagged (PI) [§3.0; §3.3].
    inline constexpr float kVelDepth = 0.5f;  // (PI) tunable default
}

// ---------------------------------------------------------------------------
// §3.3 — (PI) continuous-param range ceilings flagged (PI) in the doc.
// ---------------------------------------------------------------------------
namespace paramrange {
    // mw101.fx.delay_feedback maximum — keeps the recirculation loop stable; doc 07
    // §5.2.7 must match this ceiling [docs/design/06 §3.3 notes; ADR-010 FX-8].
    inline constexpr float kDelayFeedbackMax = 0.95f;  // (PI) tunable ceiling
}

} // namespace mw::cal
