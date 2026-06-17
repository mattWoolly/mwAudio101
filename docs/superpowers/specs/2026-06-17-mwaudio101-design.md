<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# mwAudio101 — Design Spec

**Date:** 2026-06-17
**Status:** Approved (owner sign-off 2026-06-17)
**Owner:** Matt Woolly

A circuit-accurate, GPLv3, cross-platform software synthesizer inspired by the Roland SH-101
(a monophonic analog synth, 1982), extended with the modern essentials a plugin needs to be
market-viable. This document is the validated design; it feeds the playbook phase flow
(`orchestration-notes/README.md`): Research → Architecture/ADRs → Backlog → Development → QA → CI.

> **Trademark note.** "Roland" and "SH-101" are trademarks of Roland Corporation. mwAudio101 is
> an independent, unaffiliated work that models *documented circuit behavior*. The product ships
> under its own name and a modern (non-replica) UI; no Roland marks or trade dress are used.

---

## 1. Owner-locked decisions (ratified 2026-06-17)

This table is normative and sits ABOVE all ADRs. An ADR may record tension with a lock but must
never silently reverse it.

| # | Decision | Choice |
|---|----------|--------|
| 1.1 | Product | **mwAudio101** — circuit-accurate SH-101–inspired monosynth + modern essentials |
| 1.2 | Framework / language | **JUCE**, C++20, CMake |
| 1.3 | Plugin formats | VST3, AU (macOS), CLAP, Standalone. **LV2** = goal-tier (Linux) |
| 1.4 | DSP fidelity | **Circuit-accurate analog modeling** (IR3109 filter, VCO exponential converter, sub-osc divider, per-voice drift, VCA taper) |
| 1.5 | Feature scope | Faithful mono SH-101 path **+ modern essentials** (§4) |
| 1.6 | License | **GPLv3-or-later** (JUCE free tier; binaries still distributable/sellable) |
| 1.7 | Calibration oracle | **Schematics + service manual + IR3109 datasheet + published measurements.** Honest claim: *"modeled from documented circuit behavior."* Recordings/sample packs are a **secondary, local-only, gitignored** cross-check — never the calibration oracle (playbook rule). |
| 1.8 | Platforms | **macOS arm64 = reference** (bless + bit-exact) · **Linux x64 = co-required hard gate** · Windows x64 = goal (best-effort) |
| 1.9 | GUI | **Modern reimagined UI** (signal-flow-inspired, max trademark distance), resizable vector, hi-DPI |
| 1.10 | Presets | **~64**, IDM/acid-leaning, categorized; each preset traceable to a research finding |
| 1.11 | Run scope | Full pipeline (Phases 1–6), autonomous; orchestrator merges green PRs; stop only for genuine owner-locks |
| 1.12 | Fleet intensity | Aggressive — up to ~8–12 agents/wave; multi-dimension adversarial QA |
| 1.13 | PR mechanics | worktree sub-agent → real `gh` PR → QA/reviewer agent 3-way audit → orchestrator squash-merges on green |
| 1.14 | Repo workflow | Research/design/ADRs/backlog → `main` directly. Dev tasks → branch + PR + review → merge. |
| 1.15 | CI timing | **Last**; GitHub Actions mirrors local presets 1:1; macOS + Linux hard-gate |

**Out of scope (now), each deferred via a future ADR:** mod matrix, second LFO/envelope,
multi-FX rack, macro controls, randomizer, sampler/wavetable extensions, mobile (iOS/AUv3),
external hardware MIDI-CC learn UI beyond standard host automation. (These were the
"full workstation" option, declined in favor of "faithful clone + modern essentials.")

---

## 2. Goals & non-goals

**Goals**
- Reproduce the SH-101's *tone-defining* behavior from documented circuit analysis: the 4-pole
  OTA lowpass (IR3109-class) with resonance self-oscillation; the VCO saw/pulse cores with
  exponential pitch conversion; the divider sub-oscillator; noise; the ADSR + LFO modulation
  topology; portamento; the arpeggiator and 100-step sequencer.
- Ship a market-ready plugin: poly/unison, oversampling, per-voice analog drift, an integrated
  Chorus/Delay/Drive FX section, host-synced arp/seq, full host automation, MPE-lite, and a
  curated preset browser.
- Be honest: every authenticity claim traces to a cited research source or is an ADR-recorded
  deliberate deviation. No "authentic" label without a trace.

**Non-goals**
- Not a sample-accurate clone of one specific physical unit (we have no measured hardware oracle;
  see 1.7). We model *documented* behavior and label it as such.
- Not a feature-maximalist workstation (see Out of scope, §1).
- Not a photoreal hardware replica (trademark/trade-dress distance is deliberate).

---

## 3. Architecture overview

Three decoupled streams, integrated in a deliberate late wave (maximizes safe parallelism):

### 3.1 `core/` — DSP engine (pure C++20, no JUCE; the testable, (mostly) deterministic heart)
- **Sources:** `Oscillator` (VCO: saw + variable-width pulse/PWM, exponential pitch converter,
  drift), `SubOscillator` (frequency-divider square, octave-down variants), `NoiseSource`.
- **Mix/shape:** `Mixer` (VCO/sub/noise levels), `LadderFilter` (IR3109-modeled 4-pole 24 dB/oct
  LPF, resonance to self-oscillation, OTA nonlinearity — **the tone-defining module**),
  `Vca` (level + amp envelope, taper).
- **Modulation:** `Envelope` (ADSR with analog-shaped segments), `LFO` (triangle/square/random
  + rate), modulation routing matching the original panel (LFO→pitch/PWM/filter; env→filter/amp).
- **Voicing:** `Voice` (assembles one signal path), `VoiceManager` (mono / poly / unison,
  glide/portamento, note priority, MPE-lite).
- **Sequencing:** `Arpeggiator`, `StepSequencer` (100-step), host-sync `Clock`.
- **Quality:** `Oversampler` wrapper; `params/` normalized parameter contracts (the automation
  surface); `fx/` Chorus, Delay, Drive (post-voice bus).
- All invented constants tagged `(PI)` and centralized in one calibration table.

### 3.2 `plugin/` — JUCE shell
`AudioProcessor`, `APVTS` parameter tree (automation + state contract), state save/load
(versioned), format wrappers (VST3/AU/CLAP/Standalone; LV2 goal-tier), MIDI + MPE-lite input.

### 3.3 `ui/` — Editor
Modern vector UI bound to APVTS, resizable + hi-DPI, signal-flow-inspired layout, preset browser.

### 3.4 Supporting trees
`presets/` (data + loader), `tests/` (Catch2 unit + golden/regression harness),
`tools/` (calibration/fit tools with planted-answer self-tests; disjoint cal/val sets),
`docs/` (research, design, BUILDING, QA-REPORT), `plan/` (backlog, decisions/ADRs, ORCHESTRATION).

---

## 4. Feature scope (the "modern essentials")

| Area | Faithful (from documented SH-101) | Modern essential added |
|---|---|---|
| Oscillator | 1 VCO: saw + pulse(PWM), range switch | per-voice drift control, oversampling |
| Sub / noise | divider sub-osc, white noise | (as original) |
| Filter | 4-pole OTA LPF, resonance self-osc | (faithful; oversampled) |
| Envelope | one ADSR | (faithful) |
| LFO | tri/square/random, modulates pitch/PWM/filter | (faithful) |
| Amp | VCA, gate/env | (faithful) |
| Glide | portamento | (faithful) |
| Arp | up/down/updown modes | host tempo-sync |
| Sequencer | 100-step | host tempo-sync, save/restore in preset |
| Voicing | monophonic | **poly + unison** toggle, voice count |
| FX | none (external) | **Chorus + Delay + Drive** section |
| I/O | CV/Gate hardware | MIDI, **MPE-lite**, full host automation |
| Presets | none (memory-less) | **~64 curated**, preset browser |

Exact ranges/curves (octave switch positions, LFO shapes, ADSR timing, sub-osc division ratios,
PWM range) are **research-to-verify** in Phase 1 and will be pinned in `docs/design/` with
citations before any backlog task hardens them.

---

## 5. Calibration, golden tests & fidelity discipline

- **Bit-exactness scope:** integer/deterministic-by-construction paths are bit-exact across
  macOS + Linux. Floating-point DSP stages are blessed on **macOS arm64** and tolerance-compared
  elsewhere (`max abs ≤ 1e-6`) plus domain checks.
- **FP discipline pinned in build:** `-ffast-math` OFF, `-ffp-contract=off` (`/fp:precise` on
  Windows) wherever bit-exactness/golden compare is claimed.
- **Golden harness:** two-stage comparer (pass/fail gate, then rich diff diagnostics). Re-blessing
  is guarded: refuse without `BLESS_REASON`; write a provenance MANIFEST (version+hash read from
  the producing binary, UTC date, who, platform, reason); CI bless-guard fails any PR touching
  `blessed/` without a same-diff MANIFEST change.
- **Calibration tools self-test:** planted-answer recovery (plant a known value, assert residual
  ≈ 0) and refuse-to-fit on validation-tagged data (disjoint cal/val enforced at the CLI).
- **(PI) ledger:** every invented constant tagged `(PI)`, centralized; QA sweeps each to
  Confirmed (test-pinned + tuning note) or Ticketed (open task).
- **Reference recordings are demoted:** any third-party recording/sample is a *secondary,
  local-only* cross-check, gitignored in `research-cache/`, excluded from CI; never the oracle,
  never a reason to re-bless.

---

## 6. Build, test, platforms

- Everything runs locally from one command set; CI is a thin 1:1 mirror added last.
  `docs/BUILDING.md` is the source of truth for the local==CI mapping and the per-platform
  format table (which formats build on which OS, and why).
- **Silent-pass prevention:** every `ctest -R/-L` selector carries `--no-tests=error`, and test
  names begin with the selector word. A cross-cutting `license_headers` check is registered as a
  ctest so every task inherits it.
- **Platforms:** macOS arm64 (reference) → Linux x64 (co-required hard gate) → Windows x64
  (goal, `continue-on-error: true`). Bring platforms up in priority order *before* wiring CI.
- **Never build a format where no validator is wired** (e.g. don't ship LV2 on a platform with no
  LV2 validator); scope formats per-platform in the preset and document why.

---

## 7. Process (how this gets built)

Per `orchestration-notes/`: one `plan/ORCHESTRATION.md` drives six gated phases. The hard gate
before Phase 3: no backlog task files until this decisions table exists and the governing ADRs
are `accepted`. Tasks are atomic (one file = one task = one branch = one PR), wave-parallel off
the dependency DAG, TDD for core/algorithmic logic, verified locally before any PR, reviewed by
an adversarial QA/reviewer agent, squash-merged by the orchestrator on green. QA is a committed
adversarial deliverable; HIGH findings spawn numbered tasks. CI comes last.

---

## 8. Risks & mitigations

| Risk | Mitigation |
|---|---|
| "Authentic" claim without ground truth | Honest label (1.7); trace-or-deviate; recordings demoted to local cross-check |
| Filter model is the hard part | Treat `LadderFilter` as its own research+ADR+golden-pinned stream; oversample; planted-answer cal tests |
| Cross-platform FP divergence | Bless on macOS, tolerance-compare Linux; FP discipline flags pinned |
| Trademark/trade-dress | Own name, modern non-replica UI, explicit unaffiliated notice |
| Scope creep | "Modern essentials" fixed in §4; everything else routed to Out-of-scope ADRs |
| Fleet context loss | Atomic tasks with pointer-only Context; design docs are the single source of truth |

---

*Approved 2026-06-17. Next: write `plan/ORCHESTRATION.md` and begin Phase 1 (deep research).*
