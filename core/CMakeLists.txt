# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# core/CMakeLists.txt — the mwcore pure-C++20 static library (task 005).
# Realizes docs/design/00 §3.3, docs/design/11 §13.6, ADR-001 C1, ADR-014 C11.

add_library(mwcore STATIC
  Core.cpp)

target_include_directories(mwcore PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_features(mwcore PUBLIC cxx_std_20)

# FP discipline is structural, not copy-pasted: mwcore links the one INTERFACE
# target carrying the FROZEN flags [docs/design/11 §11; ADR-014 C4; ADR-001 C12].
target_link_libraries(mwcore PUBLIC mw_fp_discipline)

# --- no-JUCE-in-core guard, part 1: SOURCE scan (build-time, FAILS the build) ---
add_custom_target(mwcore_no_juce_guard ALL
  COMMAND ${CMAKE_COMMAND}
          -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
          -P ${CMAKE_SOURCE_DIR}/cmake/NoJuceInCore.cmake
  COMMENT "no-JUCE-in-core guard: scanning core/ sources for JUCE references"
  VERBATIM)
add_dependencies(mwcore mwcore_no_juce_guard)

# --- no-JUCE-in-core guard, part 2: LINK/INCLUDE closure assertion --------------
# mwcore's link interface MUST contain no JUCE target [ADR-001 C1; ADR-014 C11].
# Assert it at configure time over the resolved link libraries.
get_target_property(_mwcore_link_libs mwcore LINK_LIBRARIES)
get_target_property(_mwcore_iface_libs mwcore INTERFACE_LINK_LIBRARIES)
foreach(_lib ${_mwcore_link_libs} ${_mwcore_iface_libs})
  string(TOLOWER "${_lib}" _lib_lc)
  if(_lib_lc MATCHES "juce")
    message(FATAL_ERROR
      "no-JUCE-in-core guard FAILED: mwcore link interface contains a JUCE target "
      "'${_lib}' [ADR-001 C1; ADR-014 C11].")
  endif()
endforeach()
