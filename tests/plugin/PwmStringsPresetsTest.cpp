// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PwmStringsPresetsTest.cpp — JUCE-linked validation of the authored
// PWMStrings preset category (task 148). Enumerates presets/PWMStrings/*.mw101preset
// on disk and, for EVERY file, asserts it loads through the real §6.4 loader/validator
// (mw::plugin::preset::loadPresetJson) — i.e. every one of the 91 live registry IDs is
// present, every value is in range, every choice index is valid, the category matches
// the folder (PWMStrings), sound_ext is consistent with the params, and no forbidden
// attribution/descriptor ships. It also checks the §4.5 / §7.1(4) recipe (triangle-LFO
// pulse-width sweep over square+sub with chorus baked) and the §4.5 honest label that
// every strings/pad preset must declare itself a MONO PWM stylization (one VCO; true
// polyphonic pads are impossible).
//
// WHY plugin/ AND NOT a default-preset test: the loader projects the CANONICAL JUCE
// ValueTree that StateSerializer (task 023) owns, so the validation seam links JUCE and
// cannot build in JUCE-free mwcore or under the `default` preset. The task file's
// component:docs / `cmake --preset default` Verification block is STALE for the same
// reason 023/025/144 moved plugin-side; behavior is exactly the task's Scope/Acceptance,
// only the build location/commands are corrected. Build with MW_BUILD_PLUGIN=ON; run
// `ctest -R presets_pwmstrings --no-tests=error`.
//
// Folder location: the on-disk preset bank is authored data (not BinaryData-embedded),
// so the test must find presets/PWMStrings/ at runtime. There is no compile-time
// project-root define (and tests/CMakeLists.txt is shared / not edited by this task), so
// the folder is located by walking up from __FILE__ (this TU lives at
// <repo>/tests/plugin/…) with a CWD-walk fallback. Test display names begin with the
// `presets_pwmstrings` tag and avoid the '[' character so the selector matches exactly
// these cases.

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

// Locate <repo>/presets/PWMStrings. Primary strategy: __FILE__ is the absolute path of
// this TU (<repo>/tests/plugin/PwmStringsPresetsTest.cpp), so its grandparent's
// grandparent is <repo>. Fallback: walk up from the current working directory looking
// for a presets/PWMStrings child (covers ctest invoked from the build tree).
juce::File findPwmStringsDir()
{
    // __FILE__-relative: .../tests/plugin/PwmStringsPresetsTest.cpp ->
    // up 1 = tests/plugin, up 2 = tests, up 3 = <repo>.
    const juce::File thisFile{ juce::String::fromUTF8(__FILE__) };
    if (thisFile.existsAsFile())
    {
        const auto repo = thisFile.getParentDirectory()   // tests/plugin
                                  .getParentDirectory()    // tests
                                  .getParentDirectory();   // <repo>
        const auto dir = repo.getChildFile("presets").getChildFile("PWMStrings");
        if (dir.isDirectory())
            return dir;
    }

    // CWD-walk fallback.
    auto cur = juce::File::getCurrentWorkingDirectory();
    for (int hops = 0; hops < 12 && cur.exists(); ++hops)
    {
        const auto dir = cur.getChildFile("presets").getChildFile("PWMStrings");
        if (dir.isDirectory())
            return dir;
        const auto parent = cur.getParentDirectory();
        if (parent == cur)
            break;
        cur = parent;
    }
    return {};
}

// All *.mw101preset files in the PWMStrings folder.
juce::Array<juce::File> pwmStringsFiles()
{
    juce::Array<juce::File> out;
    const auto dir = findPwmStringsDir();
    if (dir.isDirectory())
        dir.findChildFiles(out, juce::File::findFiles, false, "*.mw101preset");
    return out;
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

int choiceIndex(const std::map<std::string, double>& m, const char* id)
{
    const auto it = m.find(id);
    return it == m.end() ? -1 : static_cast<int>(std::lround(it->second));
}

} // namespace

TEST_CASE("presets_pwmstrings the folder holds the authored category bank", "[presets_pwmstrings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto dir = findPwmStringsDir();
    REQUIRE(dir.isDirectory());                       // the folder exists and was found

    const auto files = pwmStringsFiles();
    // The category is ~8 presets; require a healthy bank so a deleted/renamed file is
    // caught (non-vacuous: an empty enumeration must NOT silently pass).
    REQUIRE(files.size() >= 8);
}

TEST_CASE("presets_pwmstrings every file loads through the real validator with all 91 IDs in range",
          "[presets_pwmstrings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = pwmStringsFiles();
    REQUIRE(files.size() >= 8);

    for (const auto& file : files)
    {
        INFO("preset file: " << file.getFileName().toStdString());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);

        // loadPresetJson runs the FULL §6.4 validator: schemaVersion + required meta
        // present, category in the §6.5 enum, EVERY registry ID present + in range +
        // valid choice index, sound_ext == used-software-ext, no per-step accent, no
        // forbidden attribution. A nullopt means this file failed one of those rules.
        REQUIRE(canonical.has_value());

        // Registry-complete: one recovered <PARAM> per live ID, none missing.
        const auto values = recoveredParamValues(*canonical);
        REQUIRE(values.size() == mw::params::kParamDefs.size());
        for (const auto& def : mw::params::kParamDefs)
            REQUIRE(values.find(def.id) != values.end());

        // Category MUST match the folder it lives in.
        CHECK(meta.category == "PWMStrings");
    }
}

TEST_CASE("presets_pwmstrings every file bakes the triangle-LFO PWM sweep over square plus sub with chorus",
          "[presets_pwmstrings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = pwmStringsFiles();
    REQUIRE(files.size() >= 8);

    for (const auto& file : files)
    {
        INFO("preset file: " << file.getFileName().toStdString());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());
        const auto v = recoveredParamValues(*canonical);

        // §4.5 / §7.1(4) recipe: a triangle LFO sweeping the pulse width.
        CHECK(choiceIndex(v, "mw101.lfo.shape") == 0);   // Tri (hardware shape)
        CHECK(choiceIndex(v, "mw101.lfo.dest") == 2);    // -> PWM
        CHECK(v.at("mw101.lfo.depth_pwm") > 0.0);        // the sweep is actually wired

        // …over a square+sub voice (the pulse leads, the sub adds body).
        CHECK(v.at("mw101.pulse.level") > 0.0);
        CHECK(v.at("mw101.sub.level") > 0.0);

        // …with Chorus FX baked in (ADR-016: FX bakeable here): master FX engaged and
        // the chorus enabled with audible wet mix.
        CHECK(choiceIndex(v, "mw101.fx.bypass") == 0);        // FX engine ON
        CHECK(choiceIndex(v, "mw101.fx.chorus_enable") == 1); // chorus engaged
        CHECK(v.at("mw101.fx.chorus_mix") > 0.0);             // audibly wet
    }
}

TEST_CASE("presets_pwmstrings every description carries the mono PWM stylization honest label",
          "[presets_pwmstrings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = pwmStringsFiles();
    REQUIRE(files.size() >= 8);

    for (const auto& file : files)
    {
        INFO("preset file: " << file.getFileName().toStdString());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        // §4.5 honest label: each strings/pad preset MUST state it is a mono PWM
        // stylization (one VCO; true polyphonic pads are impossible). We require both
        // the "mono" framing and the explicit "one VCO" / "stylization" honesty, so a
        // bare "mono" tag cannot satisfy the gate by accident.
        const auto desc = meta.description.toLowerCase();
        CHECK(desc.contains("mono"));
        CHECK(desc.contains("stylization"));
        CHECK(desc.contains("one vco"));
    }
}

TEST_CASE("presets_pwmstrings meta is honest, in-enum, and sound_ext-consistent",
          "[presets_pwmstrings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = pwmStringsFiles();
    REQUIRE(files.size() >= 8);

    static const std::array<const char*, 6> kCategories{
        "AcidBassLead", "SubBass", "Lead", "PWMStrings", "BlipsFX", "SeqArpRiff"
    };

    for (const auto& file : files)
    {
        INFO("preset file: " << file.getFileName().toStdString());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        // Required meta fields (the validator already requires non-empty name/author).
        CHECK(meta.name.isNotEmpty());
        CHECK(meta.author.isNotEmpty());

        // Category is in the §6.5 enum AND is exactly the folder's category.
        bool known = false;
        for (const char* c : kCategories)
            known = known || (meta.category == c);
        CHECK(known);
        CHECK(meta.category == "PWMStrings");

        // sound_ext consistency (the loader rejects a mismatch, so this re-checks the
        // contract both ways): these patches stay on hardware registers + Tri LFO, so
        // none uses a software-only feature -> sound_ext == false. Belt-and-suspenders
        // against the registry: vco.range < 4 and lfo.shape != Sine(4).
        const auto v = recoveredParamValues(*canonical);
        const bool usesSwExt =
            choiceIndex(v, "mw101.vco.range") >= 4 || choiceIndex(v, "mw101.lfo.shape") == 4;
        CHECK(meta.soundExt == usesSwExt);
        CHECK(meta.soundExt == false);
        CHECK(choiceIndex(v, "mw101.vco.range") < 4);
        CHECK(choiceIndex(v, "mw101.lfo.shape") != 4);

        // Attribution discipline (the validator also enforces these; assert at the meta
        // surface): no track-reconstruction claim, no "TB-303 filter" descriptor in any
        // human-facing text. inspired_by is JSON null (empty here) or an inspired-by
        // string — never a track claim.
        const auto haystack =
            (meta.name + " " + meta.description + " " + meta.inspiredBy + " "
             + meta.tags.joinIntoString(" ")).toLowerCase();
        CHECK(! haystack.contains("as used on track"));
        CHECK(! haystack.contains("tb-303 filter"));
    }
}

// Non-vacuity guard: prove the validator actually REJECTS a broken PWMStrings preset, so
// the "every file loads" assertions above are meaningful (a no-op loader would pass them
// vacuously). We mutate a real authored file's JSON to drop a required registry ID and a
// second variant to assert a wrong-category descriptor / forbidden phrase is rejected.
TEST_CASE("presets_pwmstrings the validator rejects a broken preset, proving the suite is non-vacuous",
          "[presets_pwmstrings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = pwmStringsFiles();
    REQUIRE(files.size() >= 8);

    // Start from a real authored file so the baseline is known-good.
    const auto good = files.getReference(0).loadFileAsString();
    REQUIRE(good.isNotEmpty());

    // Sanity: the unmodified authored text loads.
    {
        auto tmp = juce::File::createTempFile(".mw101preset");
        tmp.replaceWithText(good);
        PresetMeta meta;
        const auto ok = loadPresetJson(tmp, meta);
        tmp.deleteFile();
        REQUIRE(ok.has_value());
    }

    // (a) Drop a required registry ID -> the §6.4 "every ID present" rule rejects it.
    {
        auto broken = good.replace("\"mw101.vco.tune\"", "\"mw101.vco.tune_REMOVED\"");
        REQUIRE(broken != good);                       // the mutation took effect
        auto tmp = juce::File::createTempFile(".mw101preset");
        tmp.replaceWithText(broken);
        PresetMeta meta;
        const auto res = loadPresetJson(tmp, meta);
        tmp.deleteFile();
        CHECK_FALSE(res.has_value());
    }

    // (b) Inject a forbidden "TB-303 filter" descriptor into the description -> the
    //     attribution-discipline rule rejects it.
    {
        auto broken = good.replace("Mono PWM stylization", "TB-303 filter Mono PWM stylization");
        REQUIRE(broken != good);
        auto tmp = juce::File::createTempFile(".mw101preset");
        tmp.replaceWithText(broken);
        PresetMeta meta;
        const auto res = loadPresetJson(tmp, meta);
        tmp.deleteFile();
        CHECK_FALSE(res.has_value());
    }
}
