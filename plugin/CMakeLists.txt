# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# plugin/CMakeLists.txt — JUCE shell placeholder (gated on MW_BUILD_PLUGIN).
#
# FOUNDATION SCOPE: this subdirectory is only added when MW_BUILD_PLUGIN is ON
# (top-level CMakeLists). The full juce_add_plugin target over the shared
# AudioProcessor is task 113 (single-juce-add-plugin-target). The foundation keeps
# the plugin/host/*.h PODs (Capabilities.h, HostEvent.h — tasks 098/099) here; they
# are pure C++ and are compiled/tested by the headless test binary, not by JUCE.
#
# TODO(task-113): define the juce_add_plugin target (links mwcore + mw_fp_discipline,
#                 formats from MW_RESOLVED_FORMATS) here.

if(NOT MW_BUILD_PLUGIN)
  return()
endif()

if(NOT TARGET juce::juce_audio_processors)
  message(FATAL_ERROR
    "plugin/: MW_BUILD_PLUGIN is ON but JUCE was not fetched. Check cmake/Dependencies.cmake.")
endif()

# (Intentionally empty target wiring in the foundation — see TODO(task-113).)
message(STATUS "mwAudio101: plugin/ placeholder active (juce_add_plugin target deferred to task 113).")
