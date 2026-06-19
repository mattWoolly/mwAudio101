# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# Top-level CMakeLists.txt — mwAudio101 (task 001).
# Realizes docs/design/11 §9.1 / §2.1 and ADR-014 Decision (Tree/CMake structure).

# CMake floor: schema-v6 presets need >= 3.25; configure FAILS below it with a clear
# message [docs/design/11 §9.1; ADR-014 C3]. Documented in docs/BUILDING.md.
cmake_minimum_required(VERSION 3.25)

project(mwAudio101
  VERSION 1.0.0
  DESCRIPTION "Circuit-accurate SH-101-inspired software synthesizer"
  LANGUAGES CXX)

# JUCE on Apple platforms compiles C and Objective-C/Objective-C++ translation units
# (CoreAudio/CoreMIDI/Cocoa glue), and C generally on every platform. The headless
# core/test build stays pure C++ (LANGUAGES CXX above); we only enable the extra
# languages when the JUCE shell is requested, so a core-only configure never pays for
# them [docs/design/11 §9.1; ADR-014]. This MUST precede the cmake/* includes and the
# add_subdirectory(plugin) so the JUCE targets have a C/ObjC compiler available.
if(MW_BUILD_PLUGIN)
  enable_language(C)
  if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
  endif()
endif()

# --- Language standard: C++20, extensions OFF (docs/design/11 §9.1) -----------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json for the fp_discipline_guard grep [docs/design/11 §13.4].
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Build options (wired through presets, never bare cache edits) ------------
option(MW101_TESTS    "Build the headless Catch2 test binary"            ON)
option(MW_BUILD_PLUGIN "Build the JUCE plugin shell (fetches JUCE — large)" OFF)
option(MW_BUILD_CLAP  "Build the CLAP format (needs MW_BUILD_PLUGIN)"     OFF)
option(MW_BUILD_LV2   "Build the LV2 format, Linux goal-tier (needs MW_BUILD_PLUGIN)" OFF)
# Sanitizer toggles are flipped by the asan/ubsan/tsan presets, not edited by hand.
option(MW_ENABLE_ASAN  "AddressSanitizer"            OFF)
option(MW_ENABLE_UBSAN "UndefinedBehaviorSanitizer"  OFF)
option(MW_ENABLE_TSAN  "ThreadSanitizer"             OFF)

if((MW_BUILD_CLAP OR MW_BUILD_LV2) AND NOT MW_BUILD_PLUGIN)
  message(WARNING
    "mwAudio101: MW_BUILD_CLAP/MW_BUILD_LV2 imply MW_BUILD_PLUGIN; they are inert with the plugin off.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- FP discipline INTERFACE target (must exist before core links it) ---------
include(cmake/CompilerFlags.cmake)   # defines mw_fp_discipline (task 004)

# --- Sanitizers (applied globally when a sanitizer preset enables one) --------
if(MW_ENABLE_ASAN OR MW_ENABLE_UBSAN OR MW_ENABLE_TSAN)
  set(_mw_san "")
  if(MW_ENABLE_ASAN)
    list(APPEND _mw_san address)
  endif()
  if(MW_ENABLE_UBSAN)
    list(APPEND _mw_san undefined)
  endif()
  if(MW_ENABLE_TSAN)
    list(APPEND _mw_san thread)
  endif()
  list(JOIN _mw_san "," _mw_san_csv)
  add_compile_options(-fsanitize=${_mw_san_csv} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${_mw_san_csv})
endif()

# --- Dependencies (CPM bootstrap + pins; Catch2 always, JUCE/clap gated) ------
include(cmake/Dependencies.cmake)    # tasks 003

# --- Configure-time format gate (resolves formats; adds no target) ------------
include(cmake/Formats.cmake)         # tasks 095/096 (pulls in Validators.cmake)

# --- Subdirectory wiring per the §2.1 tree ------------------------------------
add_subdirectory(core)               # mwcore static lib (tasks 005, 005b, 007-009, 014-017)

if(MW_BUILD_PLUGIN)
  add_subdirectory(plugin)           # JUCE shell — only when explicitly requested
endif()

if(MW101_TESTS)
  enable_testing()
  add_subdirectory(tests)            # Catch2 headless binary (task 006 + units + guards)
endif()
