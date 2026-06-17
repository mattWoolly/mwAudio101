<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Backlog index — execution order

All tasks start `status: todo`. Rows are grouped into **execution waves**: every task in a wave
depends only on tasks in *earlier* waves, so a whole wave runs in parallel — one agent + one git
worktree each. The task file's `status` is the source of truth; this table is the dependency map
+ a human-readable dashboard.

> **Status: not yet populated.** This INDEX is filled in **Phase 3 (Backlog)**, which is gated on
> the owner-locked decisions table (`plan/ORCHESTRATION.md`) and `accepted` governing ADRs
> (`plan/decisions/`). Until Phases 1–2 complete, this file is a placeholder.

| Wave | id | Title | Component | Size | Depends on | Status |
|---|---|---|---|---|---|---|
| — | — | _(populated in Phase 3)_ | — | — | — | — |

## Notes (standing rationale ledger — record WHY the DAG is shaped this way)

- Keep independent streams (core DSP / plugin-shell / UI) decoupled until a deliberate late
  integration wave; this maximizes safe parallelism.
- Filter golden-corpus / freeze-gate QA is placed at its earliest possible wave — not deferred to
  the end.
- CI is intentionally last per the owner-locked decision.
