<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Voice, Polyphony/Unison & Control Model

## 1. Scope and authority

### 1.1 What this document owns

This document is the single source of truth for the voice-assembly,
polyphony/unison, note-priority/key-assignment, glide, and control-rate model of
mwAudio101. Backlog tasks cite it by section number (for example
"implement per docs/design/04-voice-and-control.md §5.2"). It is
implementation-ready: it gives concrete C++ class/struct/function signatures,
data layouts, parameter tables, and the normative note-priority/retrigger
contract.

It owns and specifies:

- `Voice` — assembly of the per-voice circuit-accurate signal path plus
  per-voice drift state [ADR-006 §Decision item 1].
- `KeyAssigner` — the bit-faithful firmware key-scan / note-priority / retrigger
  state machine; the sole note-priority authority for MONO and UNISON
  [ADR-006 §Decision item 2; research/07 §3.2-§3.3, §11].
- `VoiceManager` — owns the fixed `Voice[kMaxVoices]` pool and switches between
  MONO / UNISON / POLY [ADR-006 §Decision item 3].
- `Glide` (portamento) — per-voice glide of the pitch target
  [research/05 §5].
- `ControlCore` — the shared time-muxed 6-bit-DAC/4052/S&H control object and the
  Vintage-vs-Modern control-rate model [ADR-005; ADR-016 §Decision item 1].

### 1.2 What this document references but does NOT define

- **Parameter IDs, ranges, defaults, skews, and the APVTS schema** are owned by
  doc 06 (the parameter schema) per ADR-008. This document names the *behavioral*
  parameters it consumes (mode, unison count, detune, glide time, vintage-control
  macro) and gives their *numeric semantics*, but the canonical parameter-ID
  strings, the host-facing ranges/skews, and the smoothing policy are doc 06's
  (see docs/design/06 §2). Where a numeric range is shown here it is the
  behavioral contract for this subsystem; the host parameter that drives it is
  doc 06's to mint.
- **Per-voice DSP blocks** — CEM3340 VCO + sub + noise (doc 01), IR3109 VCF
  (doc 02), BA662A VCA, ADSR, LFO (doc 03 — envelope/LFO/VCA) — are defined
  in their own design docs. This document specifies only how `Voice` *assembles*
  and *drives* them and what state it owns.
- **Vintage variance / drift DSP** (the random-walk law, three-tier model,
  `vintage.age` macro) is owned by ADR-009 and its design doc. This document owns
  only the per-voice *seed derivation* and *where the drift state lives*
  [ADR-006 §Decision item 1, C18].
- **Arpeggiator / 100-step sequencer and MPE-lite** plug into the
  KeyAssigner/allocator boundary defined here (§9) but their internal behavior is
  owned by their own ADRs/design docs (ADR-007, ADR-012, ADR-022).
- **The `process(const BlockContext&) noexcept` seam, `kRenderBlock` chunking,
  and the `AudioThreadGuard` no-alloc/no-lock contract** are owned by ADR-001;
  the single-threaded voice render order is owned by ADR-019. This document
  conforms to them (§8).

### 1.3 Calibration / pragmatic-invention policy

Every substantive numeric or behavioral claim carries a trace citation to a
research section or ADR. Where a value is an engineering invention not present in
research, it is tagged **(PI)** (pragmatic invention) and MUST be defined as a
named constant in the single calibration table `core/calibration/Calibration.h`
(created by the backlog), never hard-coded inline. (PI) values in this document
are: the MODERN control sub-block tick length (§7.5), the steal fade length
(§6.4), the re-strike pitch-match window, and the unison stereo-spread
distribution law (§5.3). They are tuning constants with no hardware analog and
are labeled as modern additions per ADR-013, not asserted as SH-101 fidelity.

## 2. Module map and files

The backlog creates these files. All headers expose the signatures in §3-§7.

| File | Contents | Defining section |
| --- | --- | --- |
| `core/voice/Voice.h` / `.cpp` | `Voice` value-type aggregate; assembles VCO/sub/noise/VCF/VCA/ADSR/LFO + drift; `prepare`, `noteOn`, `noteOff`, `setGlideTarget`, `render`, `isActive` | §4 |
| `core/voice/VoiceManager.h` / `.cpp` | `VoiceManager`; owns `Voice[kMaxVoices]`; mode dispatch; poly allocator; active-voice list | §6 |
| `core/voice/KeyAssigner.h` / `.cpp` | `KeyAssigner`; held-note bitset/stack; coupled `GateTrigMode`; lowest/last-note resolution; emits `NoteDecision` | §5 |
| `core/voice/Glide.h` / `.cpp` | `Glide`; per-voice portamento slew on the pitch target | §5.5 |
| `core/control/ControlCore.h` / `.cpp` | `ControlCore`; shared 6-bit-DAC/4052/S&H model; Vintage/Modern pitch domain; control tick | §7 |
| `core/voice/VoiceTypes.h` | shared PODs: `NoteDecision`, `GateTrigMode`, `VoiceMode`, `VoiceState`, `NoteEvent`, `kMaxVoices`, `kMaxUnison`, `kMaxPoly` | §3 |

`KeyAssigner` is verified against a disassembly-semantics reference in
`test/golden/KeyAssignerReference.{h,cpp}` (the golden-trace reference,
created by the backlog) [ADR-006 §Decision item 2, C19].

## 3. Shared types and constants (`VoiceTypes.h`)

### 3.1 Compile-time pool sizing

```cpp
namespace mw {

inline constexpr int kMaxUnison = 8;   // maxUnison cap [ADR-006 §3, C9]
inline constexpr int kMaxPoly   = 8;   // (PI) v1 poly cap; calibration constant
inline constexpr int kMaxVoices = kMaxPoly * kMaxUnison;  // = 64 [ADR-006 §3]

} // namespace mw
```

`kMaxVoices >= maxPoly x maxUnison` is the ADR-006 §3 invariant; `kMaxPoly = 8`
is a **(PI)** v1 choice sized generously per ADR-006 (raising it is a recompile,
not a runtime knob [ADR-006 §Consequences]). All three live in
`core/calibration/Calibration.h` so the pool size is centralized.

### 3.2 Modes and coupled selector

```cpp
namespace mw {

// Voice-manager mode. MONO is default/bless target [ADR-016 R-3].
enum class VoiceMode : uint8_t { Mono = 0, Unison = 1, Poly = 2 };

// The single coupled S7 selector: priority AND retrigger are one control.
// There is deliberately NO separate priority param and NO separate retrigger
// toggle [research/07 §3.2-§3.3, §11; ADR-006 §Decision item 2, C].
enum class GateTrigMode : uint8_t {
    Gate     = 0,  // lowest-note priority, NO legato retrigger
    GateTrig = 1,  // last-note priority, retrigger every key
    Lfo      = 2   // lowest-note priority, ADSR (re)triggered by clock H->L edge
};

// Per-voice lifecycle state for the active-voice scan [ADR-006 §4].
enum class VoiceState : uint8_t {
    Idle = 0,      // not sounding; skipped by the render loop
    Active = 1,    // gate asserted, sounding
    Releasing = 2, // gate de-asserted, release tail finishing in place
    Stealing = 3   // forced fast-fade before reuse (poly only) [ADR-006 C15]
};

} // namespace mw
```

### 3.3 Note events and decisions

`NoteEvent` is the engine-internal, already-demultiplexed MIDI event the
VoiceManager consumes (translation from raw MIDI / MPE / arp-seq is owned by
ADR-012/022). `NoteDecision` is the KeyAssigner's only output for the mono path.

```cpp
namespace mw {

struct NoteEvent {
    enum class Type : uint8_t { NoteOn, NoteOff, AllNotesOff };
    Type    type;
    uint8_t note;       // 0..127 MIDI note
    float   velocity;   // 0..1 normalized; routed to VCA/VCF when velocity ON [ADR-016 R-2]
    int     sampleOffset; // sample-accurate offset within the block
};

// The ONLY thing the mono Voice consumes from note logic [ADR-006 §Decision item 1].
struct NoteDecision {
    int   activeNote = -1;   // resolved MIDI note, or -1 if no note held
    bool  gate       = false;// gate asserted?
    bool  retrigger  = false;// fire ADSR from trigger state this tick?
    bool  clockReset = false;// assert CLOCK RESET (LFO/ARP re-phase) [research/07 §5.2]
};

} // namespace mw
```

### 3.4 Real-time invariants for this subsystem

- The pool, all per-voice DSP scratch, drift state, and the KeyAssigner's
  bitset/stack are sized and allocated in `prepare`; the audio thread only
  activates/idles existing voices [ADR-006 §4, C17].
- Every hot-path method (`render`, `noteOn`, `noteOff`, `tick`) is `noexcept`,
  performs no heap allocation, takes no lock, and contains no unbounded loop
  [ADR-001; ADR-019 VT-03].
- Mode and voice-count changes happen only at `prepare` or via a lock-free flag
  read at a block boundary, never mid-block [ADR-006 C17].
- All allocation/stealing is an O(kMaxVoices) integer-comparison scan run once
  per note event, never per sample [ADR-006 §4, C14].

## 4. `Voice` — the circuit-accurate signal chain (`Voice.h/.cpp`)

### 4.1 Responsibility

A `Voice` is the SH-101 signal path *only* plus an inline per-voice drift block.
It has **no knowledge of polyphony or note priority**: it consumes a resolved
note, a gate, a retrigger flag, and a glide target, and renders audio. Per
ADR-006 §Decision item 1 and research/04 §2.1/§3.1, each `Voice` owns **exactly
one** ADSR and **one** LFO — the modulation section is per-voice, never
globalized.

The original is monophonic with one shared EG and one LFO [research/04 §2.1,
§3.1]; polyphony is therefore N independent copies of that one-EG/one-LFO
topology [ADR-006 §Context, §Decision item 1].

### 4.2 Data layout

`Voice` is a flat value type (no virtual dispatch in the inner loop), laid out
cache-friendly. The per-voice DSP objects are the design docs' types; this
document fixes only their membership and the drift block.

```cpp
namespace mw {

struct VoiceDrift {                 // inline per-voice drift state [ADR-006 §Decision item 1]
    uint32_t seed = 0;              // derived from voiceIndex + instanceSeed (§4.4)
    XorShift32 rng;                 // deterministic PRNG; never wall-clock [ADR-006 C18]
    dsp::OnePoleSmoother tuneWalk;   // slow random-walk -> tuning drift (mw::dsp::OnePoleSmoother)
    dsp::OnePoleSmoother pwWalk;     // -> pulse-width drift
    dsp::OnePoleSmoother cutoffWalk; // -> cutoff drift
    // DSP law (random-walk coefficients, depth scaling) is owned by ADR-009.
};

class Voice {
public:
    void prepare (double sampleRate, int oversampleFactor,
                  int voiceIndex, uint32_t instanceSeed) noexcept;

    // Note lifecycle. retrigger=true fires the ADSR from its trigger state.
    void noteOn  (int midiNote, float velocity, bool retrigger) noexcept;
    void noteOff () noexcept;                       // gate de-assert -> release
    void setGlideTarget (float targetPitchHz) noexcept;  // glide handled per-voice (§5.5)

    // Unison voices are configured once per note via these (§5.3).
    void setDetuneCents   (float cents) noexcept;
    void setStereoPan     (float pan)   noexcept;   // -1..+1

    // Render `numSamples` of this voice into `outL/outR`, accumulating.
    // Runs per-sample / oversampled internally; control state is set externally.
    void render (float* outL, float* outR, int numSamples) noexcept;

    // Forced fast fade for a poly steal (§6.4); flips state to Stealing.
    void beginSteal () noexcept;

    VoiceState state() const noexcept { return state_; }
    bool       isActive() const noexcept { return state_ != VoiceState::Idle; }
    int        currentNote() const noexcept { return currentNote_; }
    float      currentLevel() const noexcept;       // VCA/env level for quietest-steal [ADR-006 C14]
    uint64_t   noteSerial() const noexcept { return noteSerial_; } // for steal ordering [ADR-006 C14]

private:
    // --- modulation section: ONE each per Voice [research/04 §2.1, §3.1] ---
    Adsr    env_;       // single shared ADSR (-> VCF, VCA, PW) [research/04 §2.1]
    Lfo     lfo_;       // single LFO [research/04 §3.1]
    // --- signal path (defined in their own design docs) ---
    Oscillator vco_;    // CEM3340 + sub + noise (doc 01)
    Filter     vcf_;    // IR3109 ladder, 2x oversampled (doc 02, ADR-003)
    Vca        vca_;    // BA662A OTA
    Glide      glide_;  // per-voice portamento (§5.5)
    VoiceDrift drift_;  // inline drift state (§4.4)

    VoiceState state_      = VoiceState::Idle;
    int        currentNote_= -1;
    uint64_t   noteSerial_ = 0;     // set by VoiceManager on allocation [ADR-006 C14]
    float      stealGain_  = 1.0f;  // fast-fade ramp during Stealing
};

} // namespace mw
```

### 4.3 Render contract

- `render` is `noexcept`, allocation-free, lock-free [ADR-001; ADR-019 VT-01].
- When `state_ == Idle` the VoiceManager skips the voice entirely; idle slots
  cost nothing [ADR-006 §4].
- A `Releasing` voice keeps rendering until its ADSR release reaches the silence
  threshold, then self-transitions to `Idle`; release tails finish in place
  [ADR-006 §Decision item 3 POLY, C15].
- A `Stealing` voice applies the fast-fade ramp `stealGain_` (§6.4) and
  transitions to `Idle` when the ramp completes, at which point the slot is
  reused.
- Velocity (when ON, the default [ADR-016 R-2]) scales VCA level and adds to VCF
  cutoff amount; the routing nodes are owned by ADR-012/the VCA-ADSR doc, this
  doc only passes `velocity` through `noteOn`.

### 4.4 Per-voice drift seed (determinism)

The drift seed is derived **only** from `voiceIndex` and a per-instance
`instanceSeed`, never from wall-clock, so renders are byte-stable on the macOS
arm64 bless gate and the Linux x64 co-gate [ADR-006 §Decision item 1, C18;
ADR-019 VT-04].

```cpp
// In Voice::prepare. Mixing function is a (PI) constant set; see Calibration.h.
drift_.seed = hashCombine (instanceSeed, static_cast<uint32_t> (voiceIndex));
drift_.rng.reseed (drift_.seed);
```

Each unison voice therefore carries a distinct deterministic drift seed, making
detune sound like real analog beating rather than a static pitch fan
[ADR-006 §Decision item 3 UNISON, C10]. The drift *DSP* (walk coefficients,
`vintage.age` scaling) is ADR-009's; only the seed derivation is owned here.

## 5. `KeyAssigner` — bit-faithful note priority (`KeyAssigner.h/.cpp`)

### 5.1 Responsibility and authority

`KeyAssigner` is a small allocation-free struct that is the literal model of the
firmware `keyboard_read` / `play` state machine [research/07 §2.3, §3.1-§3.3].
It is the **sole note-priority authority for MONO and UNISON**; POLY bypasses it
entirely [ADR-006 §Decision item 3, C12]. It couples priority and trigger to the
single S7 selector exactly as the hardware does — there is no separate priority
parameter and no separate retrigger toggle [research/07 §11: "Do not treat
priority and trigger as independent parameters"; ADR-006 C].

### 5.2 Data layout

The keyboard is a 4-bank x 8-key (32-key) matrix read active-low
[research/07 §3.1]. The model carries a held-note bitset (the matrix analog) and
the prior-scan bitset for the last-note XOR. Because mwAudio101 accepts the full
0..127 MIDI range (it is a software instrument, not the literal 32-key bed), the
held-note set is a 128-bit bitset; the *scan order* and the XOR-of-changed-down
logic are the firmware's.

```cpp
namespace mw {

class KeyAssigner {
public:
    void prepare() noexcept;          // clears all held state
    void reset() noexcept;            // panic / all-notes-off

    void setMode (GateTrigMode m) noexcept { mode_ = m; }  // bound to S7

    // Apply all note events that land within the current control tick, then
    // resolve. Events are applied in arrival order; multiple downs in one tick
    // are batched for the last-note XOR (§5.4, C4).
    void noteOn  (int midiNote) noexcept;
    void noteOff (int midiNote) noexcept;

    // Resolve priority + trigger for the current control tick. MUST be called
    // once per control tick (§7). Returns the {activeNote, gate, retrigger,
    // clockReset} the mono/unison Voice(s) consume.
    NoteDecision resolve() noexcept;

    bool anyHeld() const noexcept;

private:
    std::bitset<128> held_;        // currently-held keys
    std::bitset<128> prevScan_;    // prior-tick snapshot for last-note XOR
    GateTrigMode     mode_   = GateTrigMode::GateTrig; // default per voice doc; param owned by doc 06
    int   lastActive_        = -1; // active note emitted last tick
    bool  gateWasAsserted_   = false;
    bool  newKeyThisTick_    = false; // for CLOCK RESET in Lfo/Arp (§5.4 C6)
};

} // namespace mw
```

### 5.3 Resolution algorithm

`resolve()` is `noexcept`, allocation-free, and runs an O(128) scan (bounded,
not per-sample) once per control tick:

- **Lowest-note priority** (Gate, Lfo): scan banks low->high and key low->high;
  the first held key found wins — "as soon as we find a key down, we are done"
  [research/07 §3.2].
- **Last-note priority** (GateTrig): compute `changedDown = held_ & ~prevScan_`
  (the XOR of newly-changed-down keys against the prior scan) and pick the
  **lowest of the just-pressed** keys; if no new down this tick, the active note
  stays the most-recently-pressed still-held key [research/07 §3.2, C4].
- **Retrigger** is emitted per the §5.4 contract table; it is a function of mode
  and event, never an independent parameter.
- **CLOCK RESET** is emitted on any new keypress while `mode_ == Lfo` (and, via
  the arp boundary §9, while ARP is active) [research/07 §5.2, C6].

After resolving, `prevScan_ = held_` and `newKeyThisTick_ = false`.

### 5.4 Note-priority / retrigger contract (NORMATIVE)

This table is implemented verbatim. It is the ADR-006 §Contract case table,
restated as this subsystem's behavioral contract; the backlog's KeyAssigner and
VoiceManager tests assert it. "control tick" = the decimated control update
(§7). "retrigger" = re-fire the shared ADSR from its trigger state. All
MONO/UNISON note selection is via the single KeyAssigner; POLY bypasses it.

| ID | Mode | Selector (S7) | Event | Required behavior | Trace |
| --- | --- | --- | --- | --- | --- |
| K1 | MONO | Gate | New key while a note is held | Lowest-held wins; gate STAYS asserted (no retrigger); glide to new lowest. | [ADR-006 C1; research/07 §3.2-§3.3] |
| K2 | MONO | Gate | Release of current lowest while others held | Active note becomes next-lowest still-held; gate stays asserted; glide to it. | [ADR-006 C2] |
| K3 | MONO | GateTrig | New key while a note is held | Last-note priority; ADSR retriggers on the new key. | [ADR-006 C3; research/07 §3.3] |
| K4 | MONO | GateTrig | Multiple keys go down within one control tick | Pick LOWEST of the just-pressed (XOR changed-down vs prior scan); retrigger once. | [ADR-006 C4; research/07 §3.2] |
| K5 | MONO | Lfo | Key held | Pitch uses lowest-note priority; ADSR is (re)triggered by clock H->L edge, NOT the key. | [ADR-006 C5; research/07 §3.3, §5.1] |
| K6 | MONO | Lfo | New keypress | Assert CLOCK RESET (re-phase clock/sequence). Also applies in ARP mode. | [ADR-006 C6; research/07 §5.2] |
| K7 | MONO/UNISON | any | All keys released | Gate de-asserts; ADSR enters release. | [ADR-006 C7] |
| K8 | MONO/UNISON | any | Pitch CV update | VINTAGE: 6-bit quantized at 1 V/oct, portamento steps at the control tick. MODERN: continuous float (§7). | [ADR-006 C8; ADR-005 C1/C3; research/07 §4.2] |
| K9 | UNISON | any | Note resolved | Stack U voices (U in 1..8) on that ONE note; selection/retrigger identical to K1-K6. | [ADR-006 C9] |
| K10 | UNISON | any | Detune / spread | Symmetric cents detune centered on the note; stereo spread distributed; each voice distinct drift seed (real beating, not static fan). | [ADR-006 C10] |
| K11 | UNISON | any | Steal | Steal whole unison groups, never individual stacked voices. | [ADR-006 C11] |
| K12 | POLY | n/a (bypassed) | Each note-on | Allocate own voice; every note a fresh trigger (GATE+TRIG-style); no cross-voice lowest-note concept. | [ADR-006 C12] |
| K13 | POLY | n/a | Re-strike of a held key | Reuse that key's own voice (no doubling). | [ADR-006 C13] |
| K14 | POLY | n/a | No idle voice available | Steal: oldest-in-release -> quietest (lowest VCA/env level) -> oldest-held. Tie-break by ascending integer note-serial. Deterministic. | [ADR-006 C14] |
| K15 | POLY | n/a | A steal | Fast forced fade-out into reuse (no hard cut); other voices' release tails finish in place. | [ADR-006 C15] |
| K16 | POLY | n/a | Effective polyphony under unison | floor(maxPoly / U) active groups; active-voice count hard-capped at kMaxVoices. | [ADR-006 C16] |
| K17 | MONO | any | Golden-trace conformance | C++ KeyAssigner emits identical {activeNote, gate, retrigger} sequences to the disassembly-semantics reference over the legato/overlap/release-order battery. Poly/unison exempt, tested separately. | [ADR-006 C19; research/07 §11] |

**Deliberate omission (do not "fix"):** power users cannot get last-note
priority WITHOUT retrigger; the two are one coupled S7 control. Decoupling them
is exactly what breaks SH-101 authenticity [ADR-006 §Consequences].

### 5.5 `Glide` — portamento (`Glide.h/.cpp`)

Glide is per-voice and slews the pitch target [research/05 §5]. Range and modes:

| Property | Value | Unit | Trace |
| --- | --- | --- | --- |
| Time range | 0 - 5 | s | [research/05 §5, §6] |
| Modes | OFF / ON / AUTO | enum | [research/05 §5] |
| AUTO behavior | glide only on legato (key released before next pressed) | — | [research/05 §5] |
| Disabled while arp runs | yes — glide does not work while arpeggiator drives notes | — | [research/05 §5] |
| Curve | RC-style (exponential) integrator on the pitch CV | — | **(PI)**, curve unknown [research/05 §5 honest label] |

The glide curve and whether it is constant-time or constant-rate are
**undocumented** [research/05 §5 honest label]; the model uses an RC-style
exponential slew toward the target, with the time constant mapped from the 0-5 s
TIME parameter — a **(PI)** mapping centralized in `Calibration.h`. In VINTAGE
control the glide smooths *between* 6-bit quantized holds, exactly as the
hardware RC does; the pitch itself is not smoothed away [ADR-005 §Decision
item 2].

```cpp
namespace mw {

enum class GlideMode : uint8_t { Off = 0, On = 1, Auto = 2 };

class Glide {
public:
    void  prepare (double sampleRate) noexcept;
    void  setMode (GlideMode m) noexcept { mode_ = m; }
    void  setTimeSeconds (float t) noexcept;          // 0..5 s [research/05 §5]
    void  setTarget (float targetPitchHz, bool legato, bool arpActive) noexcept; // applies mode rules
    float nextValue() noexcept;                       // one slewed pitch sample/tick
    void  snapTo (float pitchHz) noexcept;            // jump (no glide) for arp/first note
private:
    GlideMode mode_ = GlideMode::Off;
    float current_ = 0.0f, target_ = 0.0f, coeff_ = 1.0f;
};

} // namespace mw
```

## 6. `VoiceManager` — pool, modes, allocation (`VoiceManager.h/.cpp`)

### 6.1 Responsibility

`VoiceManager` owns the fixed `Voice[kMaxVoices]` pool, allocated in `prepare`,
and is the dispatch point for the three modes [ADR-006 §Decision item 3]. The
three modes are skins over the one pool — there is one render path for all three
[ADR-019 VT-01]. It also owns the active-voice list so the render loop skips idle
slots [ADR-006 §4].

```cpp
namespace mw {

class VoiceManager {
public:
    void prepare (double sampleRate, int oversampleFactor, uint32_t instanceSeed) noexcept;

    // Configured at prepare or at a block boundary via a lock-free flag, NEVER
    // mid-block [ADR-006 C17].
    void setMode        (VoiceMode m) noexcept;
    void setUnisonCount (int u) noexcept;            // 1..kMaxUnison [ADR-006 C9]
    void setGateTrigMode(GateTrigMode m) noexcept;   // forwards to keyAssigner_ (S7)

    // Note events demultiplexed from MIDI/MPE/arp-seq (§9). Sample-accurate.
    void handleNoteEvent (const NoteEvent& e) noexcept;

    // Advance the KeyAssigner one control tick and propagate decisions to voices
    // (MONO/UNISON). Called by ControlCore once per control tick (§7).
    void controlTick (const NoteDecision& d) noexcept;

    // Render all active voices for `numSamples`, summing in FIXED voice-index
    // order, into outL/outR [ADR-019 VT-01, VT-02].
    void render (float* outL, float* outR, int numSamples) noexcept;

private:
    std::array<Voice, kMaxVoices> pool_;             // preallocated, no heap on audio thread
    KeyAssigner  keyAssigner_;                       // sole authority for MONO/UNISON (§5)
    VoiceMode    mode_        = VoiceMode::Mono;      // default [ADR-016 R-3]
    int          unison_      = 1;
    uint64_t     nextSerial_  = 0;                    // monotonically increasing [ADR-006 C14]

    // Active-voice indices, dense prefix; size = activeCount_. No allocation.
    std::array<uint8_t, kMaxVoices> active_{};
    int          activeCount_ = 0;

    // --- mode-specific dispatch ---
    void driveMono   (const NoteDecision& d) noexcept;  // exactly ONE voice
    void driveUnison (const NoteDecision& d) noexcept;  // U voices on one note
    int  allocatePoly(int midiNote) noexcept;           // returns voice index; runs the steal scan
    void releasePoly (int midiNote) noexcept;
};

} // namespace mw
```

### 6.2 MONO mode (default, bless target)

Exactly ONE active `Voice`, driven verbatim by the `KeyAssigner` decision. Zero
behavioral logic on top — a pass-through, so mono is bit-faithful by
construction and diffable bit-for-bit against the arm64 bless render
[ADR-006 §Decision item 3 MONO; ADR-016 R-3]. `driveMono` applies the
`NoteDecision`: set the glide target to `activeNote`, assert/de-assert gate,
fire retrigger when the decision says so.

### 6.3 UNISON mode

U voices (U in 1..kMaxUnison) are all fed the SAME note resolved by the SAME
single `KeyAssigner`, so unison note-feel stays mono-faithful — priority and
retrigger are owned by the KeyAssigner exactly as in MONO [ADR-006 §Decision
item 3 UNISON, C9]. `driveUnison` broadcasts the one `NoteDecision` to all U
voices, then applies per-voice detune and pan:

- **Detune** is symmetric in cents, centered on the note: voice `i` of `U` gets
  `detuneCents_i = spread * (2*i/(U-1) - 1)` for `U > 1` (centered, edges at
  `±spread`), `0` for `U == 1`. `spread` is the detune-amount parameter
  (range/skew owned by doc 06) [ADR-006 C10].
- **Stereo spread** distributes pan across the U voices; the distribution law is
  **(PI)** (e.g. symmetric linear fan `pan_i = 2*i/(U-1) - 1` scaled by a
  spread-amount parameter), centralized in `Calibration.h` and labeled a modern
  addition [ADR-006 C10; ADR-013].
- Each unison voice keeps its distinct deterministic drift seed (§4.4), so
  detune is real analog beating, not a static fan [ADR-006 C10].
- Stealing under unison operates on whole unison groups, never individual
  stacked voices [ADR-006 C11].

### 6.4 POLY mode (bypasses the KeyAssigner)

POLY is the only mode that bypasses the `KeyAssigner` [ADR-006 §Decision item 3
POLY, C12]. Poly has no historical SH-101 behavior; its policy is a designed
choice [ADR-006 §Context]. The per-note allocator (`allocatePoly`):

1. **Idle voice** — prefer any `Idle` slot.
2. **Re-strike** — if the incoming note equals a held voice's `currentNote`
   within the re-strike window, reuse that voice (no doubling) [ADR-006 C13].
3. **Steal** — otherwise run the deterministic O(kMaxVoices) integer-stamped
   scan and steal in order: **oldest-in-release -> quietest (lowest current
   VCA/env level) -> oldest-held**, tie-broken by ascending integer note-serial
   [ADR-006 C14]. No timestamps, no sort, no allocating containers, no priority
   queue.

A steal is a **fast forced fade-then-reuse** (`Voice::beginSteal`, state
`Stealing`), not a hard cut; other voices' release tails finish in place
[ADR-006 C15]. The fade length is **(PI)** (e.g. a 1-3 ms ramp), centralized in
`Calibration.h`.

Every poly note is its own fresh trigger (GATE+TRIG-style); there is no
cross-voice lowest-note concept [ADR-006 C12]. Each allocation stamps the voice
with `noteSerial_ = nextSerial_++` for deterministic steal ordering
[ADR-006 C14, C18].

**Unison stacks on poly:** effective polyphony = `floor(maxPoly / U)` active
groups; the active-voice count is hard-capped at `kMaxVoices`; stealing removes
whole unison groups [ADR-006 C16].

### 6.5 Determinism

Drift seeds and steal ordering derive only from voice index + instance seed and
integer note-serials, never wall-clock, so renders are byte-stable on the arm64
bless gate and the Linux co-gate [ADR-006 C18; ADR-019 VT-04].

## 7. `ControlCore` — control rate and 6-bit CV (`ControlCore.h/.cpp`)

### 7.1 Responsibility

`ControlCore` is the one shared virtual "6-bit DAC + 4052 mux + sample-and-hold"
object, always present, time-multiplexed across VCO CV / CV OUT / RANDOM
[ADR-005 §Decision item 1; research/07 §4.1]. It drives the control tick that
clocks the `KeyAssigner`/`VoiceManager` and applies the VINTAGE-vs-MODERN pitch
domain. The shipped default is **MODERN-SMOOTH** [ADR-016 R-1, §Decision item 1];
VINTAGE is the labeled toggle and the bit-exact reference variant.

The control core is driven by a **sample counter inside `processBlock`**, never
a wall-clock timer or background thread [ADR-005 §Decision item 3, §Contract].

### 7.2 DAC / mux model

A single 6-bit DAC time-multiplexed via a 4052, route bits D7:D6 [research/07
§4.1; ADR-005 §Decision item 1]:

| Route (D7:D6) | Destination |
| --- | --- |
| `00` | CV OUT |
| `01` | VCO |
| `10` | RANDOM |
| `11` | parked / idle (bus parked between updates) |

The parked `11xxxxxx` idle state is modeled. RANDOM is regenerated on each clock
H->L edge so its rate tracks the LFO/EXT clock [research/07 §4.1, §5.3].

### 7.3 VINTAGE pitch quantization

Pitch CV is assembled as **integer DAC counts** (key + range + octave +
key-shift), ranges exactly **12 counts apart**, **1 count = 1 semitone**,
converted to volts only at the S/H boundary, so portamento and glides genuinely
stair-step through 6-bit counts [research/07 §4.2; ADR-005 §Decision item 2]:

| Range | Base (DAC counts) | Volts |
| --- | --- | --- |
| 16' | 0x0C (12) | 1 V |
| 8' | 0x18 (24) | 2 V |
| 4' | 0x24 (36) | 3 V |
| 2' | 0x30 (48) | 4 V |

Octave switch adds 0x00 (down, -12) / 0x0C (mid) / 0x18 (up, +12); KEY TRANSPOSE
adds a stored key-shift [research/07 §4.2]. The 6-bit ladder lands on
~1.6-cent-spaced steps within an octave (12 counts / octave) [ADR-005
§ratification]. Quantization happens in the **control domain before** the
oversampled audio render, so the step is consistent regardless of oversample
factor [ADR-005 §Consequences].

### 7.4 VINTAGE control rate

Control updates land on a fixed-order polling tick of nominally **~2 ms**,
sample-accurate at block boundaries, driven by the sample counter [ADR-005
§Decision item 3; research/07 §2.3]. The firmware loop runs every **1.5-3.5 ms**
[research/07 §2.3]. Per-tick variation across that envelope is a **separately
toggleable loop-time jitter**, seeded from a cheap per-instance PRNG/LUT once per
tick. Jitter magnitude is an **open validation gap** [research/07 §8.5, §9.5-9.6]
so it is a labeled flavor toggle with a conservative default; the **bit-exact
macOS arm64 reference / Linux co-gate variant runs the fixed-tick, jitter-OFF
VINTAGE configuration** [ADR-005 §Decision item 3, C1].

### 7.5 MODERN pole (shipped default)

MODERN bypasses the 6-bit quantizer (continuous float pitch) and runs control at
a clean fixed sub-block tick with smoothed CV and host-synced sample-accurate
arp/seq edges [ADR-005 §Decision item 4; ADR-016 R-1]. The clean tick length is
**(PI)** — e.g. 16-32 samples [ADR-005 C3] — centralized in `Calibration.h` and
owned (and to be justified) by this project since the hardware does not dictate
it [ADR-005 §Consequences].

### 7.6 Per-feature MODERN auto-engage

The owner-locked modern features — **poly/unison voices, MPE-lite pitch bend,
and sub-cent host automation of pitch** — are musically incompatible with the
hard 6-bit ladder and **auto-engage the MODERN pole for the pitch path** even
when the macro is set to VINTAGE [ADR-005 §Decision item 5, C4-C6]. The mono,
single-voice, non-MPE path honors VINTAGE fully. This auto-engagement is surfaced
to the user, never silent [ADR-005 §Contract invariants; ADR-013].

Concretely: when `mode_ != Mono` (UNISON or POLY) or MPE per-note pitch is
active or pitch is under sub-cent automation, the VINTAGE 6-bit quantizer is
bypassed and the pitch path is continuous float, regardless of the Vintage
Control macro setting.

### 7.7 Clock feel and mode crossfade

VINTAGE clock feel = H->L edge advance, clock-reset-on-keypress re-phasing, and
clock-tied random S/H [research/07 §5.1-§5.3; ADR-005 §Decision item 6]; MODERN =
host-synced sample-accurate edges. The detailed arp/seq clock is owned by
ADR-007/022; this doc owns only the CLOCK RESET emission via the KeyAssigner
(§5.3, K6).

When the Vintage Control macro is automated VINTAGE<->MODERN, both CV branches
are precomputed and the CV is crossfaded/blended (no zipper, branchless hot
path); no allocation on mode switch [ADR-005 §Consequences, C7].

### 7.8 Control tick signatures

```cpp
namespace mw {

enum class VintageControlPole : uint8_t { Modern = 0, Vintage = 1 }; // default Modern [ADR-016 R-1]

class ControlCore {
public:
    void prepare (double sampleRate) noexcept;

    void setPole         (VintageControlPole p) noexcept;  // Vintage Control macro
    void setJitterEnabled(bool on) noexcept;               // loop-time jitter toggle (off by default)

    // Called once per processBlock chunk. Advances the sample counter, fires
    // control ticks at the right boundaries, clocks the KeyAssigner via the
    // VoiceManager, and applies the pitch domain. Returns nothing; drives vm.
    void advance (int numSamples, VoiceManager& vm) noexcept;

    // Effective pole after per-feature auto-engage (§7.6). MODERN when poly/
    // unison/MPE/sub-cent-automation is active even if macro=Vintage.
    VintageControlPole effectivePole (VoiceMode mode, bool mpeActive,
                                      bool pitchAutomated) const noexcept;

    // Integer DAC-count pitch assembly (VINTAGE); counts->volts at S/H (§7.3).
    static int   assemblePitchCounts (int midiNote, int rangeBase,
                                      int octaveOffset, int keyShift) noexcept;
    static float countsToVolts (int counts) noexcept;       // 1 count = 1 semitone, 12/oct

private:
    VintageControlPole macroPole_ = VintageControlPole::Modern; // [ADR-016 R-1]
    bool   jitterOn_     = false;                               // [ADR-005 C1]
    int    sampleCounter_= 0;
    double sampleRate_   = 0.0;
    XorShift32 jitterRng_;                                      // seeded; deterministic
    // DAC/mux/S&H route state (00/01/10/11) modeled here (§7.2).
};

} // namespace mw
```

### 7.9 Control-rate contract (NORMATIVE)

Restates ADR-005 §Contract for this subsystem. Control tick is always driven by a
sample counter inside `processBlock`; no wall-clock, no locks, no heap on the
audio thread.

| Case | Context | Macro | Jitter | Pitch domain (VCO CV) | Control tick | Bit-exact ref? | Trace |
| --- | --- | --- | --- | --- | --- | --- | --- |
| CC1 | Mono, single voice, no MPE | VINTAGE | OFF | 6-bit integer counts; 12/oct; counts->volts at S/H; portamento stair-steps | Fixed ~2 ms | YES (arm64 bless / Linux co-gate) | [ADR-005 C1] |
| CC2 | Mono, single voice, no MPE | VINTAGE | ON | 6-bit integer counts (as CC1) | Variable, seeded over 1.5-3.5 ms | NO (labeled flavor; open gap) | [ADR-005 C2; research/07 §8.5] |
| CC3 | Mono, single voice, no MPE | MODERN (default) | n/a | Continuous float (quantizer bypassed) | Clean fixed sub-block tick (PI 16-32 smp) | Deterministic; not default ref | [ADR-005 C3; ADR-016 R-1] |
| CC4 | Poly / unison | VINTAGE requested | any | MODERN auto-engages: continuous float per voice | Clean fixed sub-block tick | Deterministic | [ADR-005 C4] |
| CC5 | MPE-lite per-note pitch bend | VINTAGE requested | any | MODERN auto-engages: continuous float / fine bend | Clean fixed sub-block tick | Deterministic | [ADR-005 C5] |
| CC6 | Sub-cent host automation of pitch | VINTAGE requested | any | MODERN auto-engages for the automated pitch path | Clean fixed sub-block tick | Deterministic | [ADR-005 C6] |
| CC7 | Automating the macro itself | VINTAGE<->MODERN | any | Crossfade/blend both CV branches; no zipper; branchless | unchanged per target pole | n/a | [ADR-005 C7] |

## 8. Threading and real-time invariants

Per ADR-019, v1 voice rendering is **single-threaded inside the one
`process(const BlockContext&) noexcept` call** — no worker threads, no thread
pool, no cross-thread voice handoff [ADR-019 VT-01, §Decision].

| ID | Invariant | Trace |
| --- | --- | --- |
| RT1 | `VoiceManager::render` walks the active-voice list and renders each active voice for the `kRenderBlock` chunk on the audio-callback thread, inside the single `process` call. | [ADR-019 VT-01] |
| RT2 | Active voices are summed into the block mix in FIXED voice-index order before the shared FX chain. | [ADR-019 VT-02] |
| RT3 | Voice rendering uses NO mutex, condition variable, futures, atomics-as-locks, or any blocking/spinning cross-thread primitive; the `AudioThreadGuard` sentinel must never trip from the voice loop. | [ADR-019 VT-03; ADR-001 C4] |
| RT4 | Identical input -> FP bit-exact output on macOS arm64 and tolerance-banded on Linux x64; fixed render/sum order is the mechanism. | [ADR-019 VT-04; ADR-006 C18] |
| RT5 | Worst-case CPU = active voices x per-voice 2x-oversampled cost + shared FX, on one thread; guarded by the per-block CPU-budget regression assertion. | [ADR-019 VT-05; ADR-003 F-09] |
| RT6 | Pool, drift, scratch, KeyAssigner state all sized/allocated in `prepare`; audio thread only activates/idles existing voices; no heap alloc, no locks anywhere on the audio thread. | [ADR-006 §4, C17; ADR-001] |
| RT7 | Mode and voice-count changes only at `prepare` or via a lock-free flag read at a block boundary, never mid-block. | [ADR-006 C17] |

## 9. Note-ownership boundary (arp / seq / MPE)

The host-synced arp, the 100-step sequencer, and MPE-lite must plug into the
KeyAssigner/allocator boundary with a clear "who owns the note" contract
[ADR-006 §Consequences]. This document fixes the *boundary*; the producers'
internal behavior is owned by ADR-007/012/022.

- **Single ingress:** all note sources feed `VoiceManager::handleNoteEvent` as
  `NoteEvent`s. The VoiceManager is the single demux point.
- **MONO/UNISON:** notes flow through the `KeyAssigner` (§5). The arp/seq, when
  driving notes, emit `NoteEvent`s like a keyboard; CLOCK RESET re-phasing
  (K6) and glide-disable-while-arp (§5.5) apply.
- **POLY:** notes bypass the KeyAssigner and hit the poly allocator (§6.4)
  directly.
- **MPE-lite** (per-note pitch + one pressure destination, lower zone
  [ADR-016 §5]) forces the MODERN pitch pole (§7.6, CC5) and, in POLY, maps each
  MPE member channel to its own voice; in MONO/UNISON, per-note pitch applies to
  the single resolved note. Detailed mapping is owned by ADR-012/022.

## 10. Defaults (out-of-box / INIT)

The shipped INIT state for this subsystem, per ADR-016 (mechanisms unchanged;
only the default pole selection):

| Surface | Shipped default | Faithful pole (one action away) | Trace |
| --- | --- | --- | --- |
| Voice mode | MONO (coupled-S7 KeyAssigner; bless target) | (mono IS faithful) Poly / Unison are the modern toggle | [ADR-016 R-3] |
| Control rate / pitch | MODERN-SMOOTH (continuous float, clean smoothed CV) | VINTAGE 6-bit additive-integer, stair-stepped portamento, ~2 ms tick (bit-exact ref variant) | [ADR-016 R-1] |
| Velocity | ON (-> VCA level + VCF cutoff amount) | OFF (faithful no-velocity SH-101 keyboard) | [ADR-016 R-2] |
| Loop-time jitter | OFF | ON (labeled flavor; open validation gap) | [ADR-005 C1-C2] |

The canonical parameter IDs/ranges/defaults that express these are owned by
doc 06 (docs/design/06 §2); the values above are this subsystem's behavioral
contract for the INIT patch and factory baseline.

## Acceptance hooks

Objectively-testable properties a backlog task's tests must verify:

- **Golden-trace conformance (K17):** the C++ `KeyAssigner` emits identical
  `{activeNote, gate, retrigger}` sequences to the disassembly-semantics
  reference (`KeyAssignerReference`) over the full legato/overlap/release-order
  battery, in all three `GateTrigMode` values [ADR-006 C19].
- **Gate lowest-note (K1/K2):** in MONO+Gate, pressing a higher key while a
  lower is held keeps the gate asserted (no retrigger) and the active note stays
  lowest; releasing the lowest while others held selects the next-lowest with the
  gate still asserted.
- **GateTrig last-note + XOR (K3/K4):** in MONO+GateTrig, a new key takes
  priority and retriggers; multiple downs within one control tick resolve to the
  lowest of the just-pressed (XOR changed-down) with exactly one retrigger.
- **Lfo trigger (K5/K6):** in MONO+Lfo, pitch follows lowest-note but the ADSR
  fires on the clock H->L edge (not the key), and a new keypress asserts CLOCK
  RESET.
- **Coupling enforced:** there is no code path and no parameter that yields
  last-note priority without retrigger (the S7 enum is the only control)
  [ADR-006 §Consequences].
- **Unison mono-faithfulness (K9):** with U>1, the resolved note and
  retrigger sequence for the unison stack are byte-identical to the MONO case for
  the same input; only detune/pan/drift-seed differ per voice.
- **Unison beating (K10):** U unison voices on one note carry distinct drift
  seeds; detune is symmetric and centered; the summed output is not a static
  pitch fan (measurable beating).
- **Unison group steal (K11):** stealing under unison removes whole U-voice
  groups, never an individual stacked voice.
- **Poly fresh trigger + re-strike (K12/K13):** every poly note-on is a fresh
  trigger; re-striking a held note reuses that note's voice with no doubling.
- **Poly steal order (K14):** with no idle voice, stealing follows
  oldest-in-release -> quietest -> oldest-held, tie-broken by ascending integer
  note-serial, deterministically (same input -> same victim).
- **Poly steal is faded (K15):** a steal applies a fast fade (not a hard cut) and
  other voices' release tails finish in place (no click at the steal).
- **Unison-on-poly capacity (K16):** effective polyphony equals
  `floor(maxPoly / U)`; the active-voice count never exceeds `kMaxVoices`.
- **Glide range/modes:** glide TIME spans 0-5 s; AUTO glides only on legato; ON
  always glides; glide is disabled while the arpeggiator drives notes
  [research/05 §5].
- **VINTAGE quantization (CC1):** in MONO+VINTAGE, VCO-CV pitch lands on integer
  6-bit DAC counts (12 counts/octave, 1 count = 1 semitone); portamento
  stair-steps; ranges are 12 counts apart (16'/8'/4'/2' = 1/2/3/4 V).
- **MODERN default (CC3):** the INIT/out-of-box state runs MODERN-SMOOTH
  (continuous float pitch, clean smoothed CV) with jitter OFF [ADR-016 R-1].
- **Per-feature auto-engage (CC4-CC6):** with the macro set to VINTAGE, enabling
  poly/unison, MPE per-note pitch, or sub-cent pitch automation switches the
  pitch path to continuous float, and the auto-engagement is surfaced (not
  silent).
- **Mode crossfade (CC7):** automating the Vintage Control macro
  VINTAGE<->MODERN produces no zipper/click and allocates nothing.
- **Determinism / bless (RT4, C18):** identical input yields FP bit-exact output
  on macOS arm64 and tolerance-banded on Linux x64; drift seeds and steal
  ordering depend only on voice index + instance seed + integer note-serials,
  never wall-clock.
- **RT safety (RT3, RT6, RT7):** the `AudioThreadGuard` never trips during the
  voice loop, control core, or note handling; no heap allocation or lock occurs
  on the audio thread; mode/voice-count changes never occur mid-block.
- **Pool bounds:** `kMaxVoices == kMaxPoly * kMaxUnison`; the active-voice scan
  is O(kMaxVoices) and runs once per note event, never per sample.

## References

ADRs:

- ADR-005 — Control-rate model and 6-bit CV quantization authenticity
  (`plan/decisions/005-control-rate-cpu-authenticity.md`).
- ADR-006 — Voice architecture, polyphony and unison
  (`plan/decisions/006-voice-poly-unison-model.md`).
- ADR-016 — Owner ratifications: out-of-box defaults
  (`plan/decisions/016-owner-ratifications-2026-06-18.md`).
- ADR-019 — Voice-rendering threading model
  (`plan/decisions/019-voice-rendering-threading-model.md`).
- ADR-001 (referenced) — core/plugin boundary, `process` seam, `AudioThreadGuard`.
- ADR-003 (referenced) — IR3109 filter modeling, 2x oversampling, CPU-budget gate.
- ADR-008 (referenced) — parameter / state / preset schema (owns parameter IDs).
- ADR-009 (referenced) — vintage variance / drift DSP and `vintage.age` macro.
- ADR-012 / ADR-022 (referenced) — MIDI / MPE / arp-seq cross-format contract.

Research docs:

- `docs/research/07-cpu-key-assigner.md` — TMP80C49 firmware: polling loop,
  key matrix, note priority/trigger coupling, 6-bit DAC/4052/S&H, pitch CV
  assembly, clock/CLOCK RESET.
- `docs/research/04-envelope-lfo-vca.md` — one shared ADSR, one LFO per signal
  chain (§2.1, §3.1) driving the per-voice one-EG/one-LFO topology.
- `docs/research/05-mixer-modulation-glide.md` (referenced) — portamento
  TIME range, OFF/ON/AUTO modes, arp-disable, glide-curve honest label (§5).
