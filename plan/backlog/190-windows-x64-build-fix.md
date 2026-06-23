<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 190
title: Fix the windows-x64 CI build+test (goal-tier "Windows a plus")
status: todo
depends-on: [156, 157]
component: infra
estimated-size: M
stream: ci
priority: goal-tier
---

## Objective

windows-x64 CI (continue-on-error, goal-tier) fails in the "Build and test (scripts/check.sh windows-x64)"
step — checkout + CPM cache succeed (the .gitattributes CRLF fix landed), so the failure is in the MSVC
build or a Windows-specific test. The original requirement is "Linux + Mac native, Windows a plus", so this
is goal-tier, not a shippability blocker. Diagnose + fix so windows-x64 goes green and can be promoted off
continue-on-error.

## Scope / notes
- Needs a Windows environment (or careful MSVC-difference analysis) — cannot be fully validated from the
  macOS dev box. Pull the full windows-x64 job log, root-cause (likely an MSVC-vs-Clang/GCC-ism, a
  path/`std::filesystem` difference, or a test that assumes POSIX), fix, and re-run the matrix.
- 130b activates MW_LINK_OPENGL=ON on Windows — ensure the juce_opengl link works in the MSVC build too.
- Keep local==CI; do not weaken any gate to make Windows pass.

## Out of scope
- Promoting windows off continue-on-error until it is reliably green.
