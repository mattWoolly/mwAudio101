# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# plugin/CMakeLists.txt — the single juce_add_plugin target over the shared
# MwAudioProcessor (task 113, MINIMAL-bootstrap version).
#
# BOOTSTRAP SCOPE: this is the JUCE-phase bootstrap. It proves the full JUCE
# build+link works in-tree by wiring ONE juce_add_plugin target over ONE
# MwAudioProcessor (task 111, minimal) that owns a mw::Engine. The render path is
# identical across formats [ADR-001 C14; ADR-011 Decision] — there is exactly one
# processor and no DSP fork.
#
# Formats are STANDALONE-ONLY for now: the cmake/Formats.cmake "never build a
# format without a validator" gate (docs/design/09 §2.2; ADR-011 C1-C8) hard-removes
# any format whose validator is not wired on this box. Standalone needs no external
# validator (only the project's standalone-smoke step), so it is the one format that
# resolves on a bare dev machine. VST3 / AU / CLAP / LV2 wiring is DEFERRED to the
# full task 113 (they enable once their validators — pluginval / auval / Steinberg
# validator / clap-validator / lv2lint — are wired in CI). AAX is PERMANENTLY
# excluded [ADR-024 C6; docs/design/09 F-4].
#
# DEFERRED to leaf tasks (NOT implemented here):
#   - full 91-param APVTS layout ........................ task 020
#   - MidiFrontEnd / HostEventNormalizer / translation .. task 104 / 111 (full)
#   - CapabilityShim ..................................... task 102
#   - LatencyReporter / setLatencySamples (PDC) ......... task 105
#   - the real UI (replaces the GenericAudioProcessorEditor) ... ui stream (108+)
#   - VST3 / AU / CLAP / LV2 format targets ............. task 113 (full)

if(NOT MW_BUILD_PLUGIN)
  return()
endif()

if(NOT TARGET juce::juce_audio_processors)
  message(FATAL_ERROR
    "plugin/: MW_BUILD_PLUGIN is ON but JUCE was not fetched. Check cmake/Dependencies.cmake.")
endif()

# --- Resolve the format list through the gate (bootstrap: Standalone only) ------
# cmake/Formats.cmake (included at the top level) already populated
# MW_RESOLVED_FORMATS by running the validator gate over the platform default.
# For the bootstrap we constrain the juce_add_plugin FORMATS to Standalone only —
# the one format whose validator (standalone-smoke) is unconditionally wired — and
# assert it survived the gate so we never silently ship an unvalidated artifact.
mw_resolve_formats("Standalone" MW_BOOTSTRAP_FORMATS)
if(NOT "STANDALONE" IN_LIST MW_BOOTSTRAP_FORMATS)
  message(FATAL_ERROR
    "plugin/: Standalone did not survive the format gate (validator unwired?). "
    "See cmake/Formats.cmake / cmake/Validators.cmake.")
endif()

# JUCE wants the human-cased format token for juce_add_plugin FORMATS.
set(MW_JUCE_FORMATS Standalone)
message(STATUS "mwAudio101: plugin formats (bootstrap) = ${MW_JUCE_FORMATS}")

# --- The single shared plugin target (task 113) ---------------------------------
juce_add_plugin(mwAudio101
  COMPANY_NAME            "Matt Woolly"
  BUNDLE_ID               "io.mwaudio.mwAudio101"
  PLUGIN_MANUFACTURER_CODE Mwly
  PLUGIN_CODE             Mw01
  FORMATS                 ${MW_JUCE_FORMATS}
  PRODUCT_NAME            "mwAudio101"
  DESCRIPTION             "Circuit-accurate SH-101-inspired software synthesizer"
  IS_SYNTH                TRUE
  NEEDS_MIDI_INPUT        TRUE
  NEEDS_MIDI_OUTPUT       FALSE
  IS_MIDI_EFFECT          FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

# --- The shared processor sources (task 111, minimal) ---------------------------
target_sources(mwAudio101 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/PluginProcessor.cpp")

target_include_directories(mwAudio101 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_features(mwAudio101 PUBLIC cxx_std_20)

# JUCE compile-time config flags. JUCE_VST3_CAN_REPLACE_VST2=0 (no VST2 SDK);
# JUCE_REPORT_APP_USAGE=0 + JUCE_DISPLAY_SPLASH_SCREEN=0 are licit on the GPL tier.
target_compile_definitions(mwAudio101 PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_REPORT_APP_USAGE=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_STRICT_REFCOUNTEDPOINTER=1)

# Link: the JUCE-free DSP core + the FROZEN fp-discipline flags + the JUCE modules a
# synth shell needs. juce::juce_recommended_config_flags carries JUCE's own warning/
# optimization defaults [ADR-001 C12 — mwcore carries fp_discipline transitively, but
# the plugin TUs link it explicitly so the shell math matches the core].
target_link_libraries(mwAudio101 PRIVATE
  mwcore
  mw_fp_discipline
  juce::juce_audio_utils        # AudioProcessor + GenericAudioProcessorEditor + devices
  juce::juce_audio_processors
  juce::juce_dsp
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)
