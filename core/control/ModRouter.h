// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/ModRouter.h — the fixed LFO/ADSR modulation router (task 082).
//
// Realizes docs/design/05 §3.1 (the FIXED routing model — one instantaneous LFO
// value scaled into three destinations through independent per-destination depth
// gains, one shared ADSR routed to three destinations, plus the SH-101-specific
// LFO→VCA tremolo path) and §3.2 (the class signature + the branch-light, alloc-free
// resolve() expression). This is FIXED routing, NOT a patch matrix [ADR-007
// §Decision 1, C1].
//
// SCOPE (task 082): the ModRouter class only. The enums and scalar PODs
// (PwmSource/VcaSource, ModInputs/ModDepths/ModOutputs) are owned by the stream's
// shared POD header core/control/ControlTypes.h (task 081); this header CONSUMES
// them rather than re-declaring them, so there is a single source of truth and no
// ODR clash. Out of scope here: LFO core generation and ADSR segment generation
// (consumed as scalar inputs); the physical V/oct, Hz/V, %/V scaling constants
// (PI; centralized in core/calibration/Calibration.h); and parameter-ID binding
// (owned by the param schema, docs/design/06 §2).
//
// Real-time invariants (ADR-001, ADR-007 C26): all state is POD; prepare() sizes
// nothing on the heap and never allocates after; resolve() is a fixed arithmetic
// expression on the hot path — noexcept, no heap allocation, no lock.

#pragma once

#include "control/ControlTypes.h"

namespace mw::control {

// Fixed modulation router (docs/design/05 §3.2). Maps one instantaneous selected-LFO
// value plus the shared ADSR value into pitch / PWM / cutoff / VCA-tremolo through
// independent fixed depth gains and the PWM/VCA source switches.
class ModRouter {
public:
    // Pre-size the router (no heap members, so this only records the sample rate for
    // any future control-rate use and establishes a benign reset state). No
    // allocation here or after [ADR-007 C26]. The exact physical mod-depth scaling
    // (V/oct, Hz/V, %/V) is (PI) and folded in downstream by the calibration table;
    // the depths carried here are normalized gains (docs/design/05 §3.3).
    void prepare(double sampleRate) noexcept;

    // PWM source switch: PWM consumes the LFO only when = Lfo, the ADSR only when
    // = Env, a static MANUAL value when = Manual (docs/design/05 §3.1, §3.2; C2).
    void setPwmSource(PwmSource s) noexcept;

    // VCA control switch (ENV / GATE). Routed here; the ENV/GATE level mixing lives
    // at the VCA node, which also sums the LFO→VCA tremolo (docs/design/05 §3.1).
    void setVcaSource(VcaSource s) noexcept;

    // Replace the per-destination depth gains (from the param snapshot; IDs/ranges
    // owned by docs/design/06 §2). Plain copy of a POD; noexcept, no allocation.
    void setDepths(const ModDepths& d) noexcept;

    // Hot path (docs/design/05 §3.2): the fixed expression, branch-light, pure
    // arithmetic, noexcept, no allocation, no lock. The same instantaneous
    // in.lfoValue reaches pitch / PWM / cutoff scaled by independent depths; the
    // single shared in.envValue drives cutoff/PWM (and the VCA node downstream).
    [[nodiscard]] ModOutputs resolve(const ModInputs& in) const noexcept;

    [[nodiscard]] PwmSource pwmSource() const noexcept { return pwmSource_; }
    [[nodiscard]] VcaSource vcaSource() const noexcept { return vcaSource_; }

private:
    ModDepths depths_{};
    PwmSource pwmSource_{PwmSource::Lfo};   // signature default (docs/design/05 §3.2)
    VcaSource vcaSource_{VcaSource::Env};   // signature default (docs/design/05 §3.2)
    double sampleRate_{48000.0};
};

} // namespace mw::control
