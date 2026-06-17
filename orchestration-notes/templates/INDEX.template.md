# Backlog index — execution order

All tasks start `status: todo`. Rows are grouped into **execution waves**: every task in a
wave depends only on tasks in *earlier* waves, so all tasks within a wave can run in parallel
— one agent + one git worktree each. The task file's `status` is the source of truth; this
table is the dependency map + a human-readable dashboard.

Suffixed ids (`026b`, `045b`, …) were inserted by backlog review to preserve the
ordinal-encodes-rough-order convention without renumbering.

| Wave | id | Title | Component | Size | Depends on | Status |
|---|---|---|---|---|---|---|
| 1 | 001 | Project skeleton, presets, test framework, license-header check | infra | M | — | todo |
| 2 | 002 | <core data type> | core | S | 001 | todo |
| 2 | 003 | <independent infra/ui stream start> | ui | M | 001 | todo |
| 3 | 004 | <…> | core | M | 002 | todo |
| … | … | … | … | … | … | … |
| N | 0NN | Integration wave — assemble the independent streams | app | M | <all stream heads> | todo |
| N+1 | 0XX | Adversarial QA pass | qa | M | <feature-complete set> | todo |
| LAST | 0YY | CI (deferred per locked decision) | infra | M | <all platform bring-up tasks> | todo |

## Notes (standing rationale ledger — record WHY the DAG is shaped this way)

- Keep independent streams (core / app-shell / UI) decoupled until the deliberate late
  integration wave; this maximizes safe parallelism.
- `<NNN>` is placed at its earliest possible wave because it is a freeze-gate — do **not**
  defer it to the end.
- `<NNN>` sits in wave `<k>` because it links a doc created by `<MMM>`.
- **Never CI:** `<NNN>` (closed/payware cross-check) and `<MMM>` (host smoke matrix) are
  local/QA-phase gates only — do not add them as CI steps.
- CI is intentionally last per the owner-locked decision.
