# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# tests/CMakeLists.txt — the headless Catch2 binary + ctest registration + the
# cross-cutting invariant gates (task 006 + 010/011/012 + 095/096).
# Realizes docs/design/11 §8 / §13 and ADR-013 / ADR-014.

# Catch2 + its CMake helpers (catch_discover_tests) come from the CPM package.
list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)
include(CTest)

# --- The headless unit/golden test binary: links mwcore ONLY (no JUCE) ----------
add_executable(mw101_tests
  # Layer-1 units (core + params + state + version)
  unit/CalibrationTest.cpp
  unit/BlockContextTest.cpp
  unit/SmootherTest.cpp
  unit/PrngTest.cpp
  unit/SmoothingClassTest.cpp
  unit/ParamIDsTest.cpp
  unit/StateTreeTest.cpp
  unit/EngineVersionTest.cpp
  # Plugin-side PODs (pure C++, no JUCE — compiled here)
  unit/CapabilitiesTest.cpp
  unit/HostEventTest.cpp
  # Cross-cutting RT sentinel + its self-tests
  invariants/AudioThreadGuard.cpp
  invariants/AudioThreadGuardTest.cpp
  # Discovery wiring self-test
  unit/HarnessSelfTest.cpp)

target_link_libraries(mw101_tests PRIVATE
  mwcore                       # the JUCE-free core [ADR-001 C13]
  mw_fp_discipline             # golden-compare math matches shipped core [ADR-014 C4]
  Catch2::Catch2WithMain)

# The plugin/host PODs (Capabilities.h / HostEvent.h) are pure C++ headers; expose
# the plugin/ root so the tests can include them without JUCE.
target_include_directories(mw101_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/plugin")

target_compile_features(mw101_tests PRIVATE cxx_std_20)

# --- ctest discovery under the silent-pass rules (docs/design/11 §8.1) ----------
catch_discover_tests(mw101_tests
  TEST_PREFIX "mw101."
  PROPERTIES FAIL_REGULAR_EXPRESSION "No tests ran")

# --- Per-prefix discovery assertion (docs/design/11 §8.2; ADR-013 C2) -----------
# Full required set is `vco vcf vca env seq prng arp cal`; the foundation implements
# the cal + prng subsystems. Widen REQUIRED_TAGS as those subsystems land.
# TODO(task-006): extend to vco;vcf;vca;env;seq;arp once those tests exist.
add_test(NAME prefix_coverage
  COMMAND ${CMAKE_COMMAND}
          -DTEST_BIN=$<TARGET_FILE:mw101_tests>
          -DREQUIRED_TAGS=cal\;prng
          -P ${CMAKE_SOURCE_DIR}/cmake/CheckPrefixCoverage.cmake)

# --- License-header SPDX gate (task 011; docs/design/11 §13.2; ADR-013 C18) ------
add_executable(LicenseHeaderCheck invariants/LicenseHeaderCheck.cpp)
target_compile_features(LicenseHeaderCheck PRIVATE cxx_std_20)
add_test(NAME license_headers
  COMMAND LicenseHeaderCheck "${CMAKE_SOURCE_DIR}")

# --- ctest-labels snapshot diff (task 011; docs/design/11 §8.4; ADR-013 C3) ------
# A deleted/renamed labelled suite shows as a failing diff, not silence. The
# checked-in snapshot lists the subsystem labels currently present.
add_test(NAME labels_snapshot
  COMMAND ${CMAKE_COMMAND}
          -DSNAPSHOT=${CMAKE_SOURCE_DIR}/tests/golden/corpus/ctest-labels.snapshot
          -DTEST_BIN=$<TARGET_FILE:mw101_tests>
          -P ${CMAKE_SOURCE_DIR}/cmake/CheckLabelsSnapshot.cmake)

# --- fp_discipline_guard (task 012; docs/design/11 §13.4; ADR-014 C5) ------------
add_executable(FpDisciplineCheck invariants/FpDisciplineCheck.cpp)
target_compile_features(FpDisciplineCheck PRIVATE cxx_std_20)
add_test(NAME fp_discipline_guard
  COMMAND FpDisciplineCheck "${CMAKE_BINARY_DIR}/compile_commands.json")

# --- AudioThreadGuard sentinel: a registered ctest whose name begins with the -----
# selector word `audiothreadguard`, arming the global new/lock detector (task 010).
# The detailed behavior is covered by the [rt] Catch2 cases inside mw101_tests; this
# add_test gives the `-R audiothreadguard` selector a concrete, non-empty target so
# the silent-pass rule holds.
# Catch2 v3 itself returns non-zero when its filter matches no test cases, so this
# add_test cannot silently pass empty (it is the Catch2-level analogue of
# --no-tests=error). The ctest-level --no-tests=error applies to `-R` selectors.
add_test(NAME audiothreadguard_sentinel
  COMMAND mw101_tests "[rt]")

# --- Configure-time gate ctests (tasks 095/096) ---------------------------------
add_test(NAME validators
  COMMAND ${CMAKE_COMMAND}
          -DREPORT=${CMAKE_BINARY_DIR}/mw_validators_report.txt
          -P ${CMAKE_SOURCE_DIR}/cmake/CheckValidators.cmake)
add_test(NAME formats
  COMMAND ${CMAKE_COMMAND}
          -DREPORT=${CMAKE_BINARY_DIR}/mw_formats_report.txt
          -P ${CMAKE_SOURCE_DIR}/cmake/CheckFormats.cmake)

# A label so the `build` selector used by tasks 001/002/003/013 verification
# commands has a concrete, non-empty target (silent-pass rule). It runs the full
# binary and is a fast smoke that the build produced a working test artifact.
add_test(NAME build_smoke
  COMMAND mw101_tests "[harness]")
