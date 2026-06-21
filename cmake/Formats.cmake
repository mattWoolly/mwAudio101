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

# Absolute path to THIS module, captured at include time. The deferred ctest
# registration below re-runs the gate from a child `cmake -P` that include()s this exact
# file; capturing it now (rather than reading CMAKE_CURRENT_LIST_FILE inside the deferred
# function, which would resolve to the defining file at call time) keeps the path correct.
set(MW_FORMATS_CMAKE_FILE "${CMAKE_CURRENT_LIST_FILE}")

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
    # The map is the sparse §2.1 matrix [docs/design/09 §2.1; ADR-011 Decision
    # validator table; ADR-024 Contract table]; the wired/unwired verdict for each
    # listed validator comes from cmake/Validators.cmake via mw_validator_wired (138),
    # whose interface we consume verbatim and never re-derive here.
    set(_required_validators "")
    set(_platform_legal ON)
    if(_F STREQUAL "VST3")
      # VST3: pluginval everywhere; Steinberg `validator` is wired on macOS only, so it
      # is only a REQUIRED validator there. Off macOS the gate still requires pluginval
      # (and `validator`'s own WIRED flag is forced OFF by Validators.cmake), so listing
      # it off-macOS would make VST3 un-buildable everywhere but macOS — wrong per F-1.
      # [docs/design/09 §2.1 VST3 row, §2.2 F-1; ADR-011 validator map; ADR-024 Contract.]
      if(MW_HOST_PLATFORM STREQUAL "macos")
        set(_required_validators pluginval validator)
      else()
        set(_required_validators pluginval)
      endif()
    elseif(_F STREQUAL "AU")
      # F-3: AU is macOS-only by construction (auval is macOS-only); requesting it on
      # any other platform is a hard configure-time error [ADR-011 C3; docs/design/09 F-3].
      if(NOT MW_HOST_PLATFORM STREQUAL "macos")
        message(FATAL_ERROR
          "mwAudio101: AU is macOS-only; requested on '${MW_HOST_PLATFORM}' [ADR-011 C3; docs/design/09 F-3].")
      endif()
      set(_required_validators auval pluginval)
    elseif(_F STREQUAL "CLAP")
      # CLAP: clap-validator on any desktop platform (pluginval also exercises CLAP, but
      # clap-validator is the format-owning gate per §2.1) [docs/design/09 §2.1 CLAP row].
      set(_required_validators clap-validator)
    elseif(_F STREQUAL "STANDALONE")
      # Standalone: project-provided headless smoke step, wired on any desktop platform
      # [docs/design/09 §2.1 Standalone row].
      set(_required_validators standalone-smoke)
    elseif(_F STREQUAL "LV2")
      # F-6: LV2 is Linux-only at launch, goal-tier (non-blocking). Off Linux it is not
      # platform-legal (hard-removed, never an error in the auto path); on Linux it still
      # requires BOTH lv2lint AND lv2_validate wired — and Validators.cmake only wires
      # those behind MW_BUILD_LV2 on Linux, so LV2 never resolves buildable unless the
      # option is on and both tools are present [docs/design/09 §2.1 LV2 row, F-6;
      # ADR-011 C6; ADR-024 C1/C3].
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

# Default requested set per platform when MWAUDIO_FORMATS is not provided by a preset
# [docs/design/09 §2.2 CMakePresets bullet; ADR-011 Decision per-platform scoping table;
# ADR-024 Decision per-platform scoping (unchanged from ADR-014)]:
#   macOS   = VST3 + AU + CLAP + Standalone   (AU is macOS-only by construction; F-2/F-3)
#   Linux   = VST3 + CLAP + Standalone        (+ LV2 only when MW_BUILD_LV2; F-6 goal-tier)
#   Windows = VST3 + CLAP + Standalone        (no AU, no LV2)
# These mirror CMakePresets.json's per-platform MWAUDIO_FORMATS exactly; a preset that
# sets MWAUDIO_FORMATS overrides this default (which then runs through the same gate).
if(NOT DEFINED MWAUDIO_FORMATS)
  if(MW_HOST_PLATFORM STREQUAL "macos")
    set(MWAUDIO_FORMATS VST3 AU CLAP Standalone)
  elseif(MW_HOST_PLATFORM STREQUAL "linux")
    set(MWAUDIO_FORMATS VST3 CLAP Standalone)
    if(MW_BUILD_LV2)
      list(APPEND MWAUDIO_FORMATS LV2)   # F-6: LV2 only when explicitly opted in.
    endif()
  elseif(MW_HOST_PLATFORM STREQUAL "windows")
    set(MWAUDIO_FORMATS VST3 CLAP Standalone)
  else()
    # An unrecognized host has no validators wired (Validators.cmake only wires for
    # macos/linux/windows), so no format can resolve buildable; default to the empty
    # set rather than claiming a per-platform list the gate would only hard-remove.
    set(MWAUDIO_FORMATS "")
  endif()
endif()

mw_resolve_formats("${MWAUDIO_FORMATS}" MW_RESOLVED_FORMATS)
set(MW_RESOLVED_FORMATS "${MW_RESOLVED_FORMATS}" CACHE INTERNAL "resolved plugin formats for this platform")

# Emit the resolved list for the formats ctest and the future plugin target.
# The `platform=` and `resolved=` lines are STABLE — cmake/CheckFormats.cmake (the
# task-096 aggregate `formats` ctest) parses them verbatim; we only APPEND below.
set(_fmt_report "${CMAKE_BINARY_DIR}/mw_formats_report.txt")
set(_fmt_lines "platform=${MW_HOST_PLATFORM}\nresolved=${MW_RESOLVED_FORMATS}\n")
string(APPEND _fmt_lines "requested=${MWAUDIO_FORMATS}\n")
string(APPEND _fmt_lines "mw_build_lv2=${MW_BUILD_LV2}\n")
# Append the per-validator wired columns so the per-(format,platform) cmake_formats
# ctests can assert that every RESOLVED format's required validator is genuinely wired
# (the positive half of the gate) without re-running configure.
foreach(_vname pluginval clap-validator standalone-smoke auval validator lv2lint lv2_validate)
  mw_validator_wired(${_vname} _vw)
  string(APPEND _fmt_lines "wired_${_vname}=${_vw}\n")
endforeach()
file(WRITE "${_fmt_report}" "${_fmt_lines}")
message(STATUS "mwAudio101: resolved formats (${MW_HOST_PLATFORM}) = ${MW_RESOLVED_FORMATS}")

# ---------------------------------------------------------------------------------
# Configure-time cmake_formats_* ctests (task 137).
#
# Task 137 tag is `cmake_formats`; per the silent-pass rule (AGENTS.md) the selector
# `-R cmake_formats --no-tests=error` must match a concrete, non-empty set whose names
# all begin with the tag word. These are the gate's own assertions, the symmetric
# counterpart to cmake/Validators.cmake's `cmake_validators_*` rows:
#
#   cmake_formats_resolved_<platform>   POSITIVE — the resolved set for THIS host obeys
#       §2.2 F-1..F-8: AAX never present (F-4), AU only on macOS (F-2/F-3), LV2 only on
#       Linux (F-6), and every RESOLVED format's required validator(s) are actually wired
#       (a format may never resolve buildable on an unwired validator — ADR-011 C5).
#   cmake_formats_reject_au_off_macos   NEGATIVE — re-running the real gate with AU on a
#       non-macOS host MUST fail configure (F-3 / ADR-011 C3).
#   cmake_formats_reject_aax            NEGATIVE — the real gate with any AAX request MUST
#       fail configure on every platform (F-4 / ADR-011 C4; ADR-024 C6).
#   cmake_formats_reject_force_unwired  NEGATIVE — force-adding a format whose validator is
#       unwired (MW_FORCE_FORMAT_<F>=ON) MUST fail configure (F-5 / ADR-011 C5).
#
# The negative cases shell out to a child `cmake -P` that re-include()s THIS module with
# injected platform / format / force cache vars, so they exercise the REAL gate end to
# end (not a re-implementation), and pass only when that child FATAL_ERRORs as required.
#
# Registration is DEFERRED to end-of-top-level-scope via cmake_language(DEFER) because
# this module is include()d by the root CMakeLists BEFORE enable_testing() runs; the
# deferred calls fire after enable_testing(), so add_test() resolves. Guarded by
# MW101_TESTS (the only state in which enable_testing() runs).

# Positive checker: re-reads the report and asserts the resolved set respects §2.2.
set(_mw_fmt_checker "${CMAKE_BINARY_DIR}/CheckResolvedFormats.cmake")
file(WRITE "${_mw_fmt_checker}" [=[
# SPDX-License-Identifier: GPL-3.0-or-later
# Generated by cmake/Formats.cmake (task 137). Run with -DREPORT=<mw_formats_report.txt>.
# Asserts the configure-time resolved format set obeys docs/design/09 §2.2 F-1..F-8 for
# the report's platform: AAX never present (F-4); AU only on macOS (F-2/F-3); LV2 only on
# Linux (F-6); and every resolved format's required validator(s) are wired (ADR-011 C5).
if(NOT DEFINED REPORT)
  message(FATAL_ERROR "cmake_formats: REPORT must be set.")
endif()
if(NOT EXISTS "${REPORT}")
  message(FATAL_ERROR "cmake_formats: report not found: ${REPORT} (configure first).")
endif()
file(READ "${REPORT}" _r)
string(REGEX MATCH "platform=([a-z]+)" _ _ "${_r}")
set(_platform "${CMAKE_MATCH_1}")
string(REGEX MATCH "resolved=([A-Za-z0-9;]*)" _ _ "${_r}")
set(_resolved "${CMAKE_MATCH_1}")
macro(_fmt_wired _name _out)
  string(REGEX MATCH "wired_${_name}=([A-Za-z]+)" _ _ "${_r}")
  set(${_out} "${CMAKE_MATCH_1}")
endmacro()

set(_errors "")
# F-4: AAX is never present (the configure would have FATAL_ERROR'd — absence is the check).
if(_resolved MATCHES "AAX")
  list(APPEND _errors "AAX present in resolved formats (must be permanently excluded) [F-4]")
endif()
# F-3: AU only on macOS.
if(_resolved MATCHES "AU" AND NOT _platform STREQUAL "macos")
  list(APPEND _errors "AU present off macOS [F-3]")
endif()
# F-6: LV2 only on Linux.
if(_resolved MATCHES "LV2" AND NOT _platform STREQUAL "linux")
  list(APPEND _errors "LV2 present off Linux [F-6]")
endif()
# ADR-011 C5: every resolved format's required validator(s) must be wired. The gate
# hard-removes any unwired format, so a resolved format with an unwired validator is a
# gate breach.
foreach(_F ${_resolved})
  set(_req "")
  if(_F STREQUAL "VST3")
    set(_req pluginval)
    if(_platform STREQUAL "macos")
      list(APPEND _req validator)
    endif()
  elseif(_F STREQUAL "AU")
    set(_req auval pluginval)
  elseif(_F STREQUAL "CLAP")
    set(_req clap-validator)
  elseif(_F STREQUAL "STANDALONE")
    set(_req standalone-smoke)
  elseif(_F STREQUAL "LV2")
    set(_req lv2lint lv2_validate)
  endif()
  foreach(_v ${_req})
    _fmt_wired(${_v} _w)
    if(NOT _w STREQUAL "ON")
      list(APPEND _errors "resolved format '${_F}' but its validator '${_v}' is not wired [ADR-011 C5]")
    endif()
  endforeach()
endforeach()

if(_errors)
  string(REPLACE ";" "\n  " _e "${_errors}")
  message(FATAL_ERROR "cmake_formats FAILED [docs/design/09 §2.2; ADR-011 C1-C8; ADR-024]:\n  ${_e}")
endif()
message(STATUS
  "cmake_formats(resolved/${_platform}): PASS — resolved='${_resolved}' respects the §2.2 F-1..F-8 gate.")
]=])

# Negative checker: re-include()s the REAL Formats.cmake with injected vars and asserts
# it FATAL_ERRORs (i.e. the child `cmake -P` exits non-zero). Run via add_test with
# WILL_FAIL is brittle for -P scripts; instead this wrapper drives the child itself and
# inverts the result, so a child that DID NOT fail turns the ctest red.
set(_mw_fmt_expectfail "${CMAKE_BINARY_DIR}/ExpectFatalFormats.cmake")
file(WRITE "${_mw_fmt_expectfail}" [=[
# SPDX-License-Identifier: GPL-3.0-or-later
# Generated by cmake/Formats.cmake (task 137). Run with:
#   -DFORMATS_CMAKE=<path> -DCASE=<au_off_macos|aax|force_unwired> -DSCRATCH=<dir>
# Drives a child `cmake -P` that re-include()s the REAL Formats.cmake with cache vars
# forcing the named F-3/F-4/F-5 error condition, and PASSES only when that child fails
# configure (exits non-zero) — so the gate's FATAL branch can never silently pass.
if(NOT DEFINED FORMATS_CMAKE OR NOT DEFINED CASE OR NOT DEFINED SCRATCH)
  message(FATAL_ERROR "cmake_formats(expect-fatal): FORMATS_CMAKE, CASE and SCRATCH must be set.")
endif()
file(MAKE_DIRECTORY "${SCRATCH}")

# Build the per-case injection script the child runs. Each forces the REAL gate to hit
# exactly one normative error branch [docs/design/09 §2.2 F-3/F-4/F-5].
set(_inj "")
if(CASE STREQUAL "au_off_macos")
  # F-3: AU on a non-macOS host. Force platform=linux and request AU.
  set(_inj "set(MW_HOST_PLATFORM linux)\nset(MWAUDIO_FORMATS AU)\n")
elseif(CASE STREQUAL "aax")
  # F-4: any AAX request, on any platform (use the real host platform).
  set(_inj "set(MWAUDIO_FORMATS AAX)\n")
elseif(CASE STREQUAL "force_unwired")
  # F-5: force-add VST3 while its validators are forced UNWIRED. MW_FORCE_VALIDATOR_*=OFF
  # makes the validators report unwired even if the tool is installed; MW_FORCE_FORMAT_VST3
  # turns the resulting hard-remove into the F-5 configure error.
  set(_inj "set(MW_FORCE_VALIDATOR_pluginval OFF CACHE INTERNAL \"\" FORCE)\nset(MW_FORCE_VALIDATOR_validator OFF CACHE INTERNAL \"\" FORCE)\nset(MW_FORCE_FORMAT_VST3 ON)\nset(MWAUDIO_FORMATS VST3)\n")
else()
  message(FATAL_ERROR "cmake_formats(expect-fatal): unknown CASE '${CASE}'.")
endif()

set(_child "${SCRATCH}/_mw_fmt_case_${CASE}.cmake")
# Run the child against a throwaway SCRATCH binary dir so its file(WRITE report) never
# clobbers the real build tree's report. CMAKE_BINARY_DIR is set inside the child (in
# `cmake -P` script mode it otherwise defaults to the cwd), because Formats.cmake's
# report writer keys off ${CMAKE_BINARY_DIR}. MW101_TESTS OFF stops the child from
# re-registering ctests (cmake_language(DEFER) is a no-op in script mode anyway).
file(WRITE "${_child}"
  "set(MW101_TESTS OFF)\n"
  "set(CMAKE_BINARY_DIR \"${SCRATCH}\")\n"
  "${_inj}"
  "include(\"${FORMATS_CMAKE}\")\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -P "${_child}"
  WORKING_DIRECTORY "${SCRATCH}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err)

if(_rc EQUAL 0)
  message(FATAL_ERROR
    "cmake_formats(reject/${CASE}) FAILED: the gate did NOT reject the case "
    "[docs/design/09 §2.2 F-3/F-4/F-5; ADR-011 C3-C5; ADR-024 C6]. child stdout/stderr:\n${_out}\n${_err}")
endif()
message(STATUS "cmake_formats(reject/${CASE}): PASS — the gate rejected the case (child exited ${_rc}).")
]=])

function(_mw_register_formats_ctests)
  add_test(NAME "cmake_formats_resolved_${MW_HOST_PLATFORM}"
    COMMAND "${CMAKE_COMMAND}"
            "-DREPORT=${CMAKE_BINARY_DIR}/mw_formats_report.txt"
            -P "${CMAKE_BINARY_DIR}/CheckResolvedFormats.cmake")
  # F-4 (AAX) is platform-independent; F-3 (AU off macOS) and F-5 (force-unwired) are
  # always meaningful too (we inject the platform/force state, not the host's).
  foreach(_case au_off_macos aax force_unwired)
    add_test(NAME "cmake_formats_reject_${_case}"
      COMMAND "${CMAKE_COMMAND}"
              "-DFORMATS_CMAKE=${MW_FORMATS_CMAKE_FILE}"
              "-DCASE=${_case}"
              "-DSCRATCH=${CMAKE_BINARY_DIR}/mw_formats_reject/${_case}"
              -P "${CMAKE_BINARY_DIR}/ExpectFatalFormats.cmake")
  endforeach()
endfunction()

if(MW101_TESTS)
  cmake_language(DEFER CALL _mw_register_formats_ctests)
endif()
