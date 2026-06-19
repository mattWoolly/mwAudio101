// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/VcaGate.h — VCA anti-thump gate fade + ENV/GATE amplitude-source handling
// (task 060). Realizes docs/design/03 §4.6 (anti-thump low-offset selection), §4.4
// (ENV vs GATE amplitude source) and §6.5 (click-safe ENV<->GATE switch).
//
// WHY a separate component: the BA662A-class taper transfer Vca::process (taper+tanh,
// task 056) is intentionally STATELESS in `control`. The anti-thump mechanism is a
// STATEFUL pre-processor that shapes the *control* signal at gate/mode edges before it
// reaches Vca::process. This file owns that pre-processor and nothing else; it feeds a
// click-safe control into the existing Vca (§4.1/§4.2). Splitting it out keeps Vca's
// pure taper untouched and the two concerns independently testable.
//
// MODEL (docs/design/03 §4.6, §4.4):
//   - Per gate-edge / mode-flip, the control approaches a per-tick TARGET through a
//     short ONE-POLE fade (time constant kVcaAntiThumpMs), so onset/offset and the
//     ENV<->GATE switch are click-safe rather than a thump (§4.6, §6.5).
//   - The per-tick target is the amplitude source (§4.4):
//       ENV  -> follows the ADSR-shaped control passed in (envControl);
//       GATE -> a flat full level (1.0) for the gate duration (organ-style; the
//               envelope shape is bypassed).
//     While the gate is CLOSED the target is the nulled floor (kVcaOffsetNull), so a
//     note-off fades down to a clean floor.
//   - Residual DC offset is nulled with kVcaOffsetNull at the gate transition (§4.6);
//     the default 0 makes the closed-gate floor exactly clean.
//
// The fade SHARES the single canonical one-pole smoother kind, mw::params::OnePoleSmoother
// (task 008), per ADR-020 S10 / docs/design/03 §6.4 — no second smoother flavor. The
// design doc names it `mw::dsp::OnePoleSmoother`; the realized canonical type lives at
// core/params/Smoother.h, so (exactly as core/dsp/drift/DriftState.h does) this header
// records the design-doc name via a using-alias and CONSUMES the existing declaration
// rather than re-declaring it.
//
// All (PI) numbers (kVcaAntiThumpMs, kVcaOffsetNull) resolve from
// core/calibration/EnvLfoVcaConstants.h — never inlined at a call site [ADR-020 S13;
// docs/design/03 §1.2].
//
// RT invariants [ADR-001, ADR-019 VT-01, ADR-020 S14]: all state is POD; the fade
// coefficient is precomputed in prepare(); no heap allocation and no locks on the audio
// thread; tickControl()/processControlBlock() are noexcept hot paths.

#pragma once

#include "Vca.h"                 // VcaMode (ENV/GATE) is declared by task 052
#include "params/Smoother.h"     // the canonical mw::params::OnePoleSmoother (task 008)

namespace mw101::dsp {

class VcaGate
{
public:
    // The single canonical one-pole smoother kind shared per ADR-020 S10. The design
    // doc (§6.4) names it mw::dsp::OnePoleSmoother; the realized type (task 008) is
    // mw::params::OnePoleSmoother. This alias records the design-doc name and shares
    // the one existing declaration (no second flavor), matching DriftState.h.
    using Smoother = mw::params::OnePoleSmoother;

    // controlRateDivider: the [ADR-005]/[ADR-016] control-rate tick divider; the fade
    // advances one tick per tickControl() call at fc = sampleRate / controlRateDivider
    // (docs/design/03 §6.2). prepare() precomputes the fade coefficient from
    // kVcaAntiThumpMs (§4.6). Off the audio thread; idempotent.
    void prepare (double sampleRate, int controlRateDivider) noexcept;

    // Clear to a known start: gate closed, faded control at the nulled floor.
    void reset() noexcept;

    // ENV vs GATE amplitude source (§4.4). A switch only changes the TARGET; the fade
    // carries the current level toward it, so the flip is click-safe (§6.5). noexcept,
    // no allocation — safe to call between ticks.
    void setMode (VcaMode) noexcept;

    // Gate edges (note-on / note-off). gateOn opens (target follows the mode);
    // gateOff closes (target -> nulled floor). The fade ramps across the edge (§4.6).
    void gateOn() noexcept;
    void gateOff() noexcept;

    // Hot path: advance one control-rate tick and return the click-safe control to feed
    // Vca::process. envControl is the ADSR-shaped control level [0,1] used in ENV mode;
    // GATE mode ignores it and targets a flat full level (§4.4). Returns [0,1].
    float tickControl (float envControl) noexcept;

    // Block helper: write n click-safe control values for the n ENV-shaped inputs,
    // identical to n successive tickControl() calls (verified against the per-tick
    // reference). Hot path.
    void  processControlBlock (float* out, const float* envControl, int n) noexcept;

    // Queryable state (non-hot; for tests / callers wiring the control into Vca).
    [[nodiscard]] VcaMode mode()    const noexcept { return mode_; }
    [[nodiscard]] bool    gateOpen() const noexcept { return gateOpen_; }
    [[nodiscard]] float   control() const noexcept { return static_cast<float> (fade_.current()); }

private:
    // Recompute the smoother's target from mode + gate state + the current env input.
    [[nodiscard]] double targetFor (float envControl) const noexcept;

    Smoother fade_{};                 // shared one-pole gate fade (ADR-020 S10)
    VcaMode  mode_     = VcaMode::Env; // ENV follows ADSR; GATE holds flat full (§4.4)
    bool     gateOpen_ = false;        // current gate state (note on/off)
};

} // namespace mw101::dsp
