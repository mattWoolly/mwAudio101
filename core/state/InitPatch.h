// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/state/InitPatch.h — the INIT patch (out-of-box defaults, ADR-016) as a
// JUCE-free POD canonical-tree description (task 021). Realizes docs/design/06 §11,
// §3.10, §8.2 and ADR-016.
//
// WHAT THIS IS. The INIT patch is the canonical fallback (§8) and the out-of-box
// state. It is a PATCH built from the ParamDefs defaults (core/params/ParamDefs.h)
// with the ADR-016 §11 pole selections applied as an overlay; the per-parameter
// `defaultValue` in §3 is NOT changed by ADR-016 (this builder does not mutate
// kParamDefs), and the <extras> sequence is empty by default [docs/design/06 §11;
// ADR-021 L1; ADR-016 Contract].
//
// WHY POD, NOT juce::ValueTree. mwcore is JUCE-free (ADR-001 C1; the build's
// no-JUCE-in-core guard FAILS on any JUCE reference under core/). The §11 canonical
// tree is realized in two layers, exactly like core/state/StateTree.h: this header
// owns the JUCE-free DATA of the patch (the param id -> value overlay map, the
// renderVersion, and the empty Extras payload); the thin plugin-stream bridge that
// projects this POD onto a live juce::ValueTree / APVTS state is a separate task
// (params-10 fallback wiring; the plugin state stream), out of scope here
// [docs/design/00 §3.3; ADR-001 C1; docs/design/06 §11 OUT-OF-SCOPE].
//
// All construction is message-thread / setup-time work; nothing here runs on the
// audio thread [docs/design/06 §12].

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "../calibration/InitPatchConstants.h"
#include "../params/ParamDefs.h"
#include "../version/EngineVersion.h"
#include "Extras.h"

namespace mw::state {

// ---------------------------------------------------------------------------
// §11 overlay pole indices (the canonical choice/bool indices the INIT patch
// selects). Mirrors the §3.4 / §3.5 / §3.7 label-list ordering in ParamDefs.h so a
// test can assert the patch lands on the right enum slot, not just "non-default".
// ---------------------------------------------------------------------------
namespace initpole {
    // mw101.control.vintage = Modern (kBoolModernVint index 0; "Vintage" is 1).
    inline constexpr int kControlModern = 0;        // [docs/design/06 §11; ADR-016 R-1]
    // mw101.voice.mode = Mono (kVoiceMode index 0).
    inline constexpr int kVoiceModeMono = 0;        // [docs/design/06 §11; ADR-016 R-3]
    // mw101.fx.chorus_mode = Off (kChorusMode index 0).
    inline constexpr int kChorusModeOff = 0;        // [docs/design/06 §11; ADR-010 FX-13]
} // namespace initpole

// One resolved parameter value in the INIT patch: the canonical param ID and its
// value in MODELED units (continuous) or choice/bool index encoded as a float (the
// same encoding ParamDef::defaultValue uses, §3.1). This is the exact per-parameter
// payload a juce bridge would store into <PARAMS>.
struct PatchValue {
    std::string_view id;     // canonical "mw101.*" ID (points into kParamDefs)
    float            value;  // modeled value, or static_cast<float>(choiceIndex)
};

// The full JUCE-free description of the INIT canonical tree (§5.1 shape, minus the
// juce container): one PatchValue per live parameter, the render/schema metadata,
// and the (empty) <extras> payload [docs/design/06 §5.1, §11].
struct InitPatch {
    std::vector<PatchValue> params;   // exactly kParamDefs.size() entries, in table order
    int    renderVersion = mw101::version::kCurrentRenderVersion;  // §11; §9.2; ADR-023 V9
    int    schemaVersion = mw101::version::kCurrentSchemaVersion;  // §5.1
    Extras extras{};                  // empty <extras> sequence by default [§11; §5.4]

    // Look up a resolved INIT value by ID (linear scan; setup-time only). Returns
    // nullopt if the ID is not a live parameter.
    [[nodiscard]] std::optional<float> valueFor(std::string_view id) const {
        for (const auto& p : params) {
            if (p.id == id) return p.value;
        }
        return std::nullopt;
    }
};

// Build the INIT patch: start from every kParamDefs default, then apply the §11
// overlay table. Does NOT mutate kParamDefs (the param defaults are read, never
// written). Message-thread / setup-time only [docs/design/06 §11].
[[nodiscard]] InitPatch buildInitPatch();

} // namespace mw::state
