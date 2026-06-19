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

# --- Source discovery: CONFIGURE_DEPENDS glob (parallel-fleet enabler) ----------
# Unit/integration test sources are discovered by glob, NOT listed explicitly, so
# the parallel development fleet can add tests/unit/**/XTest.cpp without serializing
# on this file (no CMakeLists merge conflicts). CONFIGURE_DEPENDS re-runs the glob
# when the file set changes; still re-run `cmake --preset default` after adding a
# file (see docs/BUILDING.md). The standalone invariant TOOLS below
# (LicenseHeaderCheck, FpDisciplineCheck) are NOT unit tests and stay explicit; the
# RT sentinel AudioThreadGuard*.cpp is part of mw101_tests and is globbed by name.
file(GLOB_RECURSE MW_TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/invariants/AudioThreadGuard*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp")

# Empty-list guard: until any test .cpp lands, fall back to the discovery self-test
# so configure never fails on a freshly-globbed tree. HarnessSelfTest.cpp is always
# present today, so this is belt-and-suspenders for transient states.
if(NOT MW_TEST_SOURCES)
  set(MW_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/unit/HarnessSelfTest.cpp")
endif()

# --- The headless unit/golden test binary: links mwcore ONLY (no JUCE) ----------
add_executable(mw101_tests ${MW_TEST_SOURCES})

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

# === JUCE-linked plugin test harness (mw101_plugin_tests) ========================
# A SECOND Catch2 target that — unlike mw101_tests — links the real JUCE modules so
# future JUCE-phase tasks can unit-test plugin-side code (the AudioProcessor seam,
# the JUCE-MIDI translation, APVTS state) headlessly. It is built ONLY when
# MW_BUILD_PLUGIN is ON, so the core/default cycle (which never fetches JUCE) is
# completely untouched: with the plugin off this whole block is skipped.
#
# We use JUCE's juce_add_console_app() helper (available once CPMAddPackage(JUCE)
# has run) rather than a bare add_executable so the JUCE module compile environment
# (the JUCE_* defines, module include dirs, AppConfig machinery) is set up correctly
# for the linked juce_* modules — the same way juce_add_plugin sets it up for the
# shell. The processor-under-test is compiled in by adding plugin/PluginProcessor.cpp
# directly to this target's sources.
if(MW_BUILD_PLUGIN)
  if(NOT TARGET juce::juce_audio_processors)
    message(FATAL_ERROR
      "tests/: MW_BUILD_PLUGIN is ON but JUCE was not fetched. Check cmake/Dependencies.cmake.")
  endif()

  # Source discovery for the plugin test suite mirrors the tests/unit glob: future
  # JUCE tasks just drop tests/plugin/*Test.cpp and re-run configure — no edit here.
  file(GLOB_RECURSE MW_PLUGIN_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin/*.cpp")

  # Empty-list guard so a freshly-globbed tree never fails configure (belt-and-
  # suspenders; PluginHarnessTest.cpp is always present today).
  if(NOT MW_PLUGIN_TEST_SOURCES)
    set(MW_PLUGIN_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/plugin/PluginHarnessTest.cpp")
  endif()

  juce_add_console_app(mw101_plugin_tests
    PRODUCT_NAME "mw101_plugin_tests")

  # The plugin test sources + the processor-under-test (compiled directly into the
  # test binary so the seam is exercised against the real JUCE compile environment).
  target_sources(mw101_plugin_tests PRIVATE
    ${MW_PLUGIN_TEST_SOURCES}
    "${CMAKE_SOURCE_DIR}/plugin/PluginProcessor.cpp")

  # plugin/ for PluginProcessor.h; core/ for the JUCE-free seam headers it includes.
  target_include_directories(mw101_plugin_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/plugin"
    "${CMAKE_SOURCE_DIR}/core")

  target_compile_features(mw101_plugin_tests PRIVATE cxx_std_20)

  # JUCE compile-time config. Headless console run: no curl, no web browser; allow
  # JUCE's modal-loop guard to pass in a non-message-thread context. These mirror the
  # licit GPL-tier flags the plugin shell sets (plugin/CMakeLists.txt).
  target_compile_definitions(mw101_plugin_tests PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_REPORT_APP_USAGE=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

  # Link Catch2-with-main + the JUCE-free core/fp-discipline + the JUCE modules the
  # processor needs (matching plugin/CMakeLists.txt's link set).
  target_link_libraries(mw101_plugin_tests PRIVATE
    Catch2::Catch2WithMain
    mwcore
    mw_fp_discipline
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

  # ctest discovery under the silent-pass rules: test-case names begin with the
  # selector word `plugin_harness` so `-R plugin_harness` selects what we expect, and
  # the FAIL_REGULAR_EXPRESSION turns a "No tests ran" Catch2 result red. The
  # ctest-level `--no-tests=error` (passed on the verify `ctest -R` line, mirrored by
  # the testPresets noTestsAction in CMakePresets.json) makes a selector matching
  # nothing a hard failure — that flag is a ctest flag, not a Catch2 EXTRA_ARG.
  catch_discover_tests(mw101_plugin_tests
    TEST_PREFIX "mw101_plugin."
    PROPERTIES FAIL_REGULAR_EXPRESSION "No tests ran")
endif()
