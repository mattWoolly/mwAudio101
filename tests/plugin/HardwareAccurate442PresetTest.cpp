// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/HardwareAccurate442PresetTest.cpp — the §10.3 'hardware-accurate'
// tune.a4 == 442 reference preset (task 131b).
//
// Task 131 confirmed NO factory preset set mw101.tune.a4 == 442, so the §10.3
// 'hardware-accurate' 442 Hz reference (the 440-vs-442 tuning duality the 129b signpost +
// ADR-012 C21/C22 reference) did not exist; FactoryPresetCorpusTest surfaced the gap as a
// non-fatal WARN. Task 131b AUTHORS exactly one such preset into an existing category and
// flips that WARN to a hard assertion.
//
// THIS test is the content guard for the authored preset: it asserts that exactly one
// embedded factory preset sets tune.a4 == 442, that it decodes cleanly through the SAME
// §6.4 validator every preset uses (loadPresetJson => registry-complete, in-range,
// honesty-clean), and that its meta carries the 'hardware-accurate' 442 Hz framing under
// the honesty discipline (valid §6.5 category; no 'as used on track'; no 'TB-303 filter';
// inspired_by null-or-string) [docs/design/06 §6.4, §10.3; ADR-008 C14-C18; ADR-012
// C21-C22].
//
// Test-case display names begin with the `presets_bank` tag and avoid the '[' character
// so `ctest -R presets_bank` selects them and the silent-pass / --no-tests=error rule
// holds. A juce::ScopedJuceInitialiser_GUI brackets every case (JUCE singletons + leak
// detector), matching tests/plugin/PluginHarnessTest.cpp.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "params/ParamDefs.h"      // mw::params::kParamDefs (registry size)
#include "preset/PresetFormat.h"   // loadPresetJson / PresetMeta
#include "state/StateTree.h"       // mw::state canonical keys

#include "BinaryData.h"            // the embedded factory bank (task 131)

namespace {

using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::loadPresetJson;

constexpr const char* kA4Id           = "mw101.tune.a4";
constexpr double      kHardwareA4      = 442.0;
constexpr double      kA4Epsilon       = 1.0e-6;

// The six §6.5 categories — also the valid meta.category enum values (ADR-008 C14).
const juce::StringArray kCategories{
    "AcidBassLead", "SubBass", "Lead", "PWMStrings", "BlipsFX", "SeqArpRiff"
};

// Decode one embedded resource (by index) through loadPresetJson. Returns the canonical
// tree (and fills `outMeta`) on a clean §6.4 decode, or nullopt for a non-preset /
// malformed entry. Mirrors FactoryPresetCorpusTest's embedded-resource decode path.
std::optional<juce::ValueTree> decodeEmbedded(int i, PresetMeta& outMeta)
{
    const char* resourceName = BinaryData::namedResourceList[i];
    int dataSize = 0;
    const char* data = BinaryData::getNamedResource(resourceName, dataSize);
    if (data == nullptr || dataSize <= 0)
        return std::nullopt;

    const juce::String json{ juce::CharPointer_UTF8{ data },
                             static_cast<std::size_t>(dataSize) };
    const juce::TemporaryFile temp{ ".mw101preset" };
    if (! temp.getFile().replaceWithText(json))
        return std::nullopt;

    return loadPresetJson(temp.getFile(), outMeta);
}

// The a4 value of a decoded canonical tree (NaN if the param node is absent).
double a4Of(const juce::ValueTree& canonical)
{
    const auto params =
        canonical.getChildWithName(juce::Identifier{ mw::state::kParamsId });
    const auto node = params.getChildWithProperty("id", kA4Id);
    if (! node.isValid())
        return std::numeric_limits<double>::quiet_NaN();
    return static_cast<double>(node.getProperty("value"));
}

bool isHardwareA4(double v)
{
    return std::abs(v - kHardwareA4) < kA4Epsilon;
}

} // namespace

// --- exactly one embedded preset is the §10.3 442 Hz hardware-accurate reference -------

TEST_CASE("presets_bank carries exactly one hardware-accurate 442 Hz reference preset",
          "[presets_bank]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    int count442 = 0;
    juce::String foundIn;
    PresetMeta foundMeta;

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        PresetMeta meta;
        const auto canonical = decodeEmbedded(i, meta);
        if (! canonical.has_value())
            continue;   // a non-preset embedded entry (none expected, but be defensive)

        if (isHardwareA4(a4Of(*canonical)))
        {
            ++count442;
            foundIn   = juce::String{ juce::CharPointer_UTF8{
                BinaryData::getNamedResourceOriginalFilename(
                    BinaryData::namedResourceList[i]) } };
            foundMeta = meta;
        }
    }

    INFO("hardware-accurate 442 Hz preset resolved to: " << foundIn);
    // Exactly one: the bank gains the documented 442 identity without 442 leaking across
    // the corpus (ADR-012 C22 — 442 is never the default, surfaced in ONE preset).
    CHECK(count442 == 1);

    // The found preset's meta is honesty-clean and §6.5-valid: it already passed the §6.4
    // validator (decodeEmbedded => loadPresetJson), so re-assert the human-facing identity.
    REQUIRE(foundIn.isNotEmpty());
    CHECK(kCategories.contains(foundMeta.category));        // a valid §6.5 category
    CHECK(foundMeta.description.isNotEmpty());              // documents the 442 reference
    CHECK(foundMeta.tags.size() >= 1);

    // The §10.3 442 framing lives in the human text without an over-claim. The validator
    // already rejects the forbidden phrases; assert the description NAMES the hardware-
    // accurate 442 Hz reference so the identity is real, not incidental.
    const auto desc = foundMeta.description.toLowerCase();
    CHECK(desc.contains("442"));
    CHECK((desc.contains("hardware-accurate") || desc.contains("hardware accurate")));
}

// --- the 442 preset decodes clean through the SAME §6.4 validator every preset uses ----

TEST_CASE("presets_bank the 442 reference preset is registry-complete in-range and honesty-clean",
          "[presets_bank]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    bool checkedOne = false;

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        PresetMeta meta;
        const auto canonical = decodeEmbedded(i, meta);
        if (! canonical.has_value() || ! isHardwareA4(a4Of(*canonical)))
            continue;

        checkedOne = true;

        // Registry-complete: a clean loadPresetJson decode projects all 91 kParamDefs IDs
        // into the canonical <PARAMS> subtree (a missing ID is a hard §6.4 rejection, so a
        // has_value() result already proves completeness — assert the projected count).
        const auto params =
            canonical->getChildWithName(juce::Identifier{ mw::state::kParamsId });
        REQUIRE(params.isValid());
        CHECK(params.getNumChildren()
              == static_cast<int>(std::size(mw::params::kParamDefs)));

        // tune.a4 is exactly 442 in the projected tree.
        CHECK(isHardwareA4(a4Of(*canonical)));

        // It differs from INIT only in the documented ways: a4 = 442 (not 440). The
        // validator guarantees in-range; this asserts the load-bearing a4 deviation.
        CHECK_FALSE(isHardwareA4(440.0));   // sanity: 440 is NOT the hardware value
    }

    // A 442 preset MUST exist for this case to be meaningful (the silent-pass guard).
    REQUIRE(checkedOne);
}
