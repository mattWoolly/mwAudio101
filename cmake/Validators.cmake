# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/Validators.cmake — per-format validator target locator (task 095).
#
# Realizes docs/design/09 §2.1/§2.2 and ADR-011/ADR-024. Locates or declares each
# per-format validator and sets a cached WIRED flag, then exposes a queryable
# validator->wired-on-this-platform map for cmake/Formats.cmake to consume. NO
# format/target logic lives here (that is cmake/Formats.cmake). No validator is
# INVOKED here — this is configure-time discovery only.
#
# Per §2.1: auval + Steinberg `validator` on macOS; pluginval + clap-validator on
# macOS/Linux/Windows; lv2lint + lv2_validate on Linux only.

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
function(mw_validator_wired name out_var)
  set(${out_var} "${MW_VALIDATOR_${name}_WIRED}" PARENT_SCOPE)
endfunction()

# _mw_declare_validator(<name> <available-on-this-platform-bool>)
#   When available-on-platform is true, find_program the tool; WIRED iff found.
#   When false (e.g. auval off macOS), WIRED is forced OFF unconditionally.
function(_mw_declare_validator name available_on_platform)
  set(_wired OFF)
  if(available_on_platform)
    # Allow injection for the unit test harness (mock inputs) without requiring the
    # tool to be installed on the dev box.
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
_mw_declare_validator(standalone-smoke ${_any_desktop})   # headless smoke launch; always "wired" as a step
# standalone-smoke is a project-provided step, not an external tool: wire it on
# any desktop platform.
set(MW_VALIDATOR_standalone-smoke_WIRED "${_any_desktop}" CACHE INTERNAL "" FORCE)

# auval + Steinberg validator: macOS only.
_mw_declare_validator(auval     ${_is_macos} auval)
_mw_declare_validator(validator ${_is_macos} validator)   # Steinberg VST3 validator

# lv2lint + lv2_validate: Linux only.
_mw_declare_validator(lv2lint      ${_is_linux} lv2lint)
_mw_declare_validator(lv2_validate ${_is_linux} lv2_validate)

# Emit a machine-readable report the validators ctest diffs against.
set(_report "${CMAKE_BINARY_DIR}/mw_validators_report.txt")
set(_report_lines "platform=${MW_HOST_PLATFORM}\n")
foreach(_v pluginval clap-validator standalone-smoke auval validator lv2lint lv2_validate)
  string(APPEND _report_lines "${_v}=${MW_VALIDATOR_${_v}_WIRED}\n")
endforeach()
file(WRITE "${_report}" "${_report_lines}")
message(STATUS "mwAudio101: validator wired-map written to ${_report} (platform=${MW_HOST_PLATFORM})")
