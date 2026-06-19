// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/params/ParameterLayout.cpp — buildParameterLayout() (task 020).
// Realizes docs/design/06 §4; consumes the JUCE-free registry kParamDefs (§3.0/§3.1).

#include "params/ParameterLayout.h"

#include "params/ParamDefs.h"   // mwcore (JUCE-free): mw::params::kParamDefs / ParamDef / ParamType

namespace mw::plugin {

namespace {

// Build the fixed label list for a Choice / Bool entry from the registry's pointer
// table. The list is exactly def.choiceCount entries (software-ext indices live in
// the list and are fenced by the UI, not dropped here) [§3.4; §4; ADR-008 C6].
juce::StringArray makeChoiceLabels(const mw::params::ParamDef& def)
{
    juce::StringArray labels;
    labels.ensureStorageAllocated(static_cast<int>(def.choiceCount));
    for (std::uint8_t i = 0; i < def.choiceCount; ++i)
        labels.add(juce::String::fromUTF8(def.choices[i]));
    return labels;
}

// Emit ONE juce parameter for a single registry entry, dispatching on def.type.
// Every parameter is constructed with juce::ParameterID{def.id, def.versionAdded}
// (the version hint == versionAdded) so the string ID drives the deterministic host
// numeric ID [§3.2; §4; ADR-008 C3]. Structural params (isAutomatable == false) are
// built withAutomatable(false) [§3.8; ADR-008 C7].
std::unique_ptr<juce::RangedAudioParameter> makeParameter(const mw::params::ParamDef& def)
{
    const juce::ParameterID id{ juce::String::fromUTF8(def.id),
                                static_cast<int>(def.versionAdded) };
    const juce::String name = juce::String::fromUTF8(def.label);

    switch (def.type)
    {
        case mw::params::ParamType::Continuous:
        {
            // NormalisableRange{start, end, interval, skew, useSymmetricSkew}.
            // step == 0 => fully continuous interval; skew comes from the registry
            // (1.0 == linear); symmetricSkew tapers about the centre (e.g. vco.fine).
            const juce::NormalisableRange<float> range{
                def.minValue, def.maxValue, def.step, def.skew, def.symmetricSkew };

            auto attrs = juce::AudioParameterFloatAttributes()
                             .withLabel(juce::String::fromUTF8(def.unit))
                             .withAutomatable(def.isAutomatable);

            return std::make_unique<juce::AudioParameterFloat>(
                id, name, range, def.defaultValue, attrs);
        }

        case mw::params::ParamType::Choice:
        {
            auto attrs = juce::AudioParameterChoiceAttributes()
                             .withAutomatable(def.isAutomatable);

            return std::make_unique<juce::AudioParameterChoice>(
                id, name, makeChoiceLabels(def),
                static_cast<int>(def.defaultValue), attrs);
        }

        case mw::params::ParamType::Bool:
        {
            // Bool labels (choices[0]=off, choices[1]=on) are advisory display text;
            // surface them via the value->string function so a host shows e.g.
            // "Modern"/"Vintage" rather than a bare 0/1 [§3.8].
            const juce::String offLabel = juce::String::fromUTF8(def.choices[0]);
            const juce::String onLabel  = juce::String::fromUTF8(def.choices[1]);

            auto attrs = juce::AudioParameterBoolAttributes()
                             .withAutomatable(def.isAutomatable)
                             .withStringFromValueFunction(
                                 [offLabel, onLabel](bool v, int) {
                                     return v ? onLabel : offLabel;
                                 });

            return std::make_unique<juce::AudioParameterBool>(
                id, name, def.defaultValue >= 0.5f, attrs);
        }
    }

    // Unreachable: ParamType is a closed 3-value enum and the registry's compile-time
    // invariants keep every entry well-formed. Fail loudly if a fourth type ever lands.
    jassertfalse;
    return {};
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout buildParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // One parameter per LIVE kParamDefs entry, in §3.0 table order. No parameter is
    // constructed anywhere else; the layout is a pure function of kParamDefs [§4].
    for (const auto& def : mw::params::kParamDefs)
        layout.add(makeParameter(def));

    return layout;
}

} // namespace mw::plugin
