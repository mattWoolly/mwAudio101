# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/CheckPrefixCoverage.cmake — per-prefix discovery assertion (task 006).
#
# Run in script mode with -DTEST_BIN=<path> -DREQUIRED_TAGS="cal;prng;...".
# Realizes docs/design/11 §8.2 / ADR-013 C2: each required subsystem tag MUST have
# >= 1 discovered test, else this FAILS (silent-pass prevention — a deleted suite
# shows as a hard failure, not silence).
#
# FOUNDATION SCOPE: the full required set is `vco vcf vca env seq prng arp cal`
# [docs/design/11 §8.2]. The foundation only implements the `cal` and `prng`
# subsystems; the remaining tags are enforced by this same gate as those subsystems
# land (the parent passes REQUIRED_TAGS, so widening it is a one-line change). See
# tests/CMakeLists.txt for the current set + TODO(task-006).

if(NOT DEFINED TEST_BIN OR NOT DEFINED REQUIRED_TAGS)
  message(FATAL_ERROR "CheckPrefixCoverage: TEST_BIN and REQUIRED_TAGS must be set.")
endif()

set(_failures "")
foreach(_tag ${REQUIRED_TAGS})
  # Ask Catch2 to list tests matching the [tag]; count the reported total.
  execute_process(
    COMMAND "${TEST_BIN}" "[${_tag}]" --list-tests
    OUTPUT_VARIABLE _out
    RESULT_VARIABLE _rc)
  # Catch2 prints "N matching test cases" (or "1 matching test case").
  string(REGEX MATCH "([0-9]+) matching test case" _m "${_out}")
  set(_count "${CMAKE_MATCH_1}")
  if(NOT _count OR _count EQUAL 0)
    list(APPEND _failures "${_tag}")
  else()
    message(STATUS "prefix-coverage: tag [${_tag}] has ${_count} discovered test(s).")
  endif()
endforeach()

if(_failures)
  string(REPLACE ";" " " _f "${_failures}")
  message(FATAL_ERROR
    "prefix-coverage FAILED: required subsystem tag(s) with 0 discovered tests: ${_f} "
    "[docs/design/11 §8.2; ADR-013 C2].")
endif()

message(STATUS "prefix-coverage: PASS — every required tag has >= 1 discovered test.")
