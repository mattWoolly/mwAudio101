# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/Dependencies.cmake — the single full-SHA dependency pin manifest.
#
# Realizes docs/design/11 §10 and ADR-014 C2/C10. Every dependency is pinned to a
# FULL 40-char commit SHA (never a branch, floating tag, or `main`); each pin
# carries an inline comment recording version + SHA + date-pinned + why + SPDX
# license, so this one file is simultaneously the reproducibility manifest and the
# GPLv3 license-provenance audit trail [ADR-014 Decision].
#
# Disk/network discipline (this project's constraint): JUCE and clap-juce-extensions
# are LARGE and are only `CPMAddPackage`d when MW_BUILD_PLUGIN is ON. Their pins are
# DECLARED here unconditionally (provenance is always auditable) but fetched lazily.
# Catch2 (small) is always fetched so the headless mwcore test binary links against a
# vendored, bit-reproducible Catch2 [ADR-014 Decision; ADR-001 C13].

# --- CPM bootstrap, integrity-checked (ADR-014 C2) ----------------------------
# The committed cmake/CPM.cmake is the vendored bootstrap (CPM.cmake v0.42.3, MIT).
# We verify its SHA-256 before `include()` so a tampered/corrupt bootstrap fails the
# configure rather than silently changing the dependency graph [docs/design/11 §10].
set(MW_CPM_VERSION "0.42.3")
set(MW_CPM_EXPECTED_HASH "a609e875fd532b067174250f6abbc3dac22fe2d64869783fb1e80bda1625c844")

set(_mw_cpm_path "${CMAKE_CURRENT_LIST_DIR}/CPM.cmake")
file(SHA256 "${_mw_cpm_path}" _mw_cpm_actual_hash)
if(NOT _mw_cpm_actual_hash STREQUAL MW_CPM_EXPECTED_HASH)
  message(FATAL_ERROR
    "CPM bootstrap integrity check FAILED.\n"
    "  file:     ${_mw_cpm_path}\n"
    "  expected: ${MW_CPM_EXPECTED_HASH}\n"
    "  actual:   ${_mw_cpm_actual_hash}\n"
    "Re-pin CPM (version + SHA-256) in cmake/Dependencies.cmake in one reviewed PR.")
endif()

# Honor a shared source cache so sources are fetched once and reused (and so a
# no-network build can succeed) [ADR-014 C10; docs/design/11 §10].
if(NOT DEFINED CPM_SOURCE_CACHE AND DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_SOURCE_CACHE "$ENV{CPM_SOURCE_CACHE}")
endif()
if(NOT DEFINED CPM_SOURCE_CACHE)
  set(CPM_SOURCE_CACHE "$ENV{HOME}/.cache/CPM")
endif()
message(STATUS "mwAudio101: CPM_SOURCE_CACHE = ${CPM_SOURCE_CACHE}")

include("${_mw_cpm_path}")

# --- Catch2 — ALWAYS fetched (small; vendored, not system) --------------------
# Catch2 v3.7.1 — SHA fa43b77429ba76c462b1898d6cd2f2d7a9416b14 — pinned 2026-06-18
# why: headless unit/golden test framework; vendored (not find_package) so test
#      results are bit-reproducible across machines [ADR-014 C2; docs/design/11 §10].
# SPDX: BSL-1.0
CPMAddPackage(
  NAME Catch2
  GITHUB_REPOSITORY catchorg/Catch2
  GIT_TAG fa43b77429ba76c462b1898d6cd2f2d7a9416b14   # v3.7.1
  VERSION 3.7.1
)

# --- JUCE — DECLARED here; fetched only when MW_BUILD_PLUGIN is ON ------------
# JUCE 8.0.4 — SHA 51d11a2be6d5c97ccf12b4e5e827006e19f0555a — pinned 2026-06-18
# why: cross-platform plugin host shell (VST3/AU/CLAP/Standalone/LV2). LARGE, so
#      gated behind MW_BUILD_PLUGIN to keep core/test builds disk- and network-light.
# SPDX: GPL-3.0-or-later (free tier; binaries distributable/sellable per the project LICENSE)
set(MW_JUCE_GIT_TAG "51d11a2be6d5c97ccf12b4e5e827006e19f0555a")  # JUCE 8.0.4

# --- clap-juce-extensions + CLAP — DECLARED; fetched only with MW_BUILD_PLUGIN+CLAP
# clap-juce-extensions — SHA 51a9359315298de632cf44e9d7524940868441e6 — pinned 2026-06-18
# why: wraps the shared JUCE AudioProcessor as a CLAP plugin (no semver releases
#      exist, so a SHA pin is mandatory) [ADR-014 C2]. Gated behind MW_BUILD_CLAP.
# SPDX: MIT (clap-juce-extensions); CLAP itself transitively SHA-pinned, MIT.
# NB: the CLAP SDK + clap-helpers ride in as git submodules (clap-libs/clap,
#     clap-libs/clap-helpers), so the fetch MUST recurse submodules or the
#     add_subdirectory(clap-libs/clap) in its CMakeLists fails to configure
#     [free-audio/clap-juce-extensions .gitmodules @ this SHA; ADR-024 C2 — CLAP
#     transitively SHA-pinned, no fourth top-level manifest entry].
set(MW_CLAP_JUCE_EXT_GIT_TAG "51a9359315298de632cf44e9d7524940868441e6")

if(MW_BUILD_PLUGIN)
  message(STATUS "mwAudio101: MW_BUILD_PLUGIN=ON — fetching JUCE (this is large)")
  CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG ${MW_JUCE_GIT_TAG}                          # JUCE 8.0.4
    VERSION 8.0.4
  )
  if(MW_BUILD_CLAP)
    message(STATUS "mwAudio101: MW_BUILD_CLAP=ON — fetching clap-juce-extensions (recursing CLAP submodules)")
    CPMAddPackage(
      NAME clap-juce-extensions
      GITHUB_REPOSITORY free-audio/clap-juce-extensions
      GIT_TAG ${MW_CLAP_JUCE_EXT_GIT_TAG}
      GIT_SUBMODULES_RECURSE TRUE   # pulls clap-libs/clap + clap-libs/clap-helpers
      # Do NOT build the bundled example plugins (they CPM a second JUCE copy).
      OPTIONS "CLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES OFF"
    )
  endif()
else()
  message(STATUS "mwAudio101: MW_BUILD_PLUGIN=OFF — JUCE/clap NOT fetched (core/test build is JUCE-free)")
endif()
