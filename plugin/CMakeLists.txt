# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# plugin/CMakeLists.txt — the single juce_add_plugin target over the shared
# MwAudioProcessor (task 113, FULL version).
#
# SCOPE: ONE juce_add_plugin(mwAudio101 ...) target produces EVERY format
# (VST3 / AU / CLAP / Standalone / LV2) over the ONE MwAudioProcessor — no DSP fork;
# every wrapper differs only in plugin/ [docs/design/09 §2.1, §3.1; ADR-001 C14;
# ADR-011 Decision + C11; ADR-024 C5]. Build-side wiring only: the engine and the
# processor shell are owned by the core / task-111 streams.
#
# FORMAT RESOLUTION IS GATED, NOT HARD-CODED: cmake/Formats.cmake (included at the top
# level) runs the "never build a format without a wired validator" gate (§2.2;
# ADR-011 C1-C8; ADR-024 C6) and publishes MW_RESOLVED_FORMATS. We feed THAT resolved
# list straight into juce_add_plugin FORMATS — so VST3 / AU / CLAP / LV2 flow through
# natively in CI once pluginval / auval / Steinberg validator / clap-validator /
# lv2lint+lv2_validate are wired, while on a bare dev box (only standalone-smoke wired)
# the gate hard-removes the rest and FORMATS resolves to Standalone alone, BY DESIGN.
# We never weaken or bypass the gate to force an unvalidated artifact.
#
# Per-format toolchain [docs/design/09 §2.1 matrix; ADR-024 Contract table]:
#   - VST3 / AU / Standalone : JUCE native exporters (AU is macOS-only by construction)
#   - CLAP                   : clap-juce-extensions wrapping the SAME processor (§2.1;
#                              ADR-024 Contract) — added below when CLAP survives the gate
#   - LV2                    : JUCE NATIVE LV2 exporter ONLY, behind MW_BUILD_LV2, Linux
#                              goal-tier (F-6); NO clap-wrapper is vendored (ADR-024 C1)
#   - AAX                    : PERMANENTLY excluded — mw_resolve_formats FATAL_ERRORs on
#                              any AAX request [ADR-011 C4; ADR-024 C6; §2.2 F-4]
#
# DEFERRED to leaf tasks (NOT implemented here):
#   - CI validator wiring (pluginval / auval / Steinberg / clap-validator) tasks 138/156
#   - the real UI (replaces the GenericAudioProcessorEditor) ... ui stream (108+)

if(NOT MW_BUILD_PLUGIN)
  return()
endif()

if(NOT TARGET juce::juce_audio_processors)
  message(FATAL_ERROR
    "plugin/: MW_BUILD_PLUGIN is ON but JUCE was not fetched. Check cmake/Dependencies.cmake.")
endif()

# --- Consume the gate-resolved format list (NEVER hard-coded) -------------------
# cmake/Formats.cmake (included at the top level) already ran the validator gate over
# the per-platform default set and published MW_RESOLVED_FORMATS in UPPER-CASE tokens
# (e.g. STANDALONE;VST3;AU;CLAP / on Linux +LV2 when MW_BUILD_LV2). We map those to the
# human-cased FORMATS tokens juce_add_plugin expects and feed them straight through.
# On a bare dev box only STANDALONE survives the gate (only standalone-smoke is wired),
# so this is Standalone-only here BY DESIGN; in CI the wired validators let the rest
# flow through. CLAP is NOT a juce_add_plugin FORMATS token — it is added separately
# below via clap-juce-extensions — so we split it out of the JUCE FORMATS list.
set(MW_JUCE_FORMATS "")
set(MW_CLAP_RESOLVED OFF)
foreach(_F ${MW_RESOLVED_FORMATS})
  if(_F STREQUAL "VST3")
    list(APPEND MW_JUCE_FORMATS VST3)
  elseif(_F STREQUAL "AU")
    list(APPEND MW_JUCE_FORMATS AU)
  elseif(_F STREQUAL "STANDALONE")
    list(APPEND MW_JUCE_FORMATS Standalone)
  elseif(_F STREQUAL "LV2")
    # F-6: LV2 via JUCE's NATIVE LV2 exporter only, behind MW_BUILD_LV2 (Linux goal).
    # mw_resolve_formats only admits LV2 on Linux with lv2lint+lv2_validate wired AND
    # MW_BUILD_LV2 on (it is absent from the default set otherwise), so reaching here
    # already implies the option is set and the validators are green.
    list(APPEND MW_JUCE_FORMATS LV2)
  elseif(_F STREQUAL "CLAP")
    # CLAP is produced by clap-juce-extensions (§2.1; ADR-024 Contract), not a JUCE
    # FORMATS token — record that it survived the gate and add its target below.
    set(MW_CLAP_RESOLVED ON)
  endif()
endforeach()

# Defensive: the gate must never resolve to an empty set on a supported dev box; an
# empty FORMATS would silently build nothing. Standalone is the universal floor.
if(NOT MW_JUCE_FORMATS AND NOT MW_CLAP_RESOLVED)
  message(FATAL_ERROR
    "plugin/: the format gate resolved to an EMPTY set on '${MW_HOST_PLATFORM}'. "
    "At least Standalone (standalone-smoke) should be wired. "
    "See cmake/Formats.cmake / cmake/Validators.cmake.")
endif()

message(STATUS "mwAudio101: juce_add_plugin FORMATS = ${MW_JUCE_FORMATS} (CLAP via clap-juce-extensions: ${MW_CLAP_RESOLVED})")

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

# --- The shared plugin sources (CONFIGURE_DEPENDS glob; JUCE-phase tasks just drop
# plugin/**/*.cpp and re-run configure — no edit here) ---------------------------
file(GLOB_RECURSE MW_PLUGIN_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
target_sources(mwAudio101 PRIVATE ${MW_PLUGIN_SOURCES})

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

# --- CLAP target via clap-juce-extensions (wraps the SAME processor) -------------
# CLAP is not a juce_add_plugin FORMATS token; clap-juce-extensions (Surge synth team)
# adds a `mwAudio101_CLAP` target that wraps the one shared MwAudioProcessor — no DSP
# fork [docs/design/09 §2.1; ADR-011 Decision; ADR-024 Contract]. It is added ONLY when
# CLAP survived the validator gate (clap-validator wired). On a bare dev box (no
# clap-validator) MW_CLAP_RESOLVED is OFF, so nothing here runs and the Standalone build
# is untouched.
#
# Building CLAP requires the clap-juce-extensions sources, which are fetched lazily and
# only when MW_BUILD_CLAP is ON (cmake/Dependencies.cmake — the dep is heavy). The two
# must agree: if the gate admitted CLAP but the wrapper was never fetched we cannot
# honor the gate, so that is a configure-time error rather than a silent CLAP drop —
# wiring clap-validator in CI must be paired with MW_BUILD_CLAP=ON.
if(MW_CLAP_RESOLVED)
  if(NOT COMMAND clap_juce_extensions_plugin)
    message(FATAL_ERROR
      "plugin/: CLAP survived the validator gate (clap-validator wired) but "
      "clap-juce-extensions was not fetched. Configure with -DMW_BUILD_CLAP=ON so "
      "cmake/Dependencies.cmake fetches the wrapper [docs/design/09 §2.1; ADR-024 Contract].")
  endif()

  # CLAP_FEATURES per the CLAP plugin-feature taxonomy: this is an instrument synth.
  # CLAP_ID is the stable reverse-DNS plugin identity (matches the JUCE BUNDLE_ID base).
  clap_juce_extensions_plugin(
    TARGET     mwAudio101
    CLAP_ID    "io.mwaudio.mwAudio101"
    CLAP_FEATURES instrument synthesizer)

  message(STATUS "mwAudio101: CLAP target (mwAudio101_CLAP) wired via clap-juce-extensions.")
endif()
