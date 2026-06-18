// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/params/SmoothingClass.h — SmoothingClass enum + per-class de-zipper
// time-constant accessor (task 015). Realizes docs/design/06 §3.9/§3.10, ADR-020.
//
// The numeric time constants are NOT inlined here: each class maps to a named (PI)
// constant in core/calibration/Calibration.h [docs/design/06 §3.10; ADR-020 S13].

#pragma once

#include <cstdint>

#include "../calibration/Calibration.h"

namespace mw::params {

// Enum order is FROZEN by docs/design/06 §3.9: NoSmooth == 0 is the default class.
enum class SmoothingClass : std::uint8_t {
    NoSmooth = 0,   // default; stepped/structural/envelope-time
    Pitch,          // S1  ~2 ms  (PI)
    Fast,           // S2  ~10 ms (PI)
    PulseWidth,     // S3  ~5 ms  (PI)
    Level,          // S4  ~15 ms (PI)
    Glide           // S5  ~20 ms (PI)  (the glide-time CONTROL value only)
};

// Returns the de-zipper time constant (seconds) for a class, reading the named
// constant from the calibration table — never an inlined literal.
inline constexpr double smoothingTimeConstantSeconds(SmoothingClass c) noexcept {
    switch (c) {
        case SmoothingClass::NoSmooth:   return mw::cal::smoothing::kNoSmoothSeconds;
        case SmoothingClass::Pitch:      return mw::cal::smoothing::kPitchSeconds;
        case SmoothingClass::Fast:       return mw::cal::smoothing::kFastSeconds;
        case SmoothingClass::PulseWidth: return mw::cal::smoothing::kPulseWidthSeconds;
        case SmoothingClass::Level:      return mw::cal::smoothing::kLevelSeconds;
        case SmoothingClass::Glide:      return mw::cal::smoothing::kGlideSeconds;
    }
    return mw::cal::smoothing::kNoSmoothSeconds;
}

} // namespace mw::params
