<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 130
title: OpenGL opt-in escape hatch (OFF by default)
status: in-review
depends-on: [020, 006, 114]
component: ui
estimated-size: S
stream: ui
tag: ui_opengl
---

## Objective

Add a juce::OpenGLContext member to the editor that is unattached by default and attaches only on an explicit advanced setting, keeping the software path primary and the Linux x64 hard gate GPU-free.

## Context

- `docs/design/10-ui.md §11` — read first
- `ADR-015 C9` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_opengl`.

## Scope

- OpenGLContext member in MwAudioEditor, not attached in constructor
- Attach only when an explicit user/advanced setting requests it (persisted in <extras>)
- Software render path remains primary and default
- Detach cleanly on editor teardown

## Out of scope

- software render correctness (ui-5..ui-16)
- CI hard-gate config (infra/build streams)

## Acceptance criteria

- [ ] No OpenGLContext is attached by default; software path is used (§11, ADR-015 C9)
- [ ] Context attaches only on the explicit advanced setting and detaches cleanly (§11)
- [ ] Linux x64 hard gate does not require OpenGL (§11, ADR-015 C9)
- [ ] Tests named ui_opengl* assert default-OFF and opt-in attach/detach; verify is 'ctest --preset default -R ui_opengl --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_opengl --no-tests=error
```
