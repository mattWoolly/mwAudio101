# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/Formats.cmake — configure-time "no unvalidated artifact" gate (task 096).
#
# Realizes docs/design/09 §2.2 (F-1..F-8) and ADR-011 C1-C8 / ADR-024 C1-C6.
# Resolves MWAUDIO_FORMATS per platform, maps each (format,platform) to its
# required validator (from cmake/Validators.cmake), HARD-REMOVES any format whose
# validator is unwired, and message(FATAL_ERROR ...) on AU off macOS (F-3), on any
# AAX target (F-4), and on force-adding an unwired format (F-5). Emits the resolved
# final format list for the eventual juce_add_plugin target (task 113).
#
# NB: this is configure-time RESOLUTION only; it adds no build target. It runs
# whether or not MW_BUILD_PLUGIN is ON, so the gate logic is testable JUCE-free.

include("${CMAKE_CURRENT_LIST_DIR}/Validators.cmake")

# mw_resolve_formats(<requested-list> OUT_VAR)
#   requested-list: any of VST3 AU CLAP Standalone LV2 (AAX is always fatal).
function(mw_resolve_formats requested out_var)
  set(_resolved "")
  foreach(_fmt ${requested})
    string(TOUPPER "${_fmt}" _F)

    # F-4: AAX is unconditionally, permanently excluded on every platform.
    if(_F STREQUAL "AAX")
      message(FATAL_ERROR
        "mwAudio101: AAX is permanently excluded (no open validator) [ADR-011 C4; ADR-024 C6; docs/design/09 F-4].")
    endif()

    # Map (format,platform) -> required validator(s) and a "platform-legal" flag.
    set(_required_validators "")
    set(_platform_legal ON)
    if(_F STREQUAL "VST3")
      set(_required_validators pluginval validator)
      if(MW_HOST_PLATFORM STREQUAL "macos")
        set(_required_validators pluginval validator)
      else()
        set(_required_validators pluginval)   # Steinberg validator wired on macOS only
      endif()
    elseif(_F STREQUAL "AU")
      # F-3: AU is macOS-only by construction.
      if(NOT MW_HOST_PLATFORM STREQUAL "macos")
        message(FATAL_ERROR
          "mwAudio101: AU is macOS-only; requested on '${MW_HOST_PLATFORM}' [ADR-011 C3; docs/design/09 F-3].")
      endif()
      set(_required_validators auval pluginval)
    elseif(_F STREQUAL "CLAP")
      set(_required_validators clap-validator)
    elseif(_F STREQUAL "STANDALONE")
      set(_required_validators standalone-smoke)
    elseif(_F STREQUAL "LV2")
      # F-6: LV2 is Linux-only at launch, goal-tier (non-blocking).
      if(NOT MW_HOST_PLATFORM STREQUAL "linux")
        set(_platform_legal OFF)
      endif()
      set(_required_validators lv2lint lv2_validate)
    else()
      message(FATAL_ERROR "mwAudio101: unknown format '${_fmt}' requested.")
    endif()

    # Is every required validator wired for this platform?
    set(_all_wired ON)
    foreach(_v ${_required_validators})
      mw_validator_wired(${_v} _w)
      if(NOT _w)
        set(_all_wired OFF)
      endif()
    endforeach()

    if(_platform_legal AND _all_wired)
      list(APPEND _resolved ${_F})
    else()
      # F-5: a force-add of an unwired format is a configure-time error; an
      # ordinary (auto-resolved) request is hard-removed silently with a note.
      if(MW_FORCE_FORMAT_${_F})
        message(FATAL_ERROR
          "mwAudio101: format '${_F}' force-added but its validator is not wired on "
          "'${MW_HOST_PLATFORM}' [ADR-011 C5; docs/design/09 F-5].")
      endif()
      message(STATUS "mwAudio101: format '${_F}' hard-removed (validator unwired / not platform-legal here).")
    endif()
  endforeach()
  set(${out_var} "${_resolved}" PARENT_SCOPE)
endfunction()

# Default requested set per platform when MWAUDIO_FORMATS is not provided by a
# preset [docs/design/09 §2.2 CMakePresets bullet; ADR-024].
if(NOT DEFINED MWAUDIO_FORMATS)
  if(MW_HOST_PLATFORM STREQUAL "macos")
    set(MWAUDIO_FORMATS VST3 AU CLAP Standalone)
  elseif(MW_HOST_PLATFORM STREQUAL "linux")
    set(MWAUDIO_FORMATS VST3 CLAP Standalone)
    if(MW_BUILD_LV2)
      list(APPEND MWAUDIO_FORMATS LV2)
    endif()
  else()  # windows / other
    set(MWAUDIO_FORMATS VST3 CLAP Standalone)
  endif()
endif()

mw_resolve_formats("${MWAUDIO_FORMATS}" MW_RESOLVED_FORMATS)
set(MW_RESOLVED_FORMATS "${MW_RESOLVED_FORMATS}" CACHE INTERNAL "resolved plugin formats for this platform")

# Emit the resolved list for the formats ctest and the future plugin target.
set(_fmt_report "${CMAKE_BINARY_DIR}/mw_formats_report.txt")
file(WRITE "${_fmt_report}" "platform=${MW_HOST_PLATFORM}\nresolved=${MW_RESOLVED_FORMATS}\n")
message(STATUS "mwAudio101: resolved formats (${MW_HOST_PLATFORM}) = ${MW_RESOLVED_FORMATS}")
