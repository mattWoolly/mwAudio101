<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# mwAudio101 — project guide for agents

mwAudio101 is a circuit-accurate, GPLv3, cross-platform (JUCE/C++20) software synthesizer
inspired by the Roland SH-101, extended with modern essentials. Built by an AI agent fleet per
the `orchestration-notes/` playbook.

**Read these first, in order:**

1. `AGENTS.md` — the condensed operating rules (scope, trace-or-deviate, TDD, PR flow, QA).
2. `plan/ORCHESTRATION.md` — the owner-locked decisions table + the six-phase flow.
3. `docs/superpowers/specs/2026-06-17-mwaudio101-design.md` — the approved design spec.
4. Your task: `plan/backlog/NNN-*.md` (once Phase 3 is populated). Do **exactly** its scope.

**Single sources of truth:**
- Technical claims → `docs/research/` (cited) or an ADR deviation (`plan/decisions/`).
- Design contracts → numbered sections in `docs/design/`.
- Task state → the `status:` frontmatter of the task file (not the filename, not INDEX.md).
- Build/test commands → `docs/BUILDING.md` (local == CI, 1:1).

**Hard rules (see AGENTS.md for the full list):** branch + PR for all dev work; verify locally
with real pasted output before any PR; `--no-tests=error` on every ctest selector; never mark
your own task done; trademark distance is deliberate (no Roland marks / trade dress).

**Trademark.** "Roland" / "SH-101" are trademarks of Roland Corporation. mwAudio101 is an
independent, unaffiliated work that models documented circuit behavior.
