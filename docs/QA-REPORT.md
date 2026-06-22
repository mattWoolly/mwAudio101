<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# mwAudio101 — Adversarial multi-dimension QA audit (Phase 5, task 154)

**Status:** audit complete, read-and-report. **Scope:** the feature-complete core engine +
golden/calibration/invariant harness, audited across the silent-pass, CLASS-EXACT/FP-compare,
provenance, calibration-self-test, and cross-cutting RT/FP/CPU/license dimensions of
ADR-013 Contract C1–C22, plus the LFO multi-destination routing-fidelity adjudication
embedded in the task.

**Audit posture (the one truth this report encodes, per docs/design/11 §1.3 / ADR-013
Consequences):** mwAudio101 holds no physical SH-101 and takes no bench measurement as the
oracle. Every verdict below is about **self-consistency and topology-faithfulness, NOT measured
SH-101 fidelity.** No statement in this report asserts a measured-hardware match; where a check
cannot exist for that structural reason it is labelled a **PERMANENT** limit, never a TODO
(§Permanent structural limits).

**Trademark.** "Roland" / "SH-101" are trademarks of Roland Corporation. mwAudio101 is an
independent, unaffiliated work modelling documented circuit behaviour.

---

## 0. Evidence base (what I actually ran)

All evidence below is from a real local build + run in this worktree (local == CI per
docs/design/11 §9.4 / docs/BUILDING.md), commit `d372ed9`, macOS arm64 (the reference/bless
platform), AppleClang 17.0.0, generator Unix Makefiles, `MW_BUILD_PLUGIN=OFF` (JUCE-free core +
headless Catch2 binary).

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --preset default          # Configuring done; resolved formats (macos) = STANDALONE
cmake --build --preset default  # Built target mwcore / mw101_tests / LicenseHeaderCheck / FpDisciplineCheck
ctest --preset default -N       # Total Tests: 995  (976 Catch2 cases + 19 add_test gates)
```

Discovery facts used throughout (observed, not asserted):

- **`ctest --print-labels` reports "No Labels Exist."** This is load-bearing: `catch_discover_tests`
  registers Catch2 cases by **name** (prefix `mw101.`), and does **not** propagate Catch2 `[tags]`
  as ctest `LABELS`. So subsystem coverage is enforced by **name-prefix / Catch2-tag discovery**
  (the `prefix_coverage` add_test and the new `qa` suite), and the checked-in label snapshot
  (`tests/golden/corpus/ctest-labels.snapshot`, 108 tag tokens) is a **Catch2-tag** snapshot diffed
  by the `labels_snapshot` add_test, not a `ctest --print-labels` snapshot. The §8.4 intent is met by
  a different (equivalent) mechanism than the literal `ctest --print-labels` wording; this is recorded
  as a benign deviation, not a gap.

---

## 1. Coverage matrix (adversarial, §3.1 / §8.2 / ADR-013 C2)

### 1.1 Required subsystem prefixes — every prefix maps to >= 1 discovered test

Counts are Catch2 `[tag] --list-tests` matches against the built `mw101_tests` binary at commit
`d372ed9` (the mechanism `cmake/CheckPrefixCoverage.cmake` uses). The new `qa` suite
(`tests/unit/QaCoverageMatrixTest.cpp`) asserts the same matrix in-process against the Catch2
registry and recognises the alias tags actually applied to each subsystem.

| Required prefix (§3.1) | Canonical tag count | Covered? | Proving suites / alias tags (observed) |
| --- | --- | --- | --- |
| `vco`  | `[vco]` = 10  | **YES** | `[vco] [vcoshape] [oscsection] [polyblep] [sub] [dispatch_vco]` (qa: 60 cases) |
| `vcf`  | `[vcf]` = 22  | **YES** | `[vcf] [vcf-core] [vcf-tpt] [vcf-tanh] [vcf-reso] [vcf-tables] [dispatch_vcf]` (qa: 62) |
| `vca`  | `[vca]` = **0** | **YES (alias)** | `[vca_taper]`=10, `[vca_thump]`=14, `[envlfovca_rtsafe]`=8 (qa: 32) |
| `env`  | `[env]` = **0** | **YES (alias)** | `[env_curve]`=11, `[env_trig]`=11, `[env_header]`=9 (qa: 39) |
| `seq`  | `[seq]` = **0** | **YES (alias)** | `[seqengine]`=11, `[stepseq]`=12, `[modseqtypes]`=11, `[engine_seq]`=10 (qa: 44) |
| `prng` | `[prng]` = 4  | **YES** | `[prng] [vintage_prng] [noise]` (qa: 23) |
| `arp`  | `[arp]` = 17  | **YES** | `[arp]` (qa: 17) |
| `cal`  | `[cal]` = 24  | **YES** | `[cal] [calibration]` (qa: 36) |

**No required subsystem has zero coverage.** The `qa` suite verdict (all PASS):
`vco=60, vcf=62, vca=32, env=39, seq=44, prng=23, arp=17, cal=36` discovered cases.

> **FINDING QA-154-1 (MEDIUM, gate under-wired — not a coverage hole).** Three required
> subsystems — `vca`, `env`, `seq` — have **zero tests carrying the canonical single-token tag**
> (`[vca]` / `[env]` / `[seq]`); their coverage lives entirely under multi-word alias tags
> (`[vca_taper]`, `[env_curve]`, `[seqengine]`, …). Worse, the C2 gate that is supposed to catch a
> zero — `cmake/CheckPrefixCoverage.cmake` invoked from `tests/CMakeLists.txt:57` — is wired with
> only **`REQUIRED_TAGS=cal;prng`** and carries an explicit `TODO(task-006)` to widen to the full
> `vco vcf vca env seq prng arp cal` set. So C2 (ADR-013 C2; §8.2) is **only partially enforced
> today**: `prefix_coverage` would stay green even if `[vca]`/`[env]`/`[seq]` Catch2 coverage were
> deleted, because those tokens are not in `REQUIRED_TAGS` and the real suites use alias tokens.
> Evidence: `prefix_coverage` ran and **Passed (0.05 s)** with the current two-tag set.
> **Recommendation (follow-up task):** either (a) add the canonical `[vca]`/`[env]`/`[seq]` tag to
> one suite per subsystem AND widen `REQUIRED_TAGS` to all eight, or (b) teach
> `CheckPrefixCoverage.cmake` the documented alias map (mirroring the new `qa` suite). The `qa`
> suite added by this task already enforces the full eight-subsystem matrix mechanically, so the
> central claim is now test-backed; QA-154-1 is about closing the *gate*, not the coverage.

### 1.2 Cross-cutting invariant tags (§13 / ADR-013 C18–C21)

| Invariant | Contract | Proving selector (observed) | Verdict |
| --- | --- | --- | --- |
| `license` | C18 (§13.2) | add_test `license_headers` → **Passed (0.24 s)** | PASS |
| `rt`      | C19 (§13.1/§13.3) | Catch2 `[rt]` = 33 cases; `audiothreadguard_sentinel` → **Passed (52.47 s)** | PASS |
| `fp`      | C20 (§13.4) | add_test `fp_discipline_guard` → **Passed (0.10 s)** | PASS |
| `cpu`     | C21 (§13.5) | Catch2 `[cpu]` = 4 cases | PASS |

> **FINDING QA-154-2 (LOW, naming/expectation note).** `[license]` and `[fp]` are **not Catch2
> tags** (the `[fp]`/`[license]` Catch2-tag queries return 0 matching cases); they are standalone
> `add_test` invariant gates (`license_headers`, `fp_discipline_guard`). The §3.1 table lists
> `[license] [rt] [fp] [cpu]` as cross-cutting "tags", which a reader could mistake for Catch2 tags.
> No functional gap — both gates exist and pass — but the audit records that two of the four
> cross-cutting invariants are enforced as process/grep gates, not Catch2 cases, so the `qa` suite
> only asserts the `[rt]`/`[cpu]` Catch2 legs and defers `license`/`fp` to their add_test gates.

---

## 2. Per-contract audit — ADR-013 C1–C22

One auditable verdict per contract case, each citing its design § and the proving ctest
tag/name, with the **observed** result from the run above. "PASS" = the proving mechanism exists,
is discovered, and ran green here. "PARTIAL" = the property holds but the *gate* is incompletely
wired (a finding is filed).

| Case | Property (design §) | Proving tag / selector | Observed | Verdict |
| --- | --- | --- | --- | --- |
| **C1** | empty/mis-filtered binary FAILS (§4.1) | `--no-tests=error` + `FAIL_REGULAR_EXPRESSION "No tests ran"` on `catch_discover_tests` (tests/CMakeLists.txt:49) | testPresets carry `--no-tests=error`; `-R qa` selected exactly 3, did not silent-pass | PASS |
| **C2** | every required prefix >= 1 test (§8.2) | `prefix_coverage` add_test + new `[qa]` suite | matrix §1.1 all >=1; **but** gate wired `cal;prng` only | **PARTIAL** (QA-154-1) |
| **C3** | deleted/renamed suite => failing diff (§8.4) | `labels_snapshot` add_test vs `ctest-labels.snapshot` | **Passed (0.03 s)**; 108 tag tokens snapshotted | PASS (snapshot is Catch2-tag, not `ctest --print-labels` — §0) |
| **C4** | paired negative/property control on numeric DSP (§4.2) | e.g. `[vcf]` self-osc k>=4 vs k=3.9; `[qa]` negative control (unknown token => 0) | `[class-exact]` 9 cases + `[qa]` negative control **PASS** | PASS |
| **C5** | CLASS-EXACT SHA-256 identical arm64 & Linux (§6.2) | `[class-exact]` = 9 cases | ran **All tests passed (214 assertions in 9 cases)** | PASS (arm64 leg; Linux leg is CI's, not run here) |
| **C6** | CLASS-FP bit-exact on arm64 (§6.3) | `[class-fp]` = 14 cases | discovered + green in `[golden]` run | PASS (arm64) |
| **C7** | CLASS-FP within manifest band on Linux (§6.3) | `[class-fp]` + per-corpus `tolerance` in MANIFEST.toml | tolerance bands present in MANIFEST | PASS-by-construction; **Linux band not exercisable on this host** (PERMANENT-LOCAL, §3) |
| **C8** | CLASS-FP Windows goal tier (§6.3) | same tier as C7, goal-gated | n/a on this host | DEFERRED to CI (goal) |
| **C9** | Stage 2 skipped unless Stage-1 flags / `--full` (§6.1) | `[golden]` comparer cases | `[golden]` = 55 cases green | PASS |
| **C10** | bless REFUSES on non-arm64 (§7.2) | `[manifest]`/`[provenance]` bless-guard cases | `[manifest],[provenance]` **195 assertions / 23 cases PASS** | PASS |
| **C11** | bless REFUSES without `BLESS_REASON` (§7.2) | same suite | covered in the 23 green cases | PASS |
| **C12** | golden hash absent from MANIFEST => FAIL (§7.5) | `[manifest]` completeness case | green | PASS |
| **C13** | MANIFEST entry with no test => FAIL (§7.5) | `[manifest]` orphan case | green | PASS |
| **C14** | honesty-label provenance required (§7.4) | `[provenance]` = 7 cases | green; MANIFEST carries `honestyLabels` vocab | PASS |
| **C15** | calibration planted-answer recovers within tol (§12) | `[cal]`/`[calibration]` self-tests | `[cal]` = 24 cases discovered, green | PASS |
| **C16** | disjoint cal/val held-out error within tol (§12) | `[cal]` disjoint-split case | green | PASS |
| **C17** | negative-control fixture REJECTED (§12) | `[cal]` negative-control case | green | PASS |
| **C18** | every source carries SPDX GPL-3.0-or-later (§13.2) | `license_headers` add_test | **Passed (0.24 s)** | PASS (this report carries the header) |
| **C19** | no alloc/lock on `processBlock` under stress (§13.1) | `[rt]` = 33 cases; `audiothreadguard_sentinel` | **Passed (52.47 s)** | PASS |
| **C20** | FP-discipline flags OFF in DSP TU (§13.4) | `fp_discipline_guard` greps `compile_commands.json` | **Passed (0.10 s)** | PASS |
| **C21** | worst-case per-block wall-time under ceiling (§13.5) | `[cpu]` = 4 cases | discovered + green | PASS (host-relative ceiling, §3) |
| **C22** | CLASS-FP compare REFUSES across engine tag/oversample/renderVersion (§5.3) | `[class-fp]` / `[golden]` engine-tag isolation cases | green | PASS |

**Summary:** 19 of 22 contract cases PASS as run on the reference host; **C2 is PARTIAL**
(QA-154-1, gate under-wired); **C8** (Windows) is a goal tier deferred to CI; **C7's Linux leg**
is structurally not exercisable on an arm64 host (it is a CI cross-platform gate, by design).

---

## 3. Permanent structural limits (no-oracle / measured-fidelity) — NOT TODOs

These are inherent to the locked no-physical-oracle policy (docs/design/11 §1.3; ADR-013 Context
+ Consequences; research/13 §1.1, §5.6). They are recorded as **PERMANENT** and must never be
re-labelled "fix later" or papered over with a measured-fidelity claim.

1. **No measured-SH-101 fidelity is provable, ever.** A golden proves "the DSP still does what it
   did when blessed," never "this matches a real SH-101." A wrong-but-stable render passes forever
   (ADR-013 Consequences). The harness detects "we changed the DSP," not "we modelled the circuit
   wrong." **PERMANENT.**
2. **The §5 hardware-measurement gaps are permanent** (ADR-013 Context): ADSR curve law (§5.1),
   filter Bode/phase/resonance (§5.2), oscillator/sub-osc/noise spectra (§5.3), drift (§5.4),
   DSP-vs-circuit fidelity (§5.6). No test can close them under this policy. The MANIFEST honesty
   labels (`clone-derived` / `reverse-engineered` / `theory/inference` / `community-disassembly` /
   `service-manual` / `disputed` / `software-emulation-artifact`) are the *only* control that keeps
   such a guess from hardening into a "regression-protected fact." **PERMANENT.**
3. **CLASS-FP tolerance bands are a judgement call, not an oracle** (§6.4): too tight => Linux flaps
   red on legitimate libm last-ULP/FMA/reduction-order divergence; too loose => small real
   regressions slip through. The MANIFEST makes the band *honest and reviewable*, not *automatic*.
   **PERMANENT** (periodic re-derivation, not a one-time fix).
4. **The (PI) modulation-depth scalings are pragmatic inventions, not measured constants.** The
   LFO→VCO (V/oct), LFO→VCF (Hz/V), LFO→PWM (%/V) and velocity depths are explicitly "measurement-
   required open gaps" (docs/design/03 §3.6; research/04 §5.3), carried as `(PI)` in
   `core/calibration/ControlDispatchLfoConstants.h`. Tests assert their *effect direction and
   determinism*, never an absolute hardware-correct magnitude. **PERMANENT.**

**PERMANENT-LOCAL (host scope, not a defect):** C5/C6 were verified only on the arm64 reference
leg here; C7 (Linux band) and C8 (Windows) are cross-platform CI gates that cannot be exercised on
a single arm64 host. C21's wall-time ceiling is host-relative and re-derived per reference host id
(§13.5). These are environmental, not coverage gaps.

---

## 4. LFO multi-destination routing-fidelity adjudication (the headline finding)

**VERDICT: REGRESSION.** The as-built control dispatch gates the three per-destination LFO depths
(`lfo.depth_pitch` / `lfo.depth_cutoff` / `lfo.depth_pwm`) behind a single `lfo.dest` mux, applying
the LFO to **exactly one** destination at a time. The design contract is unambiguous and consistent
across **three** numbered design sections **and the governing ADR**: the LFO modulates VCO pitch
**AND** VCF cutoff **AND** (when its source switch = LFO) PWM **simultaneously**, each scaled by its
own depth knob; `lfo.dest` is at most an **emphasis** selector, never a single-destination switch.
This is a fidelity regression against the ratified routing topology.

### 4.1 What the design + ADRs require (source of truth)

- **ADR-007 §Decision item 1** (the ratified modulation-routing topology; immutable ADR):
  > "One LFO scales the *same instantaneous selected-LFO value* into **three fixed destinations**
  > with **per-destination depth gains**: VCO pitch (single MOD depth), PWM (own ENV/MANUAL/LFO
  > source switch + PWM depth), VCF cutoff (own MOD depth …)."
  There is **no `lfo.dest` mux in the routing decision at all** — the topology is three depth gains
  fed by one LFO value, simultaneously.

- **docs/design/05 §3.1** (routing model): "One LFO scales the **same instantaneous selected-LFO
  value** into three fixed destinations through **independent per-destination depth gains**." §3.2's
  `resolve()` is a branch-free expression that computes `pitchMod`, `cutoffMod`, and `pwmMod`
  **every call** — `pitchMod = lfoValue * lfoToPitch`, `cutoffMod = lfoValue * lfoToCutoff + …`,
  with only `pwmMod` gated (by `PwmSource`, not by a pitch/cutoff/pwm mux).

- **docs/design/05 §3.3 routing table** marks the legs explicitly:

  | Source | Destination | Active when |
  | --- | --- | --- |
  | LFO | VCO pitch  | **always** |
  | LFO | PWM        | PwmSource = LFO |
  | LFO | VCF cutoff | **always** |
  | LFO | VCA tremolo | always (summed) |

- **docs/design/03 §3.6** (the authoritative LFO doc): "Routing is **fixed with per-destination
  depths**, **not a matrix** … The LFO value feeds: VCO pitch … Pulse width … VCF cutoff … VCA/gate
  tremolo" — i.e. all destinations at once, each with its own depth.

- **docs/design/06 ~L381** (parameter schema): "`mw101.lfo.depth_pitch`, `mw101.lfo.depth_cutoff`
  and `mw101.lfo.depth_pwm` are the three faithful per-destination LFO depths; `mw101.lfo.dest`
  (§3.4) selects which destination the single hardware LFO routing **emphasizes**." The word is
  **"emphasizes,"** not "selects exclusively" / "muxes."

### 4.2 What the code actually does (the regression)

`core/voice/Voice.cpp` (applyControls), the dispatch site:

```cpp
// L247-256  — "Route to EXACTLY ONE destination per lfo.dest (the single MOD switch)."
float lfoPitchVolts = 0.0f;
float lfoCutoffOct  = 0.0f;
float lfoPwmNorm    = 0.0f;
switch (c.lfoDest) {
    case 1:  lfoCutoffOct = lfoEff * c.lfoCutoffDepthOct;   break;  // Filter
    case 2:  lfoPwmNorm   = lfoEff * c.lfoPwmDepthNorm;     break;  // PWM
    default: lfoPitchVolts = lfoEff * c.lfoPitchDepthVolts; break;  // Pitch
}
```

The `switch` zeroes the two non-selected legs: at `lfo.dest = Pitch`, both the cutoff and PWM LFO
depths are dead; etc. `core/Engine.cpp` (L857-861) decodes **all three** depths unconditionally
into the per-voice control block, but Voice's `switch` discards two of them every tick. The Voice
field comment (`Voice.h` L137-139) states the gating in plain terms:
`dest=Pitch -> + lfoPitchDepthVolts`, `dest=Filter -> + lfoCutoffDepthOct`,
`dest=Pwm -> + lfoPwmDepthNorm` — one leg per tick.

The single VCF-panel leg `vcf.lfo_mod` (`vcfLfoModCutoffOct`, Voice.cpp L264) is correctly applied
**unconditionally** (outside the switch) — 162e got that one right. The defect is specifically the
**three `lfo.depth_*` legs** being mutually exclusive.

### 4.3 Why the code's own justification does not hold

The implementation cites its authority twice, and **both citations are mis-applied**:

- `ControlDispatchLfoConstants.h` L26-31: *"the dispatch routes the bipolar LFO output to EXACTLY
  ONE destination (the hardware MOD selector is a single-position switch, **docs/design/03 §3.2**)."*
- `Voice.cpp` L247: *"Route to EXACTLY ONE destination per lfo.dest (the single MOD switch)."*

But **docs/design/03 §3.2 is titled "Waveform selector — four positions, NO sine core"** and is
about the **LFO waveform shape** (SmoothTri / Square / Random / Noise) being a single-selection
*source* — "one waveform active at a time, **not simultaneous outputs**" (§3.1). It says nothing
about *destination* routing. The code conflated "one LFO **waveform** active at a time" (true, §3.2)
with "one LFO **destination** active at a time" (false — §3.6 says routing is fixed multi-dest).
The actual destination-routing section, §3.6, and ADR-007 item 1, both mandate simultaneous
multi-destination routing. There is **no ADR** that ratifies single-destination gating; ADR-028
item 3 ("LFO→{pitch,pwm,cutoff} per lfo.dest×depth") is the only text that could be *read* as
dest-gating, but it is subordinate to ADR-007's routing topology and to docs 03/05/06, and on the
weight of four concordant sources the always-active reading governs.

### 4.4 Adjudication: not "ambiguous-needs-owner"

Doc 06's "emphasizes" wording is mild, but it does **not** conflict with doc 05/03 — it is fully
consistent with `lfo.dest` being a soft emphasis on top of always-active depths. The only genuinely
mux-suggestive text is the *as-built code's own comments* (which mis-cite §3.2) and the terse
ADR-028 phrase. Against that stand ADR-007 item 1 (ratified topology, "three fixed destinations …
per-destination depth gains"), doc 03 §3.6 ("fixed … not a matrix"), and doc 05 §3.1/§3.3 ("always"
on the pitch and cutoff legs). The documentary weight is decisively on **always-active multi-dest**;
this is a regression, not an open design question.

### 4.5 Recommended FOLLOW-UP task (fix scope — DO NOT implement in this audit task)

File a new HIGH backlog task ("LFO multi-destination routing fidelity: apply all three
`lfo.depth_*` legs simultaneously; reinterpret `lfo.dest` as emphasis"). Precise scope:

1. **`core/voice/Voice.cpp` ~L247-256:** delete the `switch (c.lfoDest)` mux. Compute all three
   legs every tick, each scaled by its own depth:
   `lfoPitchVolts = lfoEff * c.lfoPitchDepthVolts;`
   `lfoCutoffOct  = lfoEff * c.lfoCutoffDepthOct;`
   `lfoPwmNorm    = lfoEff * c.lfoPwmDepthNorm;`
   (pitch + cutoff are "always"; PWM additionally honours `PwmSource = LFO` per §3.3 — confirm the
   PWM-source switch, not `lfo.dest`, governs the PWM leg). The existing sum sites
   (`oc.vco.pitchCvVolts += lfoPitchVolts`, the cutoff CV sum, `oc.vco.pwmCvNorm += lfoPwmNorm`) are
   already correct and unchanged.
2. **Reinterpret `lfo.dest`** per docs/design/06 L381: as an *emphasis/boost* on the selected leg
   (e.g. a `(PI)` emphasis gain applied to the chosen destination), NOT a mux. The precise emphasis
   law is a `(PI)` constant that must land in a calibration header (it is not a measured value) —
   do not inline it.
3. **`core/Engine.cpp` L850-861:** the three depths are already decoded unconditionally — no change
   needed beyond ensuring `lfo.dest` is passed only as the emphasis selector, not consumed as a mux.
4. **Update the mis-citation:** `ControlDispatchLfoConstants.h` L26-31 and `Voice.cpp` L247 must stop
   citing §3.2 (waveform selector) as authority for single-dest routing; cite §3.6 / ADR-007 item 1.
5. **TDD:** extend `tests/unit/DispatchLfoModTest.cpp` (`[dispatch_lfo]`) with a **simultaneity**
   case: with `lfo.depth_pitch>0` AND `lfo.depth_cutoff>0` AND `lfo.dest=Pitch`, assert BOTH the
   vibrato sideband-spread (pitch leg) AND the amplitude-envelope wobble (cutoff leg) are present in
   one render — the current suite only ever sets one dest at a time and so cannot catch this
   regression. Add the paired negative control (depths at 0 => no modulation) per C4.

> **FINDING QA-154-3 (HIGH, fidelity regression).** Single-`lfo.dest` gating of the three
> `lfo.depth_*` legs in `core/voice/Voice.cpp` ~L252-256 contradicts the ratified always-active
> multi-destination routing (ADR-007 item 1; docs/design/03 §3.6; docs/design/05 §3.1/§3.3;
> docs/design/06 L381). Recommend the follow-up task in §4.5. **Not fixed in task 154** (read-only
> audit; DSP fix is out of this task's scope and edit waiver).

---

## 5. qa coverage test (test-enforced spine)

This audit adds **`tests/unit/QaCoverageMatrixTest.cpp`** (display names begin `qa`, tag `[qa]`),
so the report's central coverage claim (§1.1) is mechanically enforced, not prose:

- `qa coverage matrix: every required subsystem prefix maps to a discovered test` — enumerates the
  in-process Catch2 registry and asserts each of `vco vcf vca env seq prng arp cal` has >= 1
  discovered case via its canonical-or-alias tag set.
- `qa coverage matrix: an unknown subsystem token has zero discovered tests` — paired negative
  control (C4 discipline): proves the matcher discriminates.
- `qa coverage matrix: cross-cutting Catch2 invariant tags are present` — asserts `[rt]`, `[cpu]`.

Observed run (commit `d372ed9`):

```
$ ctest --preset default -R qa --no-tests=error --output-on-failure
    Start 785: mw101.qa coverage matrix: every required subsystem prefix maps to a discovered test
1/3 Test #785: ... Passed    0.01 sec
    Start 786: mw101.qa coverage matrix: an unknown subsystem token has zero discovered tests
2/3 Test #786: ... Passed    0.01 sec
    Start 787: mw101.qa coverage matrix: cross-cutting Catch2 invariant tags are present
3/3 Test #787: ... Passed    0.01 sec
100% tests passed, 0 tests failed out of 3
```

**New core qa tag added:** `[qa]`. Per docs/design/11 §8.4 the orchestrator must regenerate
`tests/golden/corpus/ctest-labels.snapshot` on merge to include `[qa]` (otherwise the
`labels_snapshot` diff gate will flag it — which is the gate working as intended).

---

## 6. Findings register

| ID | Severity | Title | Contradicts | Disposition |
| --- | --- | --- | --- | --- |
| QA-154-1 | MEDIUM | C2 per-prefix gate wired `cal;prng` only (TODO task-006); `[vca]`/`[env]`/`[seq]` canonical tags absent | ADR-013 C2; §8.2 | Follow-up task: widen `REQUIRED_TAGS` + add canonical tags (or teach the alias map). Coverage itself is present (matrix §1.1) and now test-enforced by `[qa]`. |
| QA-154-2 | LOW | `[license]`/`[fp]` are add_test gates, not Catch2 tags | §3.1 wording | Documentation note; both gates exist + pass. No code change required. |
| QA-154-3 | HIGH | LFO single-dest gating is a fidelity regression vs always-active multi-dest routing | ADR-007 item 1; docs 03 §3.6, 05 §3.1/§3.3, 06 L381 | Follow-up DSP task per §4.5. **Not fixed here** (read-only audit). |

No findings were rejected/false-positive in this pass; QA-154-1/2/3 each reproduce against the
built artifact at commit `d372ed9`.

---

## References

- ADR-013 (plan/decisions/013-testing-golden-calibration-harness.md) — Contract C1–C22.
- ADR-007 (plan/decisions/007-modulation-arp-seq-model.md) — §Decision item 1, the routing topology.
- ADR-028 (plan/decisions/028-control-dispatch-seam.md) — the control-dispatch seam.
- ADR-023 — engine versioning / blessed sample-rate set (renderVersion governance, C22).
- docs/design/11-testing-build-ci.md §1.3, §3.1, §5.1, §6, §7.5, §8.2, §8.4, §13, Acceptance hooks.
- docs/design/03-dsp-envelope-lfo-vca.md §3.2 (waveform selector), §3.6 (LFO destinations).
- docs/design/05-modulation-arp-seq.md §3.1 (routing model), §3.3 (routing table).
- docs/design/06-parameters-state-presets.md ~L381 (`lfo.dest` as emphasis selector).
- core/voice/Voice.cpp (applyControls, ~L247-264), core/voice/Voice.h, core/Engine.cpp (~L850-861),
  core/calibration/ControlDispatchLfoConstants.h, core/params/ParamDefs.h / ParamIDs.h.
- tests/unit/QaCoverageMatrixTest.cpp (this task), tests/unit/DispatchLfoModTest.cpp,
  cmake/CheckPrefixCoverage.cmake, tests/CMakeLists.txt, tests/golden/corpus/MANIFEST.toml,
  tests/golden/corpus/ctest-labels.snapshot.
