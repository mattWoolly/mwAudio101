# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/NoJuceInCore.cmake — the no-JUCE-in-core SOURCE guard (task 005).
#
# Run in script mode (cmake -P) with -DCORE_DIR=<path>. Scans every core/ source
# file for a JUCE include (<juce_...>) or a JUCE_ token and FAILS (fatal) on any
# hit, re-affirming ADR-001 C1 / ADR-014 C11 / docs/design/11 §13.6 mechanically.
# The link/include-closure half of the guard is enforced in core/CMakeLists.txt by
# asserting mwcore's link interface contains no JUCE target.

if(NOT DEFINED CORE_DIR)
  message(FATAL_ERROR "NoJuceInCore: CORE_DIR must be defined.")
endif()

file(GLOB_RECURSE _core_sources
  "${CORE_DIR}/*.h" "${CORE_DIR}/*.hpp" "${CORE_DIR}/*.cpp" "${CORE_DIR}/*.cc")

set(_violations "")
foreach(_f ${_core_sources})
  file(READ "${_f}" _contents)
  # Match a real JUCE include or a JUCE_ macro/token. The literal strings "<juce_"
  # and "JUCE_" appearing in THIS guard's own comments are not in core/, so they
  # are never scanned.
  if(_contents MATCHES "#[ \t]*include[ \t]*[<\"]juce_" OR _contents MATCHES "JUCE_")
    list(APPEND _violations "${_f}")
  endif()
endforeach()

if(_violations)
  string(REPLACE ";" "\n  " _v "${_violations}")
  message(FATAL_ERROR
    "no-JUCE-in-core guard FAILED: the following core/ source(s) reference JUCE "
    "[ADR-001 C1; ADR-014 C11; docs/design/11 §13.6]:\n  ${_v}")
endif()

message(STATUS "no-JUCE-in-core guard: PASS (${_core_sources} scanned, zero JUCE references).")
