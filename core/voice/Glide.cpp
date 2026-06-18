// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/Glide.cpp — implementation of the per-voice portamento slew
// (task 068). Realizes docs/design/04-voice-and-control.md §5.5 and ADR-005
// §Decision item 2. The RC-style exponential coefficient is derived from the
// 0-5 s TIME via the (PI) mapping in core/calibration/GlideConstants.h; no (PI)
// literal is inlined here [docs/design/00 §1.2; AGENTS.md "(PI) discipline"].

#include "voice/Glide.h"

#include <algorithm>
#include <cmath>

#include "calibration/GlideConstants.h"

namespace mw {

void Glide::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;
    coeff_      = 0.0f;       // no glide until a TIME is set
    current_    = 0.0f;
    target_     = 0.0f;
    gliding_    = false;
}

void Glide::setTimeSeconds(float t) noexcept {
    // Clamp into the documented 0-5 s span [research/05 §5; GlideConstants.h].
    const float clamped =
        std::clamp(t, cal::glide::kTimeMinSeconds, cal::glide::kTimeMaxSeconds);

    // RC-style one-pole: tau (seconds) from the (PI) TIME->tau mapping; the per-tick
    // coefficient is exp(-1/(tau*fs)) — the same convention as the control-rate
    // de-zipper [params/Smoother.h]. tau<=0 or fs<=0 => snap (coeff 0).
    const double tau = static_cast<double>(cal::glide::kTimeToTauScale) * clamped;
    if (tau <= 0.0 || sampleRate_ <= 0.0) {
        coeff_ = 0.0f;
    } else {
        coeff_ = static_cast<float>(std::exp(-1.0 / (tau * sampleRate_)));
    }
}

void Glide::setTarget(float targetPitchHz, bool legato, bool arpActive) noexcept {
    target_ = targetPitchHz;

    // Mode rules (§5.5 table; research/05 §5). The arpeggiator disables glide
    // entirely, regardless of mode. AUTO glides only on legato; ON always; OFF
    // never.
    bool glide = false;
    if (!arpActive) {
        switch (mode_) {
            case GlideMode::Off:  glide = false;        break;
            case GlideMode::On:   glide = true;         break;
            case GlideMode::Auto: glide = legato;       break;
        }
    }
    gliding_ = glide;

    if (!gliding_) {
        current_ = target_;   // snap path: no slew this hold
    }
}

float Glide::nextValue() noexcept {
    const float delta = target_ - current_;
    // Snap when not gliding, when the coefficient is degenerate (TIME=0), or when
    // within the snap band so the slew lands exactly on the target.
    if (!gliding_ || coeff_ <= 0.0f
        || std::fabs(delta) <= cal::glide::kSnapThreshold) {
        current_ = target_;
    } else {
        // y = target - coeff*(target - y_prev): exponential approach, monotone
        // shrink, no overshoot (coeff in (0,1)).
        current_ = target_ - coeff_ * delta;
    }
    return current_;
}

void Glide::snapTo(float pitchHz) noexcept {
    current_ = pitchHz;
    target_  = pitchHz;
    gliding_ = false;
}

} // namespace mw
