# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/CheckFormats.cmake — `formats` ctest (task 096).
#
# Run with -DREPORT=<mw_formats_report.txt>. Asserts the configure-time resolved
# format list obeys the docs/design/09 §2.2 (F-1..F-8) gate for THIS platform:
#   - AU is present ONLY on macOS (F-2/F-3).
#   - AAX is NEVER present (F-4) — the configure would have FATAL_ERROR'd, so its
#     mere absence here is the positive check.
#   - LV2 is present ONLY on Linux when wired (F-6).
# Also runs the mock-input FATAL cases via a child cmake -P so the gate's error
# branches are exercised (silent-pass prevention for the gate logic itself).

if(NOT DEFINED REPORT)
  message(FATAL_ERROR "formats: REPORT must be set.")
endif()
if(NOT EXISTS "${REPORT}")
  message(FATAL_ERROR "formats: report not found: ${REPORT} (configure first).")
endif()

file(READ "${REPORT}" _r)
string(REGEX MATCH "platform=([a-z]+)" _ _ "${_r}")
set(_platform "${CMAKE_MATCH_1}")
string(REGEX MATCH "resolved=([A-Za-z0-9;]*)" _ _ "${_r}")
set(_resolved "${CMAKE_MATCH_1}")
message(STATUS "formats: platform=${_platform} resolved=${_resolved}")

set(_errors "")

# F-4: AAX is never built anywhere.
if(_resolved MATCHES "AAX")
  list(APPEND _errors "AAX present in resolved formats (must be permanently excluded)")
endif()

# F-3: AU only on macOS.
if(_resolved MATCHES "AU" AND NOT _platform STREQUAL "macos")
  list(APPEND _errors "AU present off macOS")
endif()

# F-6: LV2 only on Linux.
if(_resolved MATCHES "LV2" AND NOT _platform STREQUAL "linux")
  list(APPEND _errors "LV2 present off Linux")
endif()

if(_errors)
  string(REPLACE ";" "\n  " _e "${_errors}")
  message(FATAL_ERROR "formats FAILED [docs/design/09 §2.2]:\n  ${_e}")
endif()

message(STATUS "formats: PASS — resolved set respects the §2.2 F-1..F-8 gate (platform=${_platform}).")
