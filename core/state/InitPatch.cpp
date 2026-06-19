// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/state/InitPatch.cpp — builds the INIT patch overlay (task 021).
// Realizes docs/design/06 §11 (the ADR-016 out-of-box pole table) over the
// ParamDefs defaults, reading every (PI) overlay magnitude from the calibration
// table by name [docs/design/06 §3.10, §11; ADR-016].

#include "InitPatch.h"

namespace mw::state {

namespace {

// Set the overlay value for one ID in an already-default-seeded patch. Asserts the
// ID is live by virtue of the seed pass having created it; if an overlay names an ID
// not in kParamDefs that is a contract bug, so we no-op rather than allocate a row
// the §5.1 tree would not have.
void overlay(InitPatch& patch, std::string_view id, float value) {
    for (auto& p : patch.params) {
        if (p.id == id) {
            p.value = value;
            return;
        }
    }
}

void overlayChoice(InitPatch& patch, std::string_view id, int index) {
    overlay(patch, id, static_cast<float>(index));
}

void overlayBool(InitPatch& patch, std::string_view id, bool on) {
    overlay(patch, id, on ? 1.0f : 0.0f);
}

} // namespace

InitPatch buildInitPatch() {
    InitPatch patch;

    // --- 1. Seed from kParamDefs defaults. The param defaultValues are READ here and
    // never written, so this builder does not mutate the registry [docs/design/06 §11].
    patch.params.reserve(mw::params::kParamDefs.size());
    for (const auto& def : mw::params::kParamDefs) {
        patch.params.push_back(PatchValue{ std::string_view{ def.id }, def.defaultValue });
    }

    // --- 2. Apply the ADR-016 §11 overlay table. Every (PI) magnitude is read from
    // the calibration table by name; no INIT value is inlined here [§3.10; §11].

    // Control rate / pitch quant -> MODERN-SMOOTH [§11; ADR-016 R-1].
    overlayChoice(patch, "mw101.control.vintage", initpole::kControlModern);

    // Velocity ON, depth low-mid (PI) [§11; ADR-016 R-2].
    overlayBool(patch, "mw101.vel.enable", true);
    overlay(patch, "mw101.vel.depth", mw::cal::initpatch::kVelDepthLowMid);

    // Voice mode MONO [§11; ADR-016 R-3].
    overlayChoice(patch, "mw101.voice.mode", initpole::kVoiceModeMono);

    // Analog drift subtle ON, Age LOW (PI) [§11; ADR-016 R-4]. The param default of
    // vintage.age stays 0 in kParamDefs; the patch moves it low + enables drift.
    overlayBool(patch, "mw101.vintage.enable", true);
    overlay(patch, "mw101.vintage.age", mw::cal::initpatch::kVintageAgeLow);

    // FX engine OFF: master bypass + every per-engine enable false + chorus Off
    // [§11; ADR-010 FX-13; ADR-016 §Accepted]. fx.bypass param default is already
    // true, so this inherits FX-off directly and reaffirms it explicitly.
    overlayBool(patch, "mw101.fx.bypass", true);
    overlayBool(patch, "mw101.fx.drive_enable", false);
    overlayBool(patch, "mw101.fx.chorus_enable", false);
    overlayBool(patch, "mw101.fx.delay_enable", false);
    overlayChoice(patch, "mw101.fx.chorus_mode", initpole::kChorusModeOff);

    // Tuning A4 = 440 Hz [§11; ADR-012 C21-C22].
    overlay(patch, "mw101.tune.a4", mw::cal::initpatch::kTuneA4Hz);

    // MPE OFF [§11; ADR-012 C10].
    overlayBool(patch, "mw101.mpe.enable", false);

    // Modern un-quantized pitch OFF [§11; ADR-012 C7].
    overlayBool(patch, "mw101.pitch.modern_unquantized", false);

    // --- 3. renderVersion CURRENT; empty <extras> sequence [§11; §9.2; ADR-023 V9].
    patch.renderVersion = mw101::version::kCurrentRenderVersion;
    patch.schemaVersion = mw101::version::kCurrentSchemaVersion;
    patch.extras = Extras{};  // stepCount == 0 => empty sequence

    return patch;
}

} // namespace mw::state
