// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/Glide.h — per-voice portamento slew (task 068).
//
// Glide is per-voice and slews the pitch target with an RC-style exponential
// integrator [docs/design/04-voice-and-control.md §5.5; research/05 §5]. The class
// signature matches §5.5 exactly. OFF/ON/AUTO modes, a 0-5 s TIME mapping, and a
// snap (no-glide) path are owned here; deciding legato/arpActive is the CALLER's
// job (this object only applies the mode rules to the flags it is handed).
//
// Curve honesty: the hardware glide CURVE is undocumented [research/05 §5 honest
// label]; the model uses an RC-style exponential slew toward the target with the
// time constant mapped from the 0-5 s TIME via a (PI) mapping centralized in
// core/calibration/GlideConstants.h. In VINTAGE control the glide smooths *between*
// the 6-bit quantized holds, exactly as the hardware RC does — the pitch itself is
// not smoothed away; ControlCore owns the stair-step, Glide only smooths between
// holds [ADR-005 §Decision item 2; task scope "Out of scope"].
//
// RT contract: every hot-path method is noexcept, allocation-free, lock-free; all
// sizing happens in prepare (here, just the per-tick coefficient) [docs/design/04
// §3.4; ADR-001].

#pragma once

#include <cstdint>

namespace mw {

// OFF / ON / AUTO [research/05 §5].
enum class GlideMode : std::uint8_t { Off = 0, On = 1, Auto = 2 };

class Glide {
public:
    Glide() noexcept = default;

    // Off the audio thread. Caches the sample rate used to derive the per-tick
    // exponential coefficient from the TIME-mapped time constant.
    void prepare(double sampleRate) noexcept;

    void setMode(GlideMode m) noexcept { mode_ = m; }

    // 0..5 s [research/05 §5]; clamped into the documented range. Recomputes the
    // RC coefficient from the (PI) TIME->tau mapping (GlideConstants.h).
    void setTimeSeconds(float t) noexcept;

    // Set the pitch target and apply the mode rules:
    //   - arpActive == true  => snap (no glide), regardless of mode [research/05 §5]
    //   - GlideMode::Off     => snap (no glide)
    //   - GlideMode::On      => always glide toward the target
    //   - GlideMode::Auto    => glide only when legato == true; else snap
    // The caller supplies legato/arpActive (task scope: Glide does not decide them).
    void setTarget(float targetPitchHz, bool legato, bool arpActive) noexcept;

    // One slewed pitch sample/tick. Snaps to target inside the snap band so the
    // value lands exactly on target and "is-gliding" is deterministic.
    float nextValue() noexcept;

    // Jump (no glide) for arp / first note: current_ := pitchHz, and the target is
    // pulled to it so a subsequent nextValue() returns it immediately.
    void snapTo(float pitchHz) noexcept;

    [[nodiscard]] GlideMode mode()    const noexcept { return mode_; }
    [[nodiscard]] float     current() const noexcept { return current_; }
    [[nodiscard]] float     target()  const noexcept { return target_; }

private:
    GlideMode mode_    = GlideMode::Off;
    float     current_ = 0.0f;
    float     target_  = 0.0f;
    float     coeff_   = 0.0f;       // exp(-1/(tau*fs)); 0 => snap, (0,1) => slew
    double    sampleRate_ = 0.0;
    bool      gliding_ = false;      // mode/flags say slew this hold? (else snap)
};

} // namespace mw
