# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/Validators.cmake — per-format validator target locator (tasks 095/138).
#
# Realizes docs/design/09 §2.1/§2.2 and ADR-011/ADR-024. Locates or declares each
# per-format validator, sets a cached per-validator WIRED flag (ON only when the
# tool is actually found/installed for this platform), exposes a queryable
# validator->wired-on-this-platform map for cmake/Formats.cmake to consume, and
# registers a configure-time `cmake_validators_*` ctest per (format, platform)
# validator so the platform contract is gated, not merely hoped. NO format/target
# resolution logic lives here (that is cmake/Formats.cmake). No validator is
# INVOKED here — this is configure-time discovery + assertion only.
#
# Per §2.1 [ADR-011 map lines 150-157; ADR-024 Contract table]:
#   - pluginval + Steinberg `validator` : VST3 (validator macOS-only); pluginval also
#     exercises AU/CLAP/Standalone, so it is wired on any desktop platform.
#   - auval                              : AU, macOS only (auval is macOS-only).
#   - clap-validator                     : CLAP, any desktop platform.
#   - standalone-smoke                   : Standalone headless smoke, any desktop
#     platform (a project-provided step, not an external tool — always a wired step).
#   - lv2lint + lv2_validate             : LV2, Linux only AND behind MW_BUILD_LV2
#     (F-6 goal-tier; LV2 is the sole opt-in format) [task 138 scope; ADR-024 C3].

# Detect platform once.
if(NOT DEFINED MW_HOST_PLATFORM)
  if(APPLE)
    set(MW_HOST_PLATFORM "macos")
  elseif(WIN32)
    set(MW_HOST_PLATFORM "windows")
  elseif(UNIX)
    set(MW_HOST_PLATFORM "linux")
  else()
    set(MW_HOST_PLATFORM "unknown")
  endif()
endif()

# mw_validator_wired(<name> OUT_VAR) — query a validator's wired flag.
# Interface is STABLE — cmake/Formats.cmake's mw_resolve_formats consumes it verbatim.
function(mw_validator_wired name out_var)
  set(${out_var} "${MW_VALIDATOR_${name}_WIRED}" PARENT_SCOPE)
endfunction()

# _mw_declare_validator(<name> <available-on-this-platform-bool> [find-names...])
#   When available-on-platform is true, find_program the tool; WIRED iff found.
#   When false (e.g. auval off macOS, lv2lint off Linux or MW_BUILD_LV2 off), WIRED
#   is forced OFF unconditionally — the ADR-011 "no unvalidated artifact" gate then
#   hard-removes the corresponding format (it can never resolve buildable).
function(_mw_declare_validator name available_on_platform)
  set(_wired OFF)
  if(available_on_platform)
    # Allow injection for the unit/gate test harness (mock inputs) without requiring
    # the tool to be installed on the dev box.
    if(DEFINED MW_FORCE_VALIDATOR_${name})
      set(_wired ${MW_FORCE_VALIDATOR_${name}})
    else()
      string(TOLOWER "${name}" _prog)
      find_program(MW_VALIDATOR_PROG_${name} NAMES ${ARGN} ${_prog})
      if(MW_VALIDATOR_PROG_${name})
        set(_wired ON)
      endif()
    endif()
  endif()
  set(MW_VALIDATOR_${name}_WIRED "${_wired}" CACHE INTERNAL "validator ${name} wired on this platform")
endfunction()

# Per-platform availability per docs/design/09 §2.1.
set(_is_macos OFF)
set(_is_linux OFF)
set(_is_windows OFF)
if(MW_HOST_PLATFORM STREQUAL "macos")
  set(_is_macos ON)
elseif(MW_HOST_PLATFORM STREQUAL "linux")
  set(_is_linux ON)
elseif(MW_HOST_PLATFORM STREQUAL "windows")
  set(_is_windows ON)
endif()

# pluginval + clap-validator: all three desktop platforms.
set(_any_desktop OFF)
if(_is_macos OR _is_linux OR _is_windows)
  set(_any_desktop ON)
endif()
_mw_declare_validator(pluginval       ${_any_desktop} pluginval)
_mw_declare_validator(clap-validator  ${_any_desktop} clap-validator)

# standalone-smoke is a project-provided headless launch step, not an external tool:
# it is a wired step on any desktop platform (no find_program — the smoke harness is
# built from our own sources by the host-smoke matrix task).
set(MW_VALIDATOR_standalone-smoke_WIRED "${_any_desktop}" CACHE INTERNAL "standalone-smoke wired on this platform" FORCE)

# auval + Steinberg validator: macOS only.
_mw_declare_validator(auval     ${_is_macos} auval)
_mw_declare_validator(validator ${_is_macos} validator)   # Steinberg VST3 validator

# lv2lint + lv2_validate: Linux only AND behind MW_BUILD_LV2 (F-6 goal-tier).
# LV2 is the single opt-in format: its validators stay UNWIRED unless the build
# explicitly opts in via MW_BUILD_LV2 on Linux, so the gate never admits LV2 on a
# platform/option where it is out of scope [task 138 scope; docs/design/09 F-6;
# ADR-011 C6; ADR-024 C3]. MW_BUILD_LV2 defaults OFF (root CMakeLists option).
set(_lv2_available OFF)
if(_is_linux AND MW_BUILD_LV2)
  set(_lv2_available ON)
endif()
_mw_declare_validator(lv2lint      ${_lv2_available} lv2lint)
_mw_declare_validator(lv2_validate ${_lv2_available} lv2_validate)

# Canonical ordered validator list + the (format, platform) key each validator gates.
# Used by the report writer and the per-(format,platform) ctest registration below.
set(MW_VALIDATOR_NAMES
  pluginval clap-validator standalone-smoke auval validator lv2lint lv2_validate)
# validator -> the format it gates (for the ctest name key); platform key is the host.
set(MW_VALIDATOR_FORMAT_pluginval        vst3)   # also exercises au/clap/standalone
set(MW_VALIDATOR_FORMAT_clap-validator   clap)
set(MW_VALIDATOR_FORMAT_standalone-smoke standalone)
set(MW_VALIDATOR_FORMAT_auval            au)
set(MW_VALIDATOR_FORMAT_validator        vst3)
set(MW_VALIDATOR_FORMAT_lv2lint          lv2)
set(MW_VALIDATOR_FORMAT_lv2_validate     lv2)

# Emit a machine-readable report the validators / cmake_validators ctests diff against.
# Format/STATUS line are STABLE — cmake/CheckValidators.cmake (the task-095 `validators`
# ctest) parses this verbatim; we only APPEND the per-validator legality columns.
set(_report "${CMAKE_BINARY_DIR}/mw_validators_report.txt")
set(_report_lines "platform=${MW_HOST_PLATFORM}\n")
string(APPEND _report_lines "mw_build_lv2=${MW_BUILD_LV2}\n")
foreach(_v ${MW_VALIDATOR_NAMES})
  string(APPEND _report_lines "${_v}=${MW_VALIDATOR_${_v}_WIRED}\n")
endforeach()
file(WRITE "${_report}" "${_report_lines}")
message(STATUS "mwAudio101: validator wired-map written to ${_report} (platform=${MW_HOST_PLATFORM})")

# ---------------------------------------------------------------------------------
# Per-(format, platform) configure-time ctests: cmake_validators_<format>_<name>.
#
# Task 138 scope: "Register each as a ctest target keyed by (format, platform)." Each
# validator gets a ctest that asserts its wired flag obeys the §2.1 / ADR-024
# per-platform + MW_BUILD_LV2 contract for THIS host: a validator must NEVER report
# wired on a platform (or under an option state) where it cannot legally exist
# (auval/validator off macOS; lv2* off Linux or with MW_BUILD_LV2 off). A validator
# legitimately ABSENT because the tool is not installed is NOT a failure — the gate
# (Formats.cmake) hard-removes that format, which is correct (ADR-011 C5). These are
# the configure-time analogue of the aggregate `validators` ctest, one row per
# (format, platform) validator, so `-R cmake_validators` selects a concrete, non-empty
# set (silent-pass rule; AGENTS.md).
#
# Registration is DEFERRED to end-of-top-level-scope via cmake_language(DEFER) because
# this module is include()d (by cmake/Formats.cmake) BEFORE enable_testing() runs in
# the root CMakeLists; the deferred calls fire after enable_testing(), so add_test()
# resolves. Guarded by MW101_TESTS (the only state in which enable_testing() runs).

# A small generated per-validator checker (kept here so the whole feature lives in
# cmake/Validators.cmake per the task-138 edit scope). It re-reads the wired-map
# report and asserts ONE validator's platform/option legality.
set(_mw_val_checker "${CMAKE_BINARY_DIR}/CheckOneValidator.cmake")
file(WRITE "${_mw_val_checker}" [=[
# SPDX-License-Identifier: GPL-3.0-or-later
# Generated by cmake/Validators.cmake (task 138). Run with:
#   -DREPORT=<mw_validators_report.txt> -DVALIDATOR=<name> -DFORMAT=<fmt>
# Asserts the named validator's wired flag respects the docs/design/09 §2.1 /
# ADR-024 per-platform + MW_BUILD_LV2 legality for the report's platform.
if(NOT DEFINED REPORT OR NOT DEFINED VALIDATOR OR NOT DEFINED FORMAT)
  message(FATAL_ERROR "cmake_validators: REPORT, VALIDATOR and FORMAT must be set.")
endif()
if(NOT EXISTS "${REPORT}")
  message(FATAL_ERROR "cmake_validators: report not found: ${REPORT} (configure first).")
endif()
file(READ "${REPORT}" _r)
string(REGEX MATCH "platform=([a-z]+)" _ _ "${_r}")
set(_platform "${CMAKE_MATCH_1}")
string(REGEX MATCH "mw_build_lv2=([A-Za-z0-9]*)" _ _ "${_r}")
set(_lv2opt "${CMAKE_MATCH_1}")
# Read this validator's wired flag (line "<name>=<ON|OFF>"). Escape the name for regex
# ('clap-validator', 'lv2_validate' contain regex-special chars only as literals here).
string(REGEX MATCH "(^|\n)${VALIDATOR}=([A-Za-z]+)" _ _ "${_r}")
set(_wired "${CMAKE_MATCH_2}")

set(_errors "")
# auval + Steinberg validator MUST NOT be wired off macOS [§2.1; F-3 spirit].
if(VALIDATOR STREQUAL "auval" OR VALIDATOR STREQUAL "validator")
  if(NOT _platform STREQUAL "macos" AND _wired STREQUAL "ON")
    list(APPEND _errors "${VALIDATOR} wired on non-macOS platform '${_platform}'")
  endif()
endif()
# lv2lint + lv2_validate MUST NOT be wired off Linux, nor with MW_BUILD_LV2 OFF [F-6].
if(VALIDATOR STREQUAL "lv2lint" OR VALIDATOR STREQUAL "lv2_validate")
  if(NOT _platform STREQUAL "linux" AND _wired STREQUAL "ON")
    list(APPEND _errors "${VALIDATOR} wired on non-Linux platform '${_platform}'")
  endif()
  if(NOT _lv2opt STREQUAL "ON" AND _wired STREQUAL "ON")
    list(APPEND _errors "${VALIDATOR} wired with MW_BUILD_LV2 not ON (=${_lv2opt})")
  endif()
endif()

if(_errors)
  string(REPLACE ";" "\n  " _e "${_errors}")
  message(FATAL_ERROR
    "cmake_validators(${FORMAT}/${VALIDATOR}) FAILED [docs/design/09 §2.1; ADR-011 C3-C6; ADR-024]:\n  ${_e}")
endif()
message(STATUS
  "cmake_validators(${FORMAT}/${VALIDATOR}): PASS — wired=${_wired} respects the §2.1 table on '${_platform}' (lv2_opt=${_lv2opt}).")
]=])

# _mw_register_validator_ctests() — runs after enable_testing(); adds one ctest per
# (format, platform) validator. Deferred so it sees an enabled test harness.
function(_mw_register_validator_ctests)
  foreach(_v ${MW_VALIDATOR_NAMES})
    set(_fmt "${MW_VALIDATOR_FORMAT_${_v}}")
    add_test(NAME "cmake_validators_${_fmt}_${_v}"
      COMMAND "${CMAKE_COMMAND}"
              "-DREPORT=${CMAKE_BINARY_DIR}/mw_validators_report.txt"
              "-DVALIDATOR=${_v}"
              "-DFORMAT=${_fmt}"
              -P "${CMAKE_BINARY_DIR}/CheckOneValidator.cmake")
  endforeach()
endfunction()

if(MW101_TESTS)
  cmake_language(DEFER CALL _mw_register_validator_ctests)
endif()
