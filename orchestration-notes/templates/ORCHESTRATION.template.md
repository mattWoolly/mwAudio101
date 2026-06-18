<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# <Project> — Orchestration Plan

**Project:** <one-paragraph description of what is being built and for whom>.

## Decisions locked with the project owner (YYYY-MM-DD)

> This table sits ABOVE the ADRs. An ADR may record tension with a locked item but must never
> silently reverse it — re-affirm the lock or flag the owner for ratification.

| Decision | Choice |
|---|---|
| Product type | <locked choice + one-line rationale> |
| Framework / language | <…> |
| License | <…> |
| Target platforms | <reference platform; co-required; goal platforms> |
| Budget / depth | <e.g. deep research + agent fleet + adversarial QA> |
| Repo workflow | Research/plan/ADRs → main directly. Dev tasks → branch + PR + agent review → merge. |
| CI timing | Added LAST (it slows local iteration); local build/test until then. |
| Out of scope (now) | <deferred features, each with the ADR that defers it> |

## Phases

1. **Research** (`docs/research/`) — deep-research workflow, adversarially verified + cited.
   Cache runnable references in a gitignored `research-cache/`.
2. **Architecture** (`docs/design/` + `plan/decisions/`) — an agent panel (2–4 personas)
   proposes competing designs; the team recommendation wins; ADRs capture each position +
   critique. Number every design-doc section as a permanent citation address space.
3. **Backlog** (`plan/backlog/`) — atomic task files, each independently executable by one
   agent with minimal context. See `plan/backlog/README.md` for the task format.
4. **Development** — agents pull backlog tasks in wave order. Each task: branch → implement
   (TDD where it fits) → local build + tests pass → push → PR → reviewer agent → squash-merge.
   Parallel tasks use git worktrees.
5. **QA** — adversarial QA fleet produces `docs/QA-REPORT.md`; HIGH findings spawn tasks.
   Place freeze-gate QA as early as its deps allow.
6. **CI** — GitHub Actions mirroring local presets 1:1; required platforms hard-gate.

## Operating rules for agents

- Tasks are defined in `plan/backlog/NNN-*.md`. **Do exactly the task's scope; no scope creep.**
- **Never commit directly to main during the dev phase**; always branch + PR.
- Branch naming: `task/NNN-short-slug`. PR title: `NNN: <task title>`.
- **Local verification before any PR:** configure + build + tests must pass; paste real output.
- **Trace-or-deviate:** every `<domain>` claim must trace to `docs/research/` (with section
  anchors) or be marked as a deliberate deviation in an ADR.
- The in-file `status:` field is the single source of truth for task state — not the filename,
  not the INDEX table.
- Only the reviewer stamps `done`, at squash-merge.
