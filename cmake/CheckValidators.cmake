# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/CheckValidators.cmake — `validators` ctest (task 095).
#
# Run with -DREPORT=<mw_validators_report.txt>. Asserts the configure-time
# validator wired-map matches the docs/design/09 §2.1 / ADR-024 Contract table for
# THIS platform: auval/Steinberg validator macOS-only; lv2lint/lv2_validate
# Linux-only; pluginval/clap-validator on any desktop. We assert PLATFORM-LEGALITY
# (a validator may legitimately be absent if the tool isn't installed), so the gate
# checks that a validator is NEVER reported wired on a platform where it cannot be.

if(NOT DEFINED REPORT)
  message(FATAL_ERROR "validators: REPORT must be set.")
endif()
if(NOT EXISTS "${REPORT}")
  message(FATAL_ERROR "validators: report not found: ${REPORT} (configure first).")
endif()

file(READ "${REPORT}" _r)
string(REGEX MATCH "platform=([a-z]+)" _ _ "${_r}")
set(_platform "${CMAKE_MATCH_1}")
message(STATUS "validators: platform=${_platform}")

macro(_get name out)
  string(REGEX MATCH "${name}=([A-Za-z]+)" _ _ "${_r}")
  set(${out} "${CMAKE_MATCH_1}")
endmacro()

_get(auval _auval)
_get(validator _validator)
_get(lv2lint _lv2lint)
_get(lv2_validate _lv2validate)

set(_errors "")

# auval + Steinberg validator MUST NOT be wired off macOS [§2.1 / F-3 spirit].
if(NOT _platform STREQUAL "macos")
  if(_auval STREQUAL "ON")
    list(APPEND _errors "auval wired on non-macOS platform '${_platform}'")
  endif()
  if(_validator STREQUAL "ON")
    list(APPEND _errors "Steinberg validator wired on non-macOS platform '${_platform}'")
  endif()
endif()

# lv2lint + lv2_validate MUST NOT be wired off Linux [§2.1].
if(NOT _platform STREQUAL "linux")
  if(_lv2lint STREQUAL "ON")
    list(APPEND _errors "lv2lint wired on non-Linux platform '${_platform}'")
  endif()
  if(_lv2validate STREQUAL "ON")
    list(APPEND _errors "lv2_validate wired on non-Linux platform '${_platform}'")
  endif()
endif()

if(_errors)
  string(REPLACE ";" "\n  " _e "${_errors}")
  message(FATAL_ERROR "validators FAILED [docs/design/09 §2.1; ADR-024]:\n  ${_e}")
endif()

message(STATUS "validators: PASS — wired-map respects the §2.1 per-platform table (platform=${_platform}).")
