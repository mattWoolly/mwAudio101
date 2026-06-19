// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/params/ParameterLayout.h — buildParameterLayout(): the APVTS layout
// generator (task 020). Realizes docs/design/06 §4 (and §3.1, §3.2).
//
// This is the SINGLE place a juce parameter is constructed for the plugin: it is a
// pure function of the JUCE-free declarative registry core/params/ParamDefs.h
// (kParamDefs). One juce parameter is emitted per LIVE kParamDefs entry, in table
// order:
//   ParamType::Continuous -> juce::AudioParameterFloat with a
//       juce::NormalisableRange<float>{min, max, step, skew} (symmetric skew where
//       def.symmetricSkew);
//   ParamType::Choice     -> juce::AudioParameterChoice with the fixed label list;
//   ParamType::Bool       -> juce::AudioParameterBool.
// Every parameter is constructed with juce::ParameterID{def.id, def.versionAdded}
// so the immutable string ID drives the deterministic VST3/AU/CLAP numeric ID
// (JUCE hashes the string ID + version hint) [docs/design/06 §3.2; ADR-008 C3].
// Structural params (def.isAutomatable == false) are built withAutomatable(false)
// [docs/design/06 §3.8; ADR-008 C7].
//
// WHY plugin/ AND NOT core/: this TU references juce::* types, so it CANNOT live in
// mwcore (the no-JUCE-in-core guard, cmake/NoJuceInCore.cmake, fails the build on any
// JUCE include/token under core/) [ADR-001 C1; ADR-014 C11]. The registry it consumes
// (kParamDefs) remains JUCE-free in core/params/ParamDefs.h; only this thin generator
// lives plugin-side, matching the JUCE/JUCE-free split [docs/design/00 §3.3].

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mw::plugin {

// Mechanically builds the full APVTS ParameterLayout from kParamDefs (§4). Pure
// function of the registry: same table in => same layout out, every call. Runs once
// at processor construction on the message thread [docs/design/06 §4 RT invariant].
juce::AudioProcessorValueTreeState::ParameterLayout buildParameterLayout();

} // namespace mw::plugin
