// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetBaselineTest.cpp — JUCE-linked validation of the factory
// INIT/baseline preset (task 144). Asserts presets/INIT.mw101preset:
//
//   1. loads through the real §6.4 loader/validator (loadPresetJson succeeds) — i.e.
//      every one of the 91 live registry IDs is present, every value is in range, and
//      every choice index is valid [ADR-008 C13/C18; docs/design/06 §6.4].
//   2. selects the ADR-016 R-1..R-4 out-of-box poles + the accepted-without-veto FX
//      default: MODERN-SMOOTH control, velocity ON (-> VCA/VCF), MONO voice mode,
//      subtle drift (Age low), FX engine OFF [ADR-016 R-1..R-4, §Accepted;
//      docs/design/06 §11].
//   3. carries a valid §6.5 category and sound_ext == false (uses no software-only
//      feature: vco.range < 4, lfo.shape != Sine) [ADR-008 C14/C15; docs/design/06 §6.5].
//
// The INIT JSON is EMBEDDED below as a raw-string literal that is byte-identical to
// presets/INIT.mw101preset (the file is authored data, not BinaryData-embedded yet),
// then written to a temp file and run through the real loadPresetJson — the same seam
// the PresetManager uses at load time [task brief: embed-or-read is fine; do NOT rely
// on BinaryData]. Test display names begin with the `presets_baseline` tag and avoid
// the '[' character so `ctest -R presets_baseline --no-tests=error` selects exactly
// these.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <map>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "preset/PresetFormat.h"   // mw::plugin::preset::loadPresetJson / PresetMeta
#include "params/ParamDefs.h"      // mw::params::kParamDefs (JUCE-free registry)
#include "state/StateTree.h"       // mw::state canonical keys

namespace {

using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::loadPresetJson;

// The factory INIT/baseline preset — byte-identical to presets/INIT.mw101preset.
const char* const kInitPresetJson = R"mwinit({
  "schemaVersion": 1,
  "meta": {
    "name": "INIT",
    "author": "Matt Woolly",
    "category": "Lead",
    "description": "Factory baseline. Modern-smooth control, velocity to VCA and VCF, monophonic, a touch of analog drift. The starting point every category preset is authored from.",
    "tags": [
      "init",
      "baseline",
      "factory"
    ],
    "inspired_by": null,
    "sound_ext": false
  },
  "params": {
    "mw101.vco.tune": 0.0,
    "mw101.vco.fine": 0.0,
    "mw101.vco.pw": 0.5,
    "mw101.vco.pwm_depth": 0.0,
    "mw101.vco.range": 1,
    "mw101.saw.level": 0.8,
    "mw101.pulse.level": 0.0,
    "mw101.sub.level": 0.0,
    "mw101.sub.mode": 0,
    "mw101.noise.level": 0.0,
    "mw101.vcf.cutoff": 1.0,
    "mw101.vcf.resonance": 0.0,
    "mw101.vcf.env_mod": 0.0,
    "mw101.vcf.lfo_mod": 0.0,
    "mw101.vcf.kbd_track": 0.0,
    "mw101.env.attack": 0.0,
    "mw101.env.decay": 0.3,
    "mw101.env.sustain": 1.0,
    "mw101.env.release": 0.1,
    "mw101.lfo.rate": 5.0,
    "mw101.lfo.shape": 0,
    "mw101.lfo.dest": 0,
    "mw101.lfo.delay": 0.0,
    "mw101.lfo.depth_pitch": 0.0,
    "mw101.lfo.depth_pwm": 0.0,
    "mw101.lfo.depth_cutoff": 0.0,
    "mw101.lfo.tempo_sync": 0,
    "mw101.lfo.sync_div": 1,
    "mw101.vca.level": 0.8,
    "mw101.vca.mode": 0,
    "mw101.glide.time": 0.0,
    "mw101.glide.mode": 0,
    "mw101.mod.bend_range_vco": 200.0,
    "mw101.mod.bend_range_vcf": 0.0,
    "mw101.mod.bend_dest": 0,
    "mw101.mod.lfo_mod_wheel": 0.0,
    "mw101.arp.mode": 0,
    "mw101.arp.range": 0,
    "mw101.arp.tempo_sync": 1,
    "mw101.arp.sync_div": 1,
    "mw101.arp.latch": 0,
    "mw101.seq.mode": 0,
    "mw101.seq.tempo_sync": 1,
    "mw101.seq.sync_div": 3,
    "mw101.key.trigger_priority": 0,
    "mw101.tune.a4": 440.0,
    "mw101.tune.slop": 2.5,
    "mw101.pitch.modern_unquantized": 0,
    "mw101.vel.enable": 1,
    "mw101.vel.depth": 0.5,
    "mw101.amp.expression": 1.0,
    "mw101.mpe.enable": 0,
    "mw101.mpe.bend_range": 48.0,
    "mw101.mpe.pressure_dest": 0,
    "mw101.vintage.age": 0.15,
    "mw101.vintage.enable": 1,
    "mw101.vintage.cal_spread": 0.25,
    "mw101.vintage.detune_amt": 0.0,
    "mw101.drift.depth": 4.0,
    "mw101.drift.rate": 0.1,
    "mw101.warmup.time": 0.0,
    "mw101.var.cutoff": 0.0,
    "mw101.var.env_time": 0.0,
    "mw101.var.pw": 0.0,
    "mw101.var.glide": 0.0,
    "mw101.fx.bypass": 1,
    "mw101.fx.drive_enable": 0,
    "mw101.fx.drive_amount": 0.0,
    "mw101.fx.drive_tone": 0.5,
    "mw101.fx.drive_output": 0.5,
    "mw101.fx.chorus_enable": 0,
    "mw101.fx.chorus_mode": 0,
    "mw101.fx.chorus_rate": 0.3,
    "mw101.fx.chorus_depth": 0.5,
    "mw101.fx.chorus_width": 1.0,
    "mw101.fx.chorus_mix": 0.0,
    "mw101.fx.delay_enable": 0,
    "mw101.fx.delay_sync": 0,
    "mw101.fx.delay_division": 1,
    "mw101.fx.delay_time": 0.3,
    "mw101.fx.delay_feedback": 0.3,
    "mw101.fx.delay_damp": 0.5,
    "mw101.fx.delay_width": 1.0,
    "mw101.fx.delay_mix": 0.0,
    "mw101.fx.delay_pingpong": 0,
    "mw101.out.mono": 0,
    "mw101.quality": 1,
    "mw101.voice.mode": 0,
    "mw101.voice.count": 1,
    "mw101.unison.count": 0,
    "mw101.control.vintage": 0
  },
  "seq": {
    "stepCount": 0,
    "steps": []
  },
  "arp": {
    "latch": false
  }
}
)mwinit";

// Write the embedded INIT JSON to a temp .mw101preset and run it through the real
// loader/validator, mirroring tests/plugin/PresetFormatTest.cpp's loadFromText helper.
std::optional<juce::ValueTree> loadInit(PresetMeta& outMeta)
{
    auto file = juce::File::createTempFile(".mw101preset");
    file.replaceWithText(juce::String::fromUTF8(kInitPresetJson));
    auto result = loadPresetJson(file, outMeta);
    file.deleteFile();
    return result;
}

// Pull the recovered <PARAMS> subtree into an id -> modeled value map.
std::map<std::string, double> recoveredParamValues(const juce::ValueTree& canonical)
{
    std::map<std::string, double> out;
    const auto params = canonical.getChildWithName(juce::Identifier{ mw::state::kParamsId });
    for (int i = 0; i < params.getNumChildren(); ++i)
    {
        const auto child = params.getChild(i);
        out[child.getProperty("id").toString().toStdString()] =
            static_cast<double>(child.getProperty("value"));
    }
    return out;
}

// The integer index a choice/bool param recovered to.
int choiceIndex(const std::map<std::string, double>& m, const char* id)
{
    const auto it = m.find(id);
    return it == m.end() ? -1 : static_cast<int>(std::lround(it->second));
}

} // namespace

TEST_CASE("presets_baseline INIT loads through the validator with all 91 IDs in range",
          "[presets_baseline]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    PresetMeta meta;
    const auto canonical = loadInit(meta);

    // loadPresetJson runs the full §6.4 validator: schemaVersion present, meta required
    // fields present, category in the §6.5 enum, EVERY registry ID present + in range +
    // valid choice index, sound_ext == used-software-ext, no per-step accent, no
    // forbidden attribution. A nullopt here means INIT failed one of those rules.
    REQUIRE(canonical.has_value());

    // Registry-complete: one <PARAM> per live ID, none missing.
    const auto values = recoveredParamValues(*canonical);
    REQUIRE(values.size() == mw::params::kParamDefs.size());
    for (const auto& def : mw::params::kParamDefs)
        REQUIRE(values.find(def.id) != values.end());
}

TEST_CASE("presets_baseline INIT selects the ADR-016 R-1 to R-4 modern poles plus FX off",
          "[presets_baseline]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    PresetMeta meta;
    const auto canonical = loadInit(meta);
    REQUIRE(canonical.has_value());
    const auto v = recoveredParamValues(*canonical);

    // R-1 — Control = MODERN-SMOOTH: mw101.control.vintage == Modern (0).
    CHECK(choiceIndex(v, "mw101.control.vintage") == 0);
    // §11: the modern un-quantized pitch escape hatch stays OFF.
    CHECK(choiceIndex(v, "mw101.pitch.modern_unquantized") == 0);

    // R-2 — Velocity = ON (-> VCA + VCF amount).
    CHECK(choiceIndex(v, "mw101.vel.enable") == 1);
    // The velocity depth lands in a low-mid (PI) band, not zero (so dynamics are audible)
    // and not pinned to the top.
    {
        const auto it = v.find("mw101.vel.depth");
        REQUIRE(it != v.end());
        CHECK(it->second > 0.0);
        CHECK(it->second < 1.0);
    }

    // R-3 — Voice mode = MONO (index 0 in the Mono/Poly/Unison choice).
    CHECK(choiceIndex(v, "mw101.voice.mode") == 0);

    // R-4 — subtle drift ON, Age LOW: vintage.enable on, vintage.age low (in tune, alive).
    CHECK(choiceIndex(v, "mw101.vintage.enable") == 1);
    {
        const auto it = v.find("mw101.vintage.age");
        REQUIRE(it != v.end());
        CHECK(it->second > 0.0);    // not dead-clean
        CHECK(it->second <= 0.25);  // "low", not a heavy detune
    }

    // FX engine-default OFF (accepted-without-veto): master bypass on, no FX enabled,
    // chorus mode Off.
    CHECK(choiceIndex(v, "mw101.fx.bypass") == 1);          // true == bypassed
    CHECK(choiceIndex(v, "mw101.fx.drive_enable") == 0);
    CHECK(choiceIndex(v, "mw101.fx.chorus_enable") == 0);
    CHECK(choiceIndex(v, "mw101.fx.delay_enable") == 0);
    CHECK(choiceIndex(v, "mw101.fx.chorus_mode") == 0);     // Off

    // §11 affirmations: Quality = Standard (1), A4 = 440 Hz, MPE off.
    CHECK(choiceIndex(v, "mw101.quality") == 1);
    {
        const auto it = v.find("mw101.tune.a4");
        REQUIRE(it != v.end());
        CHECK(it->second == 440.0);
    }
    CHECK(choiceIndex(v, "mw101.mpe.enable") == 0);
}

TEST_CASE("presets_baseline INIT meta uses a valid category and sound_ext false",
          "[presets_baseline]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    PresetMeta meta;
    const auto canonical = loadInit(meta);
    REQUIRE(canonical.has_value());

    // meta.name / author populated (the validator already requires non-empty).
    CHECK(meta.name.isNotEmpty());
    CHECK(meta.author.isNotEmpty());

    // §6.5 category enum — one of exactly six values; INIT is a baseline (Lead).
    static const std::array<const char*, 6> kCategories{
        "AcidBassLead", "SubBass", "Lead", "PWMStrings", "BlipsFX", "SeqArpRiff"
    };
    bool known = false;
    for (const char* c : kCategories)
        known = known || (meta.category == c);
    CHECK(known);
    CHECK(meta.category == "Lead");

    // INIT uses no software-only feature, so sound_ext is false. (The loader derives
    // soundExt from the params and rejects a mismatch, so this also proves the on-disk
    // flag matched.)
    CHECK(meta.soundExt == false);

    // Baseline carries no track attribution: inspired_by is JSON null -> empty string.
    CHECK(meta.inspiredBy.isEmpty());

    // And, belt-and-suspenders against the registry: vco.range stays a hardware register
    // (< 4) and lfo.shape is not the software-only Sine (index 4).
    const auto v = recoveredParamValues(*canonical);
    CHECK(static_cast<int>(std::lround(v.at("mw101.vco.range"))) < 4);
    CHECK(static_cast<int>(std::lround(v.at("mw101.lfo.shape"))) != 4);
}
