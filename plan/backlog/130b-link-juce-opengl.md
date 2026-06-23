<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 130b
title: Link juce_opengl into the plugin target so the OpenGL opt-in hatch engages a real GL context
status: done
depends-on: [113, 130]
component: infra
estimated-size: S
stream: plugin
tag: wrappers
---

## Objective

Task 130 implemented the OpenGL opt-in lifecycle/persistence via ui/RenderBackend.h, which resolves
to the REAL juce::OpenGLContext only when JUCE_MODULE_AVAILABLE_juce_opengl is set — but the plugin
target does NOT link juce_opengl (130 was forbidden from editing plugin/CMakeLists.txt), so on the
current build the hatch is a behaviour-identical software shim (no real GPU). Link juce_opengl so the
opt-in actually engages a GL context. LOW priority: OpenGL is OFF by default and software render is
primary (ADR-015 C9), so this is a 'plus', not a blocker.

## Scope

- plugin/CMakeLists.txt: link juce::juce_opengl into the mwAudio101 target (and mw101_plugin_tests if
  the RenderBackend GL path is to be exercised), guarded so the Linux x64 hard gate stays GPU-safe per
  ADR-015 C9 (e.g. behind an option defaulting appropriately per platform). RenderBackend.h then
  compiles against the real juce::OpenGLContext with NO source change (it is already forward-compatible).
- Confirm the existing ui_opengl tests still pass with the real context linked (attach/detach lifecycle).

## Out of scope

- The RenderBackend lifecycle/persistence (130, done); any UI change.

## Acceptance criteria

- [ ] juce_opengl is linked into the plugin target (platform-guarded per ADR-015 C9); RenderBackend resolves to the real juce::OpenGLContext
- [ ] ui_opengl tests pass with the real context; software render remains the default (OFF-by-default)
- [ ] No regression to the Standalone build
