# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/CheckLabelsSnapshot.cmake — label-snapshot diff (task 011).
#
# Run with -DSNAPSHOT=<file> -DTEST_BIN=<path>. Realizes docs/design/11 §8.4 /
# ADR-013 C3: a checked-in snapshot of the subsystem tags is diffed against the
# binary's current tags; a deleted/renamed labelled suite shows as a FAILING diff,
# not silence. Updating the snapshot is a reviewed change.

if(NOT DEFINED SNAPSHOT OR NOT DEFINED TEST_BIN)
  message(FATAL_ERROR "labels_snapshot: SNAPSHOT and TEST_BIN must be set.")
endif()
if(NOT EXISTS "${SNAPSHOT}")
  message(FATAL_ERROR "labels_snapshot: committed snapshot missing: ${SNAPSHOT}")
endif()

# Catch2 --list-tags prints lines like "  3  [cal]". Extract the bracketed tags,
# sort + dedupe to a canonical newline-separated list.
execute_process(COMMAND "${TEST_BIN}" --list-tags OUTPUT_VARIABLE _out RESULT_VARIABLE _rc)
string(REGEX MATCHALL "\\[[A-Za-z0-9_-]+\\]" _tags "${_out}")
list(REMOVE_DUPLICATES _tags)
list(SORT _tags)
string(REPLACE ";" "\n" _current "${_tags}")
string(STRIP "${_current}" _current)

file(READ "${SNAPSHOT}" _committed)
# Drop the SPDX/comment header lines (those starting with '#') and blank lines.
string(REGEX REPLACE "#[^\n]*\n" "" _committed "${_committed}")
string(STRIP "${_committed}" _committed)

if(NOT _current STREQUAL _committed)
  message(FATAL_ERROR
    "labels_snapshot FAILED [docs/design/11 §8.4; ADR-013 C3]:\n"
    "--- committed (tests/golden/corpus/ctest-labels.snapshot) ---\n${_committed}\n"
    "--- current (from ${TEST_BIN} --list-tags) ---\n${_current}\n"
    "If this change is intended, update the snapshot in a reviewed diff.")
endif()

message(STATUS "labels_snapshot: PASS — tags match the committed snapshot.")
