// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/ParameterLayoutTest.cpp — JUCE-linked tests for buildParameterLayout()
// (task 020). Asserts every acceptance criterion of plan/backlog/020:
//
//   1. The layout emits EXACTLY one live parameter per kParamDefs entry, in order,
//      with no parameter constructed elsewhere — the layout is a PURE function of the
//      registry (count == kParamDefs.size(); ids match the table 1:1) [§4].
//   2. Each parameter carries juce::ParameterID{id, versionAdded}, i.e. its string ID
//      and version hint match the registry, so the VST3/AU/CLAP numeric ID (a pure
//      hash of id + version hint) is deterministic [§3.2; §4; ADR-008 C3].
//   3. Structural params (def.isAutomatable == false) are non-automatable; every
//      other param is automatable [§3.8; ADR-008 C7].
//   4. Two independent builds of the layout yield identical (id, versionHint,
//      automatable) tuples in identical order — deterministic across builds [§3.2].
//
// Introspection path: a ParameterLayout's parameters are private, so we attach the
// layout to an APVTS on a throwaway juce::AudioProcessor and read back the registered
// parameters via AudioProcessor::getParameters(). Each is a RangedAudioParameter
// (-> AudioProcessorParameterWithID) so getParameterID()/isAutomatable() are exact,
// and the base getVersionHint() returns the ParameterID version hint verbatim.

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "params/ParameterLayout.h"   // mw::plugin::buildParameterLayout
#include "params/ParamDefs.h"         // mw::params::kParamDefs (JUCE-free registry)

namespace {

// A minimal headless AudioProcessor whose only job is to host an APVTS built from a
// buildParameterLayout() layout, so the constructed parameters become introspectable
// through the standard AudioProcessor::getParameters() surface.
class LayoutHostProcessor final : public juce::AudioProcessor
{
public:
    LayoutHostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    // --- the no-op AudioProcessor surface the base class requires --------------
    const juce::String getName() const override        { return "ParamLayoutHost"; }
    void prepareToPlay(double, int) override            {}
    void releaseResources() override                    {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override        { return 0.0; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override                     { return false; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram(int) override                {}
    const juce::String getProgramName(int) override     { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

// (id, versionHint, automatable) per parameter, in registration order — the tuple
// that fully determines the deterministic numeric ID and automation visibility.
struct ParamFacts {
    std::string id;
    int         versionHint{};
    bool        automatable{};
};

std::vector<ParamFacts> collectFacts(const LayoutHostProcessor& host)
{
    std::vector<ParamFacts> facts;
    for (auto* p : host.getParameters())
    {
        auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(p);
        REQUIRE(withId != nullptr);   // every emitted param is a RangedAudioParameter
        facts.push_back({ withId->getParameterID().toStdString(),
                          p->getVersionHint(),
                          p->isAutomatable() });
    }
    return facts;
}

} // namespace

TEST_CASE("paramlayout emits exactly one parameter per kParamDefs entry, in order",
          "[paramlayout]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LayoutHostProcessor host;

    const auto facts = collectFacts(host);

    // Purity-of-kParamDefs: one live parameter per registry entry, same count.
    REQUIRE(facts.size() == mw::params::kParamDefs.size());

    // ...and in the SAME order, with IDs matching the table 1:1 (no param minted
    // elsewhere, none dropped, none reordered).
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
        CHECK(facts[i].id == std::string{ mw::params::kParamDefs[i].id });
}

TEST_CASE("paramlayout carries the registry version hint so numeric IDs are deterministic",
          "[paramlayout]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LayoutHostProcessor host;

    const auto facts = collectFacts(host);
    REQUIRE(facts.size() == mw::params::kParamDefs.size());

    // Each parameter was constructed juce::ParameterID{def.id, def.versionAdded}; the
    // string ID + version hint are the SOLE inputs to JUCE's VST3/AU/CLAP numeric-ID
    // hash, so matching both proves the numeric ID is the deterministic hash of the
    // string ID [§3.2; ADR-008 C3].
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
    {
        const auto& def = mw::params::kParamDefs[i];
        CHECK(facts[i].id == std::string{ def.id });
        CHECK(facts[i].versionHint == static_cast<int>(def.versionAdded));
        CHECK(facts[i].versionHint != 0);   // a 0 hint defeats the deterministic hash
    }
}

TEST_CASE("paramlayout marks structural params non-automatable and the rest automatable",
          "[paramlayout]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LayoutHostProcessor host;

    // id -> automatable, for an order-independent lookup against the registry.
    std::map<std::string, bool> automatableById;
    for (const auto& f : collectFacts(host))
        automatableById[f.id] = f.automatable;

    REQUIRE(automatableById.size() == mw::params::kParamDefs.size());

    int structuralSeen = 0;
    for (const auto& def : mw::params::kParamDefs)
    {
        const auto it = automatableById.find(std::string{ def.id });
        REQUIRE(it != automatableById.end());
        CHECK(it->second == def.isAutomatable);
        if (!def.isAutomatable)
            ++structuralSeen;
    }

    // The §3.8 structural set (quality, voice.mode, voice.count, unison.count,
    // control.vintage) — exactly 5 — must all be non-automatable.
    CHECK(structuralSeen == 5);
}

TEST_CASE("paramlayout is a deterministic pure function across two independent builds",
          "[paramlayout]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    LayoutHostProcessor hostA;
    LayoutHostProcessor hostB;

    const auto a = collectFacts(hostA);
    const auto b = collectFacts(hostB);

    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() == mw::params::kParamDefs.size());

    // Identical (id, versionHint, automatable) tuples, in identical order, build over
    // build => the host numeric IDs are stable across builds [§3.2; §4].
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CHECK(a[i].id          == b[i].id);
        CHECK(a[i].versionHint == b[i].versionHint);
        CHECK(a[i].automatable == b[i].automatable);
    }
}
