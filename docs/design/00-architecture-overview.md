<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Architecture Overview

## 1. Purpose and status

### 1.1 Role of this document

This is the **top-level system map** for mwAudio101, a circuit-accurate,
SH-101-inspired software synthesizer (JUCE shell + JUCE-free C++20 DSP core,
GPL-3.0-or-later). It is the index every other design doc hangs off: it defines
the module tree, the end-to-end signal flow, the `core`/`plugin` boundary and its
real-time processing seam, the threading model, the latency (PDC) posture, the
engine-versioning / blessed-sample-rate contract, and the global real-time
invariants that every other doc inherits.

It is a **citation address**: backlog tasks reference sections here by number
(e.g. "implement per docs/design/00 §4.3"). Section numbers are stable; do not
renumber.

### 1.2 What this document owns vs references

This doc OWNS: the module tree (§3), the system signal flow (§4), the
`core`/`plugin` boundary and the `prepare`/`process`/`reset` seam plus the POD
`BlockContext` (§5), the single-threaded voice-render rule (§6), the constant-PDC
posture (§7), the engine `renderVersion` + blessed sample-rate set (§8), and the
global RT invariants (§9).

This doc REFERENCES (never redefines):

- **Parameter IDs, ranges, units, skews, enum orders** — owned by the schema doc,
  docs/design/06 §2 [ADR-008]. This doc never mints a parameter ID; it shows where
  the `ParamSnapshot` is read and where normalized→engineering mapping happens, and
  cites doc 06 for the catalogue.
- **DSP algorithm internals** — oscillator/sub/noise (docs/design/01), filter
  (docs/design/02), envelope/LFO/VCA (docs/design/03), voice/control
  (docs/design/04), mixer/mod/arp/seq (docs/design/05), FX
  (docs/design/07). This doc fixes only their *position* in the graph and
  the *seam* they live behind.
- **Oversampling order/ripple, ladder engine choice** — deferred to their own ADRs
  per [ADR-001] Decision; this doc fixes only that oversampler state is core-owned
  and `prepare`-sized.
- **The central calibration table** — every `(PI)` constant named here resolves in
  `core/calibration/Calibration.h` (created by the backlog) [ADR-001].

## 2. System context

### 2.1 Product shape

The modeled instrument is a strictly serial **monophonic** SH-101 voice —
`MODULATOR | VCO | SOURCE MIXER | VCF | VCA` [research/01 §2.1, §3] — wrapped by a
modern plugin shell that adds polyphony/unison, drift, a post-voice FX section, and
five host formats. The faithful *voice* is the circuit model; everything layered
after it (poly/unison, FX) is a modern essential that sits **outside** the bit-exact
modeling contract [ADR-017 §Context].

### 2.2 Targets and toolchain

- Language/build: C++20, CMake, JUCE in the shell only [ADR-001 Decision].
- Reference / bless platform: **macOS arm64** (FP bit-exact). Co-required hard gate:
  **Linux x64** (integer bit-exact, FP tolerance-banded). Windows x64 is a goal tier.
  [ADR-001 §Context; ADR-023 §Context.]
- License: GPL-3.0-or-later.

### 2.3 Honesty stance

mwAudio101 is modeled from *documented* circuit behavior with **no physical-unit
oracle** [research/01 §1; INDEX honesty stance]. Numeric values trace to research or
an ADR; engineering inventions are tagged `(PI)` and centralized in
`core/calibration/Calibration.h`.

## 3. Module tree

### 3.1 Top-level layout

The repository splits into three layers with a strict one-way dependency:
`ui -> plugin -> core`; **core never depends up** [ADR-001 Decision, C1].

```text
core/        freestanding C++20 static lib "mwcore" — ZERO JUCE include/link
  dsp/       stateless/per-instance DSP primitives (osc, filter, VCA, noise, shapers, oversampler)
  voice/     the monophonic-circuit Voice + the preallocated voice pool + active-voice list
  fx/        post-voice, once-on-the-mono-sum FX (Drive -> Chorus -> Delay)
  params/    ParamSnapshot (normalized POD), control-rate smoothers, normalized->engineering mapping
  calibration/ Calibration.h — the single (PI) constants table + per-renderVersion frozen constant-sets
plugin/      JUCE shell "mwplugin" — drives core over the POD seam
  (APVTS, MIDI/transport marshalling -> BlockContext, setLatencySamples, state/version I/O, format wrappers)
ui/          JUCE editor / panel (MODULATOR | VCO | SOURCE MIXER | VCF | VCA + controller/seq/arp)
tests/       Catch2 headless binary linking mwcore ONLY (no JUCE, no plugin, no audio device)
```

### 3.2 Module responsibilities

| Module | Owns | Key files (backlog creates) | Owning design doc |
| --- | --- | --- | --- |
| `core/dsp` | Band-limited oscillator, sub-osc divider, noise, IR3109 ladder, BA662 VCA, ADSR, LFO, oversampler scratch | `core/dsp/Oscillator.h/.cpp`, `core/dsp/LadderFilter.h/.cpp`, `core/dsp/Vca.h/.cpp`, `core/dsp/Oversampler.h/.cpp` | 01, 02, 03 |
| `core/voice` | One `Voice` = full serial circuit; `VoicePool`; active-voice list; per-voice seeded drift PRNG | `core/voice/Voice.h/.cpp`, `core/voice/VoicePool.h/.cpp` | 04 (voice), 01–05 (stages) |
| `core/fx` | Post-voice mono FX: Drive (own 2x OS) -> Chorus -> Delay; master/per-block bypass | `core/fx/FxChain.h/.cpp`, `core/fx/Drive.h/.cpp` | 07 (FX) |
| `core/params` | `ParamSnapshot` POD; control-rate smoothers; normalized→engineering mapping vs Calibration | `core/params/ParamSnapshot.h`, `core/params/Smoother.h` | 06 (IDs/ranges) |
| `core/calibration` | `Calibration.h` `(PI)` table; per-`renderVersion` frozen constant-sets | `core/calibration/Calibration.h` | this doc + per-stage docs |
| `core` (engine) | The `Engine` seam: `prepare`/`process`/`reset`; sub-block chunking; voice loop; FX; FTZ/DAZ | `core/Engine.h/.cpp`, `core/BlockContext.h` | this doc (§5, §6) |
| `plugin/` | APVTS, parameter IDs, MIDI/transport→`BlockContext` marshalling, `setLatencySamples`, state/version I/O, 5 format wrappers | `plugin/PluginProcessor.h/.cpp`, `plugin/ParamBridge.h/.cpp` | 06 (params), this doc (§7, §8) |
| `ui/` | Editor, panel layout, opt-in engine-update affordance | `ui/PluginEditor.h/.cpp` | 10 (UI) |
| `tests/` | Headless Catch2 tiers (property, no-alloc/no-lock, determinism, golden, lifecycle/fuzz, CPU-budget) | `tests/...` | 11 (testing/build/CI) |

### 3.3 Build-enforced boundary

A CI "no-JUCE-in-core" guard FAILS the build if any `core/` translation unit
includes `<juce_*>` or references `JUCE_`, or if `mwcore`'s link interface contains
any JUCE target [ADR-001 C1]. FP flags are pinned on the core target: `-ffast-math`
**OFF**, `-ffp-contract=off` (`/fp:precise` on Windows) [ADR-001 C12]. `-fno-exceptions`
is NOT used globally; the core relies on `noexcept` hot paths instead [ADR-001 Decision].

## 4. System signal flow

### 4.1 End-to-end graph

The modeled voice is strictly serial [research/01 §2.1, §11]; the shell adds
poly/unison and a post-voice FX section [ADR-017 §Decision].

```text
MIDI/transport/params (host)
        │   marshalled in plugin/ -> POD BlockContext (§5.3)
        ▼
Engine::process(BlockContext)            [core, single-threaded, ADR-019 VT-01]
        │
        ├─ for each active Voice (fixed index order, ADR-019 VT-02):
        │     VCO (saw + variable-width pulse, PWM)            [docs/design/01]
        │       + Sub-osc (÷2 sq / ÷4 sq / ÷4 25% pulse)       [docs/design/01; research/01 §3.2]
        │       + Noise (white, -3 dB ~16 kHz)                 [docs/design/01; research/01 §3.3]
        │       ▼
        │     SOURCE MIXER (4 indep. gains: Pulse|Saw|Sub|Noise) [docs/design/05; research/01 §3.4]
        │       ▼
        │   ┌── PER-VOICE 2x-OVERSAMPLED NONLINEAR ZONE ──────┐ [ADR-017 L1, L9]
        │   │   IR3109 ladder (4-pole, 24 dB/oct, self-osc)    [docs/design/02; research/01 §4]
        │   │     + diode-clamp resonance limiter              [research/01 §4.3]
        │   │     + BA662 VCA drive (tanh)                     [docs/design/03; research/01 §3.6]
        │   └──────────────────────────────────────────────────┘
        │       ▼
        │     VCA (ENV / GATE mode)                            [docs/design/03; research/01 §3.6]
        │       ▼ accumulate into block mix (fixed order)
        ▼
  MONO VOICE SUM (post poly/unison/drift)
        ▼
  POST-VOICE FX CHAIN (once on the mono sum, ADR-017 §Decision):
     Drive (own dedicated 2x OS, ADR-017 L2) -> Chorus -> Delay   [docs/design/07]
        ▼
  AudioBlockView output  (FX-off == blessed bit-exact, ADR-017 L6)
```

### 4.2 Control / modulation sources

Per research/01 §2.2 and §11, two control sources feed the voice and are positioned
(not defined) here; their laws live in docs/design/03 and docs/design/05:

- **ADSR** — one envelope shared by VCF (bipolar amount + polarity) and VCA
  [research/01 §6.1].
- **MODULATOR (LFO/CLK)** — triangle/square primitives (no sine panel position;
  sine is bender-path only), plus CPU-style random S&H and band-limited noise; its
  square doubles as the arp/seq clock with key-press clock-reset
  [research/01 §5, §9.4].

### 4.3 What is inside vs outside the modeling contract

- **Inside the bit-exact modeled-voice contract:** VCO, sub, noise, mixer, IR3109
  ladder + diode-clamp resonance + BA662 VCA drive, VCA, ADSR, LFO — i.e. the
  per-voice serial circuit, including the constant group delay of the per-voice
  oversampled zone [ADR-017 L1, L6; research/01 §11].
- **Outside that contract (modern essentials):** poly/unison, per-voice drift, and
  the post-voice FX section (Drive/Chorus/Delay) [ADR-017 §Context, L2, L9].

### 4.4 Internal sub-blocking

`process` splits the host block at MIDI/event sample-offsets and renders each
segment in fixed-size internal chunks capped at `kRenderBlock` (a `(PI)` constant,
~32 frames) [ADR-001 Decision, C6]. Control-rate parameter ticks (smoothing) align
to chunk boundaries, modeling the SH-101's coarse control-loop cadence
[ADR-001 Decision]. Audio-rate render runs underneath each chunk.

## 5. Core / plugin boundary and the processing seam

### 5.1 The three-call engine surface

The entire DSP core is driven by one small, value-typed seam, shared **identically**
by the shell and every test [ADR-001 Decision]. File: `core/Engine.h`.

```cpp
namespace mw {

class Engine {
public:
    // The ONLY place allocation, table-building, oversampler scratch/ratio
    // selection, voice-pool construction, and buffer sizing happen.
    // Called off the audio thread from the shell's prepareToPlay.
    // Idempotent and re-callable on sample-rate / block-size change. [ADR-001 C2]
    void prepare(double sampleRate, int maxBlockSize, int maxVoices) noexcept;

    // Pure render. Touches ONLY pre-sized member storage. Hot path. [ADR-001 C3-C6]
    void process(const BlockContext& ctx) noexcept;

    // Clears state to a known start. No allocation. [ADR-001 Decision]
    void reset() noexcept;
};

} // namespace mw
```

`prepare` is called from the shell's `prepareToPlay`; `process` from the audio
callback; `reset` on transport reset / re-init. `maxVoices` is the worst-case voice
cap (`kMaxVoices >= maxPoly * maxUnison`, `maxUnison <= 8`), owned by the voice ADR
and referenced here; the pool and all scratch are sized to it once [ADR-001 C2; voice
ADR cited in ADR-019 §Context].

### 5.2 No JUCE types cross the seam

`juce::AudioBuffer`, `juce::MidiBuffer`, and `APVTS` MUST NOT appear in any `core/`
header or cross the seam [ADR-001 Decision, C1, C14]. The shell marshals host
representations into the POD `BlockContext` in a thin, separately tested adapter
(`plugin/ParamBridge`).

### 5.3 BlockContext (POD aggregate)

`BlockContext` is a non-owning, value-typed aggregate — the single argument to
`process` [ADR-001 Decision]. File: `core/BlockContext.h`.

```cpp
namespace mw {

// Non-owning view over the host's channel pointers for this block.
struct AudioBlockView {
    float* const* channels;   // borrowed; not owned by core
    int            numChannels;
    int            numFrames;
};

// POD transport snapshot for this block (host-decoded; no JUCE types).
struct TransportInfo {
    double bpm;
    double ppqPosition;
    bool   isPlaying;
    double sampleRate;
};

// Host-decoded, sample-offset-timestamped event (note/CC/MPE). POD.
// Defined as mw::core::MidiEvent by the formats/IO/MIDI doc (docs/design/09),
// which also owns the HostEvent -> mw::core::MidiEvent translation in plugin/.
struct MidiEvent {            // == mw::core::MidiEvent, defined in docs/design/09
    NormalizedType type;      // event kind (note/CC/MPE), per docs/design/09
    int8_t         channel;
    int16_t        noteId;
    float          data0;
    float          value;
    int            sampleOffset;   // within the current block
};

// Non-owning span of events for this block, ordered by sampleOffset.
struct MidiEventView {
    const MidiEvent* events;
    int              numEvents;
};

struct BlockContext {
    AudioBlockView       audio;     // output target (and any aux), borrowed
    const ParamSnapshot* params;    // immutable, snapshotted once per block (§5.4)
    TransportInfo        transport;
    MidiEventView        midi;       // non-owning span; sample-accurate offsets
};

} // namespace mw
```

`BlockContext` and its members are PODs with no owning allocation and no JUCE
dependency. The core dereferences borrowed pointers only; it never copies, grows, or
frees them [ADR-001 C3].

### 5.4 Parameter inversion

Parameters are inverted across the seam [ADR-001 Decision, C7]:

1. The shell reads APVTS atomics **once per block** into an immutable, normalized
   (`[0,1]` / typed-enum) `ParamSnapshot` (`core/params/ParamSnapshot.h`). The core
   never reads `std::atomic` in tight loops.
2. The core maps **normalized → engineering units** internally against
   `core/calibration/Calibration.h`, then drives control-rate smoothers
   (`core/params/Smoother.h`) so automation never zippers.

Parameter IDs, ranges, units, skews and enum orders are **defined in docs/design/06
§2** [ADR-008]; this doc neither lists nor re-mints them. `ParamSnapshot`'s field set
mirrors that schema and is generated/validated against it.

### 5.5 prepare / process / reset lifecycle invariants

- `prepare`: non-RT; the ONLY allocator. Sizes buffers, voice pool, oversampler +
  decimator scratch, coefficient and per-sample-rate compensation tables, padding
  delay lines (§7.3), and seeds per-voice drift PRNG state [ADR-001 Decision, C2].
  Re-callable; idempotent on sample-rate / block-size change.
- `process`: RT; `noexcept`; touches only pre-sized storage; FTZ/DAZ set at entry;
  obeys §9 invariants in full [ADR-001 C3-C6, C11].
- `reset`: RT; `noexcept`; clears state to a known start; no allocation
  [ADR-001 Decision].

## 6. Threading model

### 6.1 Single-threaded voice rendering

For v1, **all per-voice rendering is single-threaded inside `process`**. The
audio-callback thread walks the active-voice list and renders every active voice for
each `kRenderBlock` chunk on one thread, accumulating into the block mix in **fixed
voice-index order**, then runs the shared FX chain — all inside the single
`process(const BlockContext&) noexcept` call. No worker threads, no thread pool, no
cross-thread voice handoff, and therefore **no synchronization primitive of any kind**
participates in voice rendering [ADR-019 VT-01, VT-02, VT-03].

### 6.2 Why fixed-order single-thread

- Satisfies the no-lock invariant by construction: nothing to synchronize
  [ADR-019 VT-03; ADR-001 C4].
- Bless-stable: fixed-order summation fixes the (non-associative) FP reduction order,
  so output is FP bit-exact on macOS arm64 and tolerance-banded on Linux x64
  [ADR-019 VT-04; ADR-001 C8, C9].
- Matches the budgeted cost model: worst-case CPU = `active voices * per-voice
  2x-oversampled cost + shared FX`, on one thread [ADR-019 VT-05].

### 6.3 Cost gate and the path to change

A per-block CPU-budget regression assertion guards the worst case in CI (max poly ×
max unison × 2x, high resonance, oversampling on) [ADR-019 VT-05; ADR-001 C10].
Multi-threaded voice rendering is reconsidered ONLY if that gate FAILS with headroom
on **both** macOS arm64 and Linux x64 and cannot be recovered by cheaper per-voice
DSP or a lower voice cap; any such move requires a **superseding ADR** that re-proves
the no-lock invariant and fixed-order determinism, and triggers a golden rebless
[ADR-019 VT-06, VT-07].

### 6.4 Thread ownership of non-audio work

`renderVersion` read/write, frozen-constant-set selection, per-sample-rate table
regeneration, and the engine-update opt-in flag run on the **message thread** or at
`prepareToPlay` only — never on the audio thread [ADR-023 V18; ADR-001 C2].

## 7. Latency and plugin-delay-compensation (PDC)

### 7.1 Constant reported latency

The plugin reports a **single CONSTANT total latency** to the host via
`setLatencySamples`, declared from `plugin/` [ADR-017 §Decision; ADR-001]. It is
**independent of FX on/off and independent of the Quality tier (1x/2x/4x)**; shorter
configurations are delay-aligned up to the worst case so the reported number never
changes for the plugin instance's lifetime [ADR-017 L4, L5]. Bounce/automation
alignment and preset recall therefore never shift, and the host never re-launches PDC
on an FX toggle or Quality switch [ADR-017 §Decision, L7, L8].

### 7.2 What contributes to reported PDC

| Source | Contributes? | Rule |
| --- | --- | --- |
| Per-voice oversampled-zone group delay (IR3109 ladder + diode-clamp resonance + BA662 VCA drive; realtime polyphase IIR halfband) | YES | Fixed, measured; part of the blessed FX-off bit-exact contract; identical on macOS arm64 and Linux x64 [ADR-017 L1] |
| FX Drive 2x-oversampling group delay (own post-voice up/down pair) | YES | Always counted, even when Drive is bypassed (constant-padded) [ADR-017 L2] |
| FX Delay musical time + Chorus modulation delay | NO | Intended musical delay; never reported, never host-compensated [ADR-017 L3] |

### 7.3 Computation and RT-safety

The worst-case total group delay across all FX/Quality combinations is computed once
in `prepare`; shorter configurations are padded up via preallocated fixed delay lines
on the short paths; the constant is reported via `setLatencySamples` from `plugin/`
[ADR-017 L4, L10]. The reported value is never recomputed or mutated on the audio
thread [ADR-017 L10; ADR-001 C2]. It is held constant build-to-build; any change is a
deliberate, reviewed event with a golden rebless [ADR-017 L11].

### 7.4 FX-off reference invariant

With all FX bypassed, output is FX-off bit-exact (sample-identical to the blessed mono
voice on the macOS arm64 reference) at the declared worst-case offset; the per-voice
IIR-zone group delay sits **inside** the golden, not subtracted [ADR-017 L6].

## 8. Engine versioning and blessed sample-rate set

### 8.1 Two version concepts

The state root carries two independent version concepts beyond the marketing
`pluginVersion` and the state-shape `schemaVersion` (the latter owned by docs/design/06
/ [ADR-008]):

- **`engineVersion`** — a human-facing `MAJOR.MINOR.PATCH` string, informational only;
  it MUST NOT trigger state migration. MAJOR = intentional sonic redesign; MINOR =
  audio-altering change that bumps `renderVersion`; PATCH = proven not to alter any
  blessed artifact [ADR-023 V1, V2].
- **`renderVersion`** — a monotonically increasing integer, the bless-affecting
  contract "these parameters render these samples." It increments **iff** a bless
  changes any CLASS-EXACT artifact hash or moves a CLASS-FP artifact outside its
  tolerance band [ADR-023 V3, V5]. It is **orthogonal** to `schemaVersion`: a pure DSP
  re-tune bumps `renderVersion` only; a parameter-shape change bumps `schemaVersion`
  only [ADR-023 V4].

### 8.2 No silent audio change

On load, if a session's `renderVersion < CURRENT_RENDER_VERSION`, the plugin keeps
rendering at the session's stored `renderVersion` (the **legacy-render path**) and
MUST NOT change its audio without explicit user opt-in; it raises a non-modal,
message-thread "update engine (audio will change)" affordance whose decline is sticky
[ADR-023 V8, V9]. New/blank sessions and new factory presets author at `CURRENT`
[ADR-023 V9].

### 8.3 Legacy-render path and frozen constant-sets

A pinned old `renderVersion` reproduces that version's audio within its original
tolerance tier by selecting the **frozen constant-set** (tanh approximation, decimator
coefficients, compensation-table contents) tagged with that `renderVersion`, chosen at
`prepareToPlay`, never at audio rate [ADR-023 V10, V18]. These constant-sets live under
`core/calibration/` keyed by `renderVersion`. Only shipped `renderVersion`s are
retained [ADR-023 §Consequences].

### 8.4 Blessed sample-rate set

Golden corpora (CLASS-EXACT and CLASS-FP) are generated at each of
**{44100, 48000, 88200, 96000} Hz**, each keyed by sample rate; the per-sample-rate
compensation table is generated for each at `prepareToPlay` [ADR-023 V12]. At a host
SR in the set, the engine runs the normal blessed path [ADR-023 V13].

### 8.5 Behavior above the blessed set

A host SR strictly above the set (e.g. 176.4/192 kHz) is **supported but unblessed**:
it runs the same engine with a per-SR table for that exact rate, validated under the
FP-tolerance tier only — never asserted bit-exact, never blessed [ADR-023 V14]. When
2x oversampling would push the internal rate above `OS_CEILING_HZ` (192 kHz internal =
2x the top blessed rate), the oversampling factor is **clamped to 1x** so the
filter-stability guard continues to hold; the clamp is recorded in engine-tag/MANIFEST
provenance and surfaced in the UI as "running unblessed at this host rate"
[ADR-023 V15, V16]. Rates below 44.1 kHz are out of scope: resampled by the host,
neither blessed nor specially clamped [ADR-023 V17].

## 9. Global real-time invariants

### 9.1 The audio-thread contract

These hold on every hot path (`process`, `reset`) and are mechanically enforced, not
aspirational [ADR-001 §"Real-time invariants", C3-C5, C11]:

| # | Invariant | Enforcement |
| --- | --- | --- |
| RT-1 | No heap allocation | `AudioThreadGuard` armed global `operator new` fails the test [ADR-001 C3] |
| RT-2 | No locks / mutex / condition variable / atomics-as-locks / spin | `AudioThreadGuard` lock sentinel; single-threaded voice loop (§6) means nothing to lock [ADR-001 C4; ADR-019 VT-03] |
| RT-3 | No syscalls, no unbounded loops | Bounded solver iterations (fixed max), bounded chunking [ADR-001 Decision, C10] |
| RT-4 | No thrown exceptions on the hot path | Hot path is `noexcept`; an escaped throw calls `std::terminate`, caught as a test crash [ADR-001 C5] |
| RT-5 | Denormals flushed (FTZ/DAZ) set at `process` entry | Self-oscillating / decay tails never enter denormal stall [ADR-001 C11] |
| RT-6 | All sizing in `prepare` | Worst-case `maxVoices * maxBlockSize * oversample` scratch + padding delay lines allocated once [ADR-001 C2; ADR-017 L10] |
| RT-7 | Determinism | Same seed + same `BlockContext` event/param sequence ⇒ bit-identical on integer/deterministic paths; FP analog stages bit-exact on macOS arm64, `max abs <= 1e-6` elsewhere [ADR-001 C8, C9; ADR-019 VT-04] |

### 9.2 Determinism mechanics

Per-voice "analog" drift uses **pre-seeded per-voice PRNG state**, sized and seeded in
`prepare`, so randomness is reproducible and goldens stay bit-stable [ADR-001 Decision;
research/01 §11 cross-ref drift doc]. Integer/deterministic paths (sub-osc divider,
arp/seq step counters, clock) are bit-exact by construction across platforms; the
floating-point analog stages are blessed on macOS arm64 and tolerance-compared elsewhere
[ADR-001 Decision, C8, C9].

### 9.3 Headless testability

The unit-test binary links `mwcore` ONLY — no JUCE, no plugin, no audio device — so it
builds and runs on the Linux hard gate and the macOS bless box [ADR-001 C13]. The same
POD render path is exercised by tests and the shell, so blessed goldens ARE what ships
(no test-only path) [ADR-001 §Pros].

## 10. Acceptance hooks

Objectively-testable properties a backlog task's tests must verify against this doc:

- **Boundary guard (RT-2/§3.3):** CI fails if any `core/` translation unit includes
  `<juce_*>` / references `JUCE_`, or if `mwcore`'s link interface contains a JUCE
  target [ADR-001 C1].
- **Seam shape (§5.1/§5.3):** the engine exposes exactly `prepare(double,int,int)
  noexcept`, `process(const BlockContext&) noexcept`, `reset() noexcept`; `BlockContext`
  is a POD with no JUCE type and no owning allocation; no `juce::AudioBuffer` /
  `juce::MidiBuffer` / `APVTS` appears in any `core/` header [ADR-001 C2, C14].
- **No-alloc / no-lock (§9.1):** an `AudioThreadGuard`-wrapped representative `process`
  performs zero heap allocations and acquires zero locks [ADR-001 C3, C4].
- **noexcept hot path (RT-4):** `process` and `reset` are `noexcept`; an injected throw
  is caught as a crash, not unwound [ADR-001 C5].
- **Sub-block accuracy (§4.4):** events at sample offsets apply sample-accurately;
  segments render in fixed `kRenderBlock`-capped chunks [ADR-001 C6].
- **Parameter inversion (§5.4):** the core reads no `std::atomic` in tight loops; a
  step change in a normalized parameter produces a smoothed (non-zippered) engineering
  value [ADR-001 C7].
- **Single-thread fixed order (§6):** voice rendering uses no synchronization primitive;
  voices sum in fixed voice-index order; the `AudioThreadGuard` lock sentinel never trips
  from the voice loop [ADR-019 VT-01, VT-02, VT-03].
- **Determinism (RT-7/§9.2):** identical seed + identical `BlockContext` sequence yields
  bit-identical output on integer/deterministic paths and across macOS/Linux; FP analog
  stages within `max abs <= 1e-6` off-reference [ADR-001 C8, C9; ADR-019 VT-04].
- **FTZ/DAZ (RT-5):** denormals are flushed at `process` entry; a self-oscillating /
  long-decay tail does not enter a denormal CPU stall [ADR-001 C11].
- **Lifecycle/fuzz (§5.5):** `prepare`-then-`process` over random valid block sizes
  (`<= maxBlockSize`) and random valid params never allocates and never trips the guards
  [ADR-001 C2; §5.5].
- **CPU-budget (§6.3):** the stress configuration (max poly × max unison × 2x, high
  resonance, oversampling on) stays within the per-block CPU budget on macOS arm64 and
  Linux x64 [ADR-001 C10; ADR-019 VT-05].
- **Constant PDC (§7):** reported `setLatencySamples` is invariant to master/per-block FX
  bypass, to the Quality tier (1x/2x/4x), and build-to-build; it equals the worst-case
  total group delay across all combinations; it is sized in `prepare` and never mutated
  from `process` [ADR-017 L4, L5, L7, L8, L10, L11].
- **FX-off bit-exact (§7.4):** with all FX bypassed, output is sample-identical to the
  blessed mono voice on the macOS arm64 reference at the declared worst-case offset; the
  per-voice IIR-zone group delay is inside the golden, not subtracted [ADR-017 L6].
- **Version orthogonality (§8.1):** the state root carries an informational
  `engineVersion` (no migration) and an integer `renderVersion`; a DSP-only re-tune bumps
  `renderVersion` without a no-op migration; a parameter-shape change bumps
  `schemaVersion` without touching `renderVersion` [ADR-023 V1, V3, V4].
- **No silent audio change (§8.2):** loading a session with `renderVersion < CURRENT`
  renders on the legacy path and does not change audio without opt-in [ADR-023 V8, V9, V10].
- **Blessed sample-rate set (§8.4/§8.5):** goldens exist at {44100, 48000, 88200, 96000}
  Hz keyed by SR; a host SR above the set runs unblessed (FP-tolerance only) and clamps to
  1x oversampling above `OS_CEILING_HZ` (192 kHz internal), with the clamp recorded in
  provenance and surfaced in the UI [ADR-023 V12, V14, V15, V16].

## 11. References

ADRs (normative contracts):

- [ADR-001] DSP core / plugin-shell boundary and real-time contract —
  plan/decisions/001-core-plugin-boundary.md
- [ADR-017] Plugin latency (PDC) policy & Drive placement —
  plan/decisions/017-plugin-latency-pdc-and-drive-placement.md
- [ADR-019] Voice-rendering threading model —
  plan/decisions/019-voice-rendering-threading-model.md
- [ADR-023] Engine versioning, bless communication & blessed sample-rate set —
  plan/decisions/023-engine-versioning-and-blessed-sample-rates.md
- [ADR-008] Parameter / state / preset schema and versioning (referenced for
  parameter-ID/schema/`schemaVersion` ownership) —
  plan/decisions/008-parameter-state-preset-schema.md

Research (cited factual ground truth):

- [research/01] SH-101 Architecture & Signal Flow —
  docs/research/01-architecture-signal-flow.md

Sibling design docs referenced (owners of their subsystems; not redefined here):

- docs/design/01 (VCO/sub/noise), 02 (IR3109 filter), 03 (envelope/LFO/VCA),
  04 (voice/control), 05 (modulation/arp/seq), 06 (parameter schema —
  IDs/ranges/units), 07 (FX), 09 (formats/IO/MIDI — owns mw::core::MidiEvent and
  the HostEvent->MidiEvent translation), 10 (UI), 11 (testing/build/CI).
