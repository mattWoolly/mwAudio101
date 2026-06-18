// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/EnvLfoVcaConstants.h — the Envelope (ADSR) / LFO (Modulator) /
// VCA (BA662A-class) / velocity-routing (PI) tunable-constant block (task 049).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h. It carries the env/lfo/vca/vel subsystem defaults
// in the SAME namespaces (mw::cal::env, ::lfo, ::vca, ::vel) that Calibration.h
// reserves for task 049, so the two headers compose additively; the calibration
// orchestrator wires this include from Calibration.h later. No DSP call site inlines
// any of these literals [docs/design/03 §1.2; ADR-020 S13; docs/design/06 §3.10].
//
// EVERY constant here is (PI) — a *pragmatic invention* / TUNABLE DEFAULT, NOT a
// measured SH-101 spec. The values trace to the default tables in
// docs/design/03-dsp-envelope-lfo-vca.md §2.4 (envelope shaping), §3.5 (LFO
// shape/rate/mod-bus), §4.3 (VCA taper/anti-thump) and §5.2 (velocity routing).
// They are honest about being inference/engineering choices [docs/design/03 §1.3;
// research/04 §5.1]; none is asserted as a measured fact. A re-tune is one localized
// edit here.

#pragma once

namespace mw::cal {

// ---------------------------------------------------------------------------
// §2.4 — ADSR segment curve law shaping constants. The exponential-RC segment
// shape is INFERRED, UNMEASURED theory [docs/design/03 §2.4; research/04 §2.6, §5.1];
// these are the tunable knobs that keep that honesty explicit.
// ---------------------------------------------------------------------------
namespace env {
    // Attack asymptote above unity (snappy charge): the Attack stage approaches this
    // target and snaps to a clamped 1.0 on the way [docs/design/03 §2.4].
    inline constexpr float kEnvAttackOvershoot = 1.25f;    // (PI) tunable default

    // Maps a user "time" knob to the 1/e time constant so the audible segment ~= the
    // labeled time: coeff = exp(-1 / (max(T,Tmin) * fc * kEnvTimeScale)).
    inline constexpr float kEnvTimeScale       = 0.20f;    // (PI) tunable default

    // Default EnvParams::curve shaping exponent (1.0 = near-RC). The curve law being
    // configurable is the required honest stance [docs/design/03 §2.4].
    inline constexpr float kEnvCurve           = 1.0f;     // (PI) tunable default

    // Level distance at which a stage snaps to its target / advances. Shared with the
    // de-zipper snap policy so stage bookkeeping is deterministic [ADR-020 S10, S12].
    inline constexpr float kEnvSnapThreshold   = 1.0e-4f;  // (PI) tunable default

    // Floor (seconds) on any segment time, bounding the one-pole coefficient.
    inline constexpr float kEnvTimeMin         = 1.0e-4f;  // (PI) tunable default
}

// ---------------------------------------------------------------------------
// §3.5 — LFO waveform-core / rate / modulation-bus constants. Mod-depth amounts
// (V/oct, Hz/V, %/V) remain measurement-required open gaps [docs/design/03 §3.5,
// §5.3]; only the shaping/bus constants live here.
// ---------------------------------------------------------------------------
namespace lfo {
    // SmoothTri triangle->sine rounding blend (0 = triangle, 1 = sine). Default leans
    // "rounded toward sine," never mathematically pure sine [docs/design/03 §3.5].
    inline constexpr float kLfoSmoothShape = 0.85f;        // (PI) tunable default

    // Rate pot taper (exp-ish feel) if the skew is not encoded on the doc-06 APVTS
    // parameter; unused if doc 06 supplies Hz directly [docs/design/03 §3.4, §3.5].
    inline constexpr float kLfoRateSkew    = 0.3f;         // (PI) tunable default

    // Fixed modulation-bus low-pass corner (Hz), ~-3 dB, applied to all modulation
    // signals on the mod bus (ModRouting), not per-shape [docs/design/03 §3.5].
    inline constexpr float kModBusLpHz     = 16000.0f;     // (PI) tunable default
}

// ---------------------------------------------------------------------------
// §4.3 — BA662A-class OTA VCA taper + anti-thump. Whether an exp converter precedes
// the BA662 is UNCONFIRMED [docs/design/03 §4.3; research/04 §5.1]; the taper is a
// tunable set, never a documented original behavior.
// ---------------------------------------------------------------------------
namespace vca {
    // Control->gain curve exponent: taper(control) = pow(clamp(control,0,1),
    // kVcaTaperExp). 1.0 = linear-in-current OTA; >1 pre-shaped toward dB-linear.
    inline constexpr float kVcaTaperExp    = 2.0f;         // (PI) tunable default

    // OTA tanh drive: out = tanh(kVcaOtaDrive * taper * in) / tanh(kVcaOtaDrive). Low
    // values keep the VCA in the OTA linear window [docs/design/03 §4.3].
    inline constexpr float kVcaOtaDrive    = 1.0f;         // (PI) tunable default

    // Gate open/close fade time (ms) — the anti-thump / click-safe ENV<->GATE fade
    // [docs/design/03 §4.3, §4.6].
    inline constexpr float kVcaAntiThumpMs = 2.0f;         // (PI) tunable default

    // Residual DC offset nulled at the gate transition (0 = clean by default)
    // [docs/design/03 §4.3, §4.6].
    inline constexpr float kVcaOffsetNull  = 0.0f;         // (PI) tunable default
}

// ---------------------------------------------------------------------------
// §5.2 — Default velocity routing into VCA level and VCF cutoff amount. Velocity is
// ON by default [ADR-016 R-2]; these scale the documented physical nodes additively,
// never as invented structure [docs/design/03 §5.2].
// ---------------------------------------------------------------------------
namespace vel {
    // Default VelocityRouting::toVcaAmount: velocity -> VCA level scale
    // [docs/design/03 §5.2].
    inline constexpr float kVelToVca    = 0.7f;            // (PI) tunable default

    // Default VelocityRouting::toCutoffAmount: velocity -> VCF cutoff-amount scale
    // [docs/design/03 §5.2].
    inline constexpr float kVelToCutoff = 0.5f;            // (PI) tunable default

    // Velocity input curve shaping (0..127 -> 0..1) [docs/design/03 §5.2].
    inline constexpr float kVelCurve    = 1.0f;            // (PI) tunable default
}

} // namespace mw::cal
