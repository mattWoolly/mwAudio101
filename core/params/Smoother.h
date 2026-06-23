// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/params/Smoother.h — control-rate one-pole de-zipper (task 008).
//
// Realizes docs/design/00 §5.4/§4.4 and ADR-001 C7 / ADR-020. A step change in a
// normalized value yields a smoothed (non-zippered) engineering value; a constant
// input is passed through unchanged. The de-zipper advances on the control-rate
// chunk-boundary tick (one tick == one process() call here), modeling the SH-101's
// coarse control-loop cadence [docs/design/00 §4.4]. All state is POD; the type is
// noexcept and allocation-free [ADR-020 S14].

#pragma once

#include <cmath>

#include "../calibration/Calibration.h"  // (PI) snap epsilon — read, not inlined [ADR-020 S13]

namespace mw::params {

class OnePoleSmoother {
public:
    OnePoleSmoother() noexcept = default;

    // Configure the per-tick smoothing coefficient from a time constant (seconds)
    // and the control-rate tick rate (Hz). Called at prepareToPlay, off the audio
    // thread. timeConstantSeconds <= 0 => no smoothing (snap; the NoSmooth class).
    void prepare(double timeConstantSeconds, double tickRateHz) noexcept {
        snapThreshold_ = cal::smoothing::kSnapThreshold; // (PI) de-zipper snap epsilon, centralized
        if (timeConstantSeconds <= 0.0 || tickRateHz <= 0.0) {
            coeff_ = 0.0;        // 0 => snap to target every tick (no de-zipper)
        } else {
            // One-pole exponential: y += (1-a)(target-y). a = exp(-1/(tau*fs)).
            coeff_ = std::exp(-1.0 / (timeConstantSeconds * tickRateHz));
        }
    }

    // Set the current value AND target with no smoothing transient. prepareToPlay /
    // reset path.
    void reset(double value) noexcept {
        current_ = value;
        target_  = value;
    }

    // Set the target the smoother approaches; does not move the current value.
    void setTarget(double target) noexcept { target_ = target; }

    // Advance one control-rate tick and return the new smoothed value.
    double process() noexcept {
        const double delta = target_ - current_;
        if (coeff_ <= 0.0 || std::fabs(delta) <= snapThreshold_) {
            current_ = target_;  // snap when not smoothing or within the snap band
        } else {
            current_ = target_ - coeff_ * delta; // y = target - a*(target - y_prev)
        }
        return current_;
    }

    [[nodiscard]] double current() const noexcept { return current_; }
    [[nodiscard]] double target()  const noexcept { return target_; }

    // True while the value has not yet snapped to the target (deterministic
    // is-smoothing state per ADR-020 S10).
    [[nodiscard]] bool isSmoothing() const noexcept {
        return std::fabs(target_ - current_) > snapThreshold_;
    }

private:
    double current_       = 0.0;
    double target_        = 0.0;
    double coeff_         = 0.0;     // 0 => snap; (0,1) => one-pole de-zipper
    double snapThreshold_ = cal::smoothing::kSnapThreshold;  // (PI) centralized [ADR-020 S13]
};

} // namespace mw::params
