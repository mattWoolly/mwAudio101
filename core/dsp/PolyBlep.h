// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/PolyBlep.h — stateless, header-only closed-form PolyBLEP residual
// (task 026). Implements the ADR-002 Contract residual verbatim, per
// docs/design/01-dsp-oscillators.md §3.1 and §10.
//
// The two-segment polynomial residual is the per-voice DEFAULT anti-aliasing
// correction for the band-limited saw, the variable-width pulse (PWM), and the
// divider-derived sub edges [ADR-002 Decision, C1-C2, C7]. It is intentionally
// stateless, fully inlineable, constexpr and noexcept: it allocates nothing and
// takes no locks, so it is safe on the audio thread by construction
// [ADR-002 C11; docs/design/01 §2.4]. No table, no per-instance state.
//
// Residual formula traces to [ADR-002 Contract] and [research/10 §2.3, §8 Table].

#pragma once

namespace mw101::dsp {

// Closed-form two-segment PolyBLEP residual.
//
//   t  : phase in [0,1)
//   dt : freq/fs (per-sample phase increment), dt > 0
//
// Returns the residual to combine with the trivial (naive) waveform per the
// ADR-002 Contract C1-C2 application mechanics:
//   - leading segment  (t <  dt):     2*(t/dt) - (t/dt)^2 - 1
//   - trailing segment (t > 1 - dt):  ((t-1)/dt)^2 + 2*((t-1)/dt) + 1
//   - interior         (otherwise):   exactly 0.0f
[[nodiscard]] constexpr float polyBlep (float t, float dt) noexcept
{
    if (t < dt)                 // leading segment
    {
        const float x = t / dt;
        return 2.0f * x - x * x - 1.0f;        // 2*t' - t'^2 - 1
    }
    if (t > 1.0f - dt)          // trailing segment
    {
        const float x = (t - 1.0f) / dt;
        return x * x + 2.0f * x + 1.0f;         // t''^2 + 2*t'' + 1
    }
    return 0.0f;
}

} // namespace mw101::dsp
