<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# The mwStime Playbook

A reusable process for building a non-trivial software project with a **fleet of AI
agents**, reverse-engineered from the `mwStime` project (which used it to ship ~65 small
PRs across a DSP core, plugin, UI, and cross-platform CI in a few days, with an adversarial
QA pass that caught real concurrency and platform bugs before release).

**Why this exists:** so you can point an agent (or a fleet) at *one place* and get the whole
working method, instead of re-explaining conventions every session. The companion sibling
project (`303`) used large per-subsystem plans → one mega-PR each. That worked, but it
serialized the work, made changes hard to revert, and left no durable ledger of open issues
after merge. This playbook is the version that fixed all three.

---

## How to use this

- **Pointing one agent at the process:** tell it to read `AGENTS.md` (the condensed
  operating rules) plus the template it needs from `templates/`.
- **Bootstrapping a new project:** copy `templates/` into the new repo (`plan/`,
  `plan/backlog/`, `plan/decisions/`, `docs/`), fill in `ORCHESTRATION.md`, then follow the
  phase flow below.
- **Running a fleet:** the orchestrator reads `plan/backlog/INDEX.md`, finds the current
  wave, and dispatches one agent + one git worktree per task in that wave.

Everything below is the *why*. The *what to copy* lives in `templates/`. Each canonical
artifact (task file, ADR, PR body, QA report) is defined **once** there and referenced from
here — don't duplicate them.

---

## The 8 load-bearing practices (lead with these)

If you adopt nothing else, adopt these. They are what made the process work, in priority
order:

1. **Trace-or-deviate.** Every substantive technical claim must *either* cite a research/
   source doc (with a section anchor) *or* be recorded as a deliberate deviation in an ADR.
   There is no third option. This is the single integrity rule that makes "no plausible
   fakes" enforceable — a guess can never ship under a confident label.

2. **One file = one task = one branch = one PR.** The atomic task file (`templates/task.template.md`)
   with six fixed sections and S/M/L sizing (L = "must split before starting") is the unit
   the entire fleet model is built on. Small, revertable, reviewable, individually traceable.

3. **Silent-pass prevention is a hard rule.** Every test-selector command carries a
   fail-on-empty flag (`ctest … --no-tests=error`) **and** test-case *names* begin with the
   selector word (discovery tools register names, not tags). A selector that matches nothing
   must be impossible to mistake for green. This is the highest-leverage anti-foot-gun in the
   whole repo.

4. **Everything runs locally from one command; CI is a thin 1:1 mirror, added last.** No
   build/test logic may live only in CI YAML. Every CI step maps to a committed
   preset/script a developer runs with the identical command (documented in `docs/BUILDING.md`).
   Defer CI until all target platforms build locally — don't pay the CI-iteration tax before
   the code is even portable.

5. **Decision-lock gate + immutable ADRs.** A dated *"Decisions locked with the owner"* table
   sits **above** the ADRs and freezes scope/framework/platforms/budget *before* any backlog
   is written. ADRs are immutable (supersede, never edit) and record each option's pros/cons
   — including each agent-panel persona's position. Coding agents never re-litigate
   architecture.

6. **Execution waves derived from the dependency DAG.** Group tasks into numbered waves where
   every task depends only on *earlier* waves; a whole wave runs in parallel, one agent + one
   worktree each. Keep independent streams (core / shell / UI) decoupled until a deliberate
   *late integration wave*. This is the agent-fleet enabler.

7. **QA is a written, committed deliverable.** An adversarial audit, partitioned into named
   dimensions, under a **3-way verification rule** (reproduce → read source at file:line →
   cite the spec/ADR it *contradicts*). Gate the release on HIGH/critical only. Keep a
   mandatory *"Rejected / false positives"* section. **Auditors never fix inline** — every
   HIGH finding becomes a new numbered backlog task, recursively (even for bugs a QA-fix
   later surfaces in CI).

8. **Golden/regression harness with a guarded bless.** Freeze observable behavior with golden
   tests; a two-stage comparer (pass/fail gate, then rich stdout diagnostics so failures are
   debuggable from logs alone). Re-blessing is a guarded, attributable event: refuse without a
   `BLESS_REASON`, write a provenance MANIFEST (version+hash read *from the producing binary*),
   and treat unexplained golden churn as an automatic review rejection.

---

## The phase flow

One top-level `plan/ORCHESTRATION.md` defines the whole pipeline. Six phases, each gated:

| # | Phase | Entry criterion | Exit criterion | Output |
|---|-------|-----------------|----------------|--------|
| 1 | **Research** | owner brief | every claim adversarially verified + cited; refuted claims fenced off | `docs/research/` cited report + a gitignored `research-cache/` of runnable references |
| 2 | **Architecture** | research locked | an *agent panel* (2–4 personas) proposed competing designs; recommendation chosen; each position + critique captured | `docs/design/*.md` specs (numbered sections) + immutable ADRs in `plan/decisions/` |
| 3 | **Backlog** | every relevant decision locked in an ADR | atomic task files written, sized, and laid out in waves | `plan/backlog/NNN-*.md` + `plan/backlog/INDEX.md` |
| 4 | **Development** | the wave's deps are `done` | each task: branch → implement → local verify → PR → reviewer agent → squash-merge | small squash-merged PRs on `main` |
| 5 | **QA** | feature-complete (place freeze-gate QA *early*, not at the end) | adversarial audit committed; HIGH findings spawned as tasks and resolved | `docs/QA-REPORT.md` + remediation tasks |
| 6 | **CI** | all target platforms build locally | CI mirrors local commands 1:1; required platforms are hard gates | `.github/workflows/` |

**Hard gate before Phase 3:** no task files may be written until the owner-locked decisions
table exists and the governing ADRs are `accepted`. This is what stops decision churn
mid-build.

**Phase ordering nuance:** place safety-critical / freeze-defining QA tasks as *early* as
their dependencies allow (e.g. right after the golden corpus exists), not lumped at the end.

---

## The canonical artifacts (each defined once, in `templates/`)

| Artifact | Template | Role |
|---|---|---|
| Orchestration plan | `templates/ORCHESTRATION.template.md` | the owner-locked decisions table + phases + agent operating rules |
| Backlog conventions | `templates/backlog-README.template.md` | task-file rules, status lifecycle, silent-pass rules, per-task workflow |
| Task file | `templates/task.template.md` | the atomic unit: 6 sections + frontmatter |
| Wave index | `templates/INDEX.template.md` | the dependency map + parallelism plan + *why-annotated* notes |
| ADR | `templates/adr.template.md` | immutable decision record |
| PR body | `templates/pr-body.template.md` | What & why / TDD / Verification (real output) / Notes |
| QA report | `templates/qa-report.template.md` | adversarial audit deliverable |

### Task file — the six sections, and *why each one*

(Full template: `templates/task.template.md`.) A task is executable by a *fresh* agent with
no conversation history because of how the sections are engineered:

- **Objective** — outcome-framed: *what exists when done* ("X and Y exist, unit-tested"),
  never activity-framed ("write X").
- **Context** — a *pure pointer list*, in reading order, to exact files + numbered design
  sections (`architecture.md §4.2`) + ADRs. Never restate design content; the design section
  is the single source of truth, the task is a thin scope wrapper. This is how tasks stay
  low-context.
- **Scope** — precise bullets, naming files and signatures to create.
- **Out of scope** — names the *specific other task id* that owns each deferred item ("MIDI
  audition (035), drag-out export (036)"). This is the primary scope-creep fence: it *routes*
  the urge to "just also do X" to a real backlog slot instead of vaguely forbidding it.
- **Acceptance criteria** — checkboxes, objectively checkable; cite the design section they
  enforce ("integer-only per §3.2") so review is mechanical.
- **Verification commands** — exact, copy-pasteable, must be *seen* to pass before any PR.

### Status lifecycle, concretely

`todo → in-progress → in-review → done` (+ `blocked` off-path). The in-file `status:`
frontmatter is the **single source of truth** — not the filename ordinal, not the INDEX
table. The lifecycle is realized by a **three-commit triplet on main** per task:

1. `Backlog: NNN — <reason>` — the *claim* commit; diff is only the task spec. Publishes
   intent to main **before** the implementation branch merges, so the plan survives
   interruption.
2. `NNN: <title> (#PR)` — the squash-merge of the implementation PR.
3. `chore: mark NNN done` — flips `status: done` and mirrors it into `INDEX.md`.

**Only the reviewer stamps `done`, at merge.** An agent never marks its own task done.

### Waves & ids

- Ids are zero-padded ordinals encoding *rough* dependency order.
- When review splits or inserts a task, use a **letter suffix** (`045` → `045` + `045b`) so
  ordinals and `depends-on` references stay stable — never renumber.
- `INDEX.md`'s bottom **Notes block is a standing rationale ledger**: it records *why* the DAG
  is shaped as it is, including intentional ordering exceptions and **"never CI"** exclusions
  ("051 (CI) intentionally last per the locked decision; 026c and 048b are never CI steps").
  This stops a future agent from "helpfully" re-ordering or adding a forbidden CI step.

---

## Foot-gun rules (elevated — state these loudly to every agent)

These are the specific traps the process exists to prevent. They live in `AGENTS.md` too.

- **`--no-tests=error` on every selector + test names prefixed with the selector word.**
  `ctest` exits 0 when a selector matches nothing. This is *the* silent-green foot-gun.
- **Auditors diagnose only — never fix inline.** Every HIGH finding → a new numbered task
  through the normal branch+PR+review pipeline; recursively (a QA-fix that breaks CI gets its
  *own* follow-up task — e.g. a macOS-only fix that broke a Linux lock-free assert).
- **A finding that "Contradicts: nothing" is self-refuting** → move it to Rejected. Verify
  counts/claims against the **actually-built artifacts**, not against prompt/spec numbers
  (which go stale).
- **Bless/re-baseline is guarded and attributable:** no regen without `BLESS_REASON`; record
  provenance (version+hash *from the producing binary*, UTC date, who, platform, reason) in a
  committed MANIFEST; only in a PR that quotes the reason and links an ADR/bug; add a CI
  *bless-guard* job that fails any PR touching `blessed/` without a same-diff MANIFEST change.
- **Never let a convenient/closed reference become the calibration oracle.** Demote it to a
  secondary, *local-only* cross-check; exclude it from CI (`ctest -E`); keep its binaries in a
  gitignored, non-redistributable `research-cache/`; never re-bless a golden or change the
  engine to chase a reference delta — route deltas to a human.
- **Pin every external tool by version + verified SHA-256.** Checksum mismatch is a HARD
  failure, never a silent skip. Self-skip (exit 77 wired to CTest `SKIP_RETURN_CODE`) only
  when a tool is genuinely *absent* — never when present-but-unverifiable.
- **Never build an artifact/format on a platform where you have no validator for it.** An
  unchecked artifact is a foot-gun (e.g. drop LV2 on Windows if no Windows LV2 validator is
  wired). Scope formats per-platform in the preset and document *why* in `docs/BUILDING.md`.
- **Bake cross-cutting invariants into the single local verification command** so no task can
  forget them: e.g. a license-header / SPDX check registered as a ctest (`license_headers`)
  runs under `ctest --preset default` like any functional test, so every task inherits it.
- **Tag every invented constant `(PI)`** (pragmatic invention) and centralize such constants
  in one calibration struct/data table. QA later sweeps every `(PI)`/uncited constant to a
  binary exit: **Confirmed** (test-pinned + tuning note) or **Ticketed** (open task). Show the
  `grep` commands used to build the ledger.

---

## Cross-platform & golden specifics (reconciled)

- **Bit-exactness scope, stated precisely:** the *integer / deterministic-by-construction*
  path is bit-exact **across all platforms** (this turns a third compiler into a discipline
  check). *Floating-point* stages are bit-exact only on the **reference platform** (where
  blessing happens) and tolerance-compared elsewhere (e.g. `max abs ≤ 1e-6`) plus domain
  checks. Pin FP discipline in the build (`-ffast-math` off, `-ffp-contract=off`, `/fp:precise`)
  wherever you claim bit-exactness.
- **Platform tiers:** one *reference* platform (blessing + bit-exact compare), one or more
  *co-required* platforms (hard gates, `continue-on-error: false`), and lower-priority *goal*
  platforms (`continue-on-error: true`). Bring platforms up in priority order as
  dependency-chained tasks *before* wiring CI.
- **Graceful-degrade, visibly:** when a CI runner lacks a packaged validator, *attempt then
  annotate* (`::notice::`) and let the check self-skip — and write down which phase owns that
  gate ("LV2 validation is the local/QA-phase gate"). Visible annotation, never a silent pass.
- **Calibration tools self-test:** a fitting/calibration tool ships with a *planted-answer*
  recovery test (plant a known value, assert residual ≈ 0) and *refuses to fit on
  validation-tagged data* (disjoint cal/val sets enforced at the CLI), both unit-tested before
  any real data exists.

---

## Docs hygiene

- Every doc (and source file) starts with the license/SPDX header; docs are linted
  (markdownlint) as first-class artifacts.
- Every *disabled* lint rule carries an inline comment explaining **why** it's off.
- `docs/BUILDING.md` is the declared source of truth for the local==CI command mapping and
  the per-platform format table (which formats build on which OS, and why).

---

## Why this beats the mega-plan approach (the `303` contrast)

What `mwStime` did that the sibling `303` project did not, and the cost `303` paid:

| mwStime practice | What `303` did instead | Cost avoided |
|---|---|---|
| Atomic task files, wave-parallel | One plan per subsystem → one mega-PR | `303`'s structure forced a long serial chain; mwStime ran up to ~11 tasks per wave in parallel |
| ~65 small squash-merged PRs | 10 big merge-commit PRs | A regression in `303` can't be reverted without losing a whole subsystem |
| Externalized QA-REPORT + QA-spawned tasks | QA prose inside each merged branch | After a `303` branch merges there's no standing ledger of known limitations; mwStime's external QA caught/fixed real concurrency + platform races that a whole-subsystem PR would have buried |
| Immutable ADR directory | Rationale in prose/chat | `303` has decision amnesia — the *why* of deferrals/choices isn't durable or supersedable |
| S/M/L size cap + `Out of scope` per task | No size cap | `303` routinely shipped multi-hundred-LOC tasks in one PR |
| `--no-tests=error` + tag-prefixed names | ctest gates without the guard | `303` is exposed to the silent-green selector trap |

**Keep the one good thing `303` had too:** its in-plan *self-review matrix* (spec-coverage map,
deliberate-simplifications list, placeholder scan, type-consistency check) is complementary to
mwStime's externalized backlog/ADR/QA layer — use both.

---

*Generated from a multi-agent audit of `mwStime` (10 process dimensions + a completeness
critic). See `templates/` for copy-ready artifacts.*
