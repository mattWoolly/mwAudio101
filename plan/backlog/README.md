<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Backlog

Atomic, agent-executable task files. **One file = one task = one branch = one PR.**

## File naming

`NNN-short-slug.md` — `NNN` is a zero-padded ordinal encoding *rough* dependency order. The
`status` frontmatter field is the source of truth for state, **not** the filename and **not** the
INDEX table. When review splits/inserts a task, use a letter suffix (`045` → `045` + `045b`) so
ordinals and `depends-on` references stay stable. Never renumber.

## Task file format

Frontmatter + six fixed sections in order (see `orchestration-notes/templates/task.template.md`):

```markdown
---
id: NNN
title: One-line task title
status: todo | in-progress | in-review | done | blocked
depends-on: [task ids that must be done first]
component: core | engine | app | ui | infra | qa | docs
estimated-size: S | M | L   # S:<200 LOC  M:<600 LOC  L: split before starting
---

## Objective         # outcome-framed: what EXISTS when done
## Context           # pure pointer list: files + numbered design §§ + ADRs to read first
## Scope             # precise bullets; name files/signatures
## Out of scope      # name the OTHER task id that owns each deferred item
## Acceptance criteria  # checkboxes; cite the design § each enforces
## Verification commands # exact, copy-pasteable, must be seen to pass before PR
```

`L` is a smell, not a size: it means *split before starting*.

## Status lifecycle

`todo → in-progress → in-review → done` (+ `blocked`). Realized on `main` as a three-commit
triplet per task: `Backlog: NNN — <reason>` (claim, spec-only diff, before the branch merges) →
`NNN: <title> (#PR)` (squash-merge) → `chore: mark NNN done` (flip status + mirror INDEX).
**Only the reviewer stamps `done`.** You never mark your own task done.

## Test-selection rules (silent-pass prevention — non-negotiable)

- Every `ctest … -R <word>` / `-L <label>` carries `--no-tests=error` — ctest exits 0 when a
  selector matches nothing, which is a silent pass.
- Test-case names must begin with the task's tag word (discovery registers names, not tags).
- Cross-cutting invariants (license-header/SPDX, no-alloc-on-audio-thread asserts) are baked into
  the same `ctest --preset default` run so every task inherits them.

## Canonical verification block

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R <tag> --no-tests=error
```

## Workflow per task

1. Claim: set `status: in-progress` (on your branch, not main).
2. Branch from latest main: `task/NNN-short-slug` (git worktree if working in parallel).
3. Implement within scope. TDD for core/algorithmic logic.
4. Run verification commands; all must pass.
5. Push, open PR titled `NNN: <title>` (body per `orchestration-notes/templates/pr-body.template.md`).
   Set `status: in-review`.
6. Reviewer agent reviews; merge squashes to main and sets `status: done`.

## Bugfix tasks (from QA/review)

Same template, but `## Context` cites the originating finding id + severity and lists
`Verified facts (read these first):` with exact `file:line` evidence; `## Out of scope` forbids
touching adjacent verified-correct subsystems. `depends-on` may be empty for independent fixes.
