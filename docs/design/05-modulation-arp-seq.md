<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Modulation Routing, Arpeggiator & 100-Step Sequencer

## 1. Scope and position

### 1.1 What this document owns

This document is the single source of truth for mwAudio101's **control core**: the
fixed modulation routing, the trigger-source switch, the arpeggiator, the
100-step sequencer, and the shared clock (internal, host-sync, external) with its
swing and clock-reset behavior. It mirrors the SH-101 single-CPU design (IC6,
TMP80C49P) modeled as one deterministic, fixed-order state machine
[ADR-007 §Decision; research/07 §2.2, §11; research/06 §6, §9].

Concretely this doc specifies these files the backlog creates:

- `core/control/ModRouter.h` / `.cpp` — fixed LFO/ENV modulation routing.
- `core/control/TriggerSource.h` / `.cpp` — S7 GATE+TRIG / GATE / LFO coupling.
- `core/control/Arpeggiator.h` / `.cpp` — UP / U&D / DOWN over a 32-key bitmap.
- `core/control/StepSequencer.h` / `.cpp` — 100-slot record/play model.
- `core/control/Clock.h` / `.cpp` — H→L edge node + 3-way source wrapper + swing.
- `core/control/SequencerEngine.h` / `.cpp` — `mw::seq::SequencerEngine`, the
  arp/seq state machine: the fixed-order tick that hosts the above and exposes
  the immutable persistence snapshot. (Distinct from doc 04's
  `mw::ControlCore` in `core/control/ControlCore.h`, which owns the control/voice
  glue; no two classes share a file or name — D3.)

### 1.2 What this document does NOT own (referenced only)

- **Parameter IDs, ranges, defaults, skews, units, automation flags** are owned
  by the parameter schema in `docs/design/06` §2 per [ADR-008]. This doc names
  parameters by their *role* and gives the numeric tables that doc 06 mints into
  formal IDs; where this doc states a range it is the behavioral requirement doc
  06 binds to, not a competing definition. Other docs REFERENCE doc 06; they
  never re-mint IDs.
- **LFO core generation** (triangle/square/RANDOM/NOISE shaping, rate taper) is
  owned by the LFO/oscillator design docs; this doc consumes the *selected LFO
  value* as an input (see §3.1) and the RANDOM-on-edge reload as an output the
  clock node drives.
- **ADSR segment generation** (curve law, A/D/S/R timing) is owned by the
  envelope design doc; this doc consumes a single shared ADSR and routes/triggers
  it (see §3.3, §4).
- **VCO/VCF/VCA DSP**, **6-bit quantization / control-rate vintage tick**
  ([ADR-005]), **voice/poly/unison** ([ADR-006]), **velocity routing**
  ([ADR-012], [ADR-016 R-2]), and **MIDI/MPE** ingestion are owned by their
  respective docs. This doc receives note-on/off + held-key state and emits
  pitch/gate/trigger/portamento control events into them.
- **Preset container format / schema versioning mechanics** are owned by
  `docs/design/06`; this doc defines only the *payload* it contributes (§9) and
  the RT-safe snapshot-swap contract.

### 1.3 Defaults posture (ADR-016)

The control core is mechanism; [ADR-016] fixes which pole the INIT/out-of-box
state selects. Where this doc states a default it cites [ADR-016] (e.g.
voice mode MONO [R-3]; MODERN-SMOOTH control [R-1]). The faithful pole of every
behavior remains reachable as a labeled toggle and stays the bit-exact reference
where one is defined [ADR-016 §Decision].

## 2. Architecture overview

### 2.1 One state machine, one clock node

All control behavior is one deterministic, fixed-order state machine. It mirrors
the SH-101 super-loop step order [research/07 §2.3; research/06 §6.1]:

```text
3  Range Data Read        -> (pitch base offsets; owned by voice/pitch doc)
4  Range Data Output
5  Keyboard Read          -> TriggerSource: priority + held-key set
6  Clock Check            -> Clock: detect H->L edge on the clock node
7  Random Data Output     -> RANDOM reload fires on the edge (LFO doc consumes)
8  Function Switch Read    -> latch arp mode / HOLD / LOAD / PLAY edges
9  Load                   -> StepSequencer::recordFromKeyboard
10 Play                   -> StepSequencer::advanceOnEdge
11 Arpeggio               -> Arpeggiator::advanceOnEdge
12 CV Output              -> emit pitch CV (pitch doc consumes)
13 Gate & LED Output      -> emit gate/trig/portamento + CLOCK RESET
```

Exactly one H→L edge detector lives on a single clock node and advances arp, seq,
and RANDOM reload **together**, so they stay phase-consistent across all clock
sources [ADR-007 C17; research/07 §5.1, §6.3]. There is no second sequencer
engine for host-sync — host edges feed this same detector (§7.4).

### 2.2 Control tick vs sample-accurate edges

The state machine runs on a tunable fixed **control tick** decoupled from clock
*edge placement* [ADR-007 §Resolution 4, C27]:

- The control tick default models the coarse hardware loop of **1.5–3.5 ms**; the
  shipped default tick period is **2.0 ms (PI)** (centralized in
  `core/calibration/Calibration.h` as `kControlTickSeconds`), matching the
  ~2 ms VINTAGE tick named in [ADR-016 R-1] / [ADR-005]
  [research/07 §2.3; research/06 §6.1].
- Clock *edges* (and swing offsets) are placed at **sample-accurate** sub-block
  sample offsets independent of the tick, so host-sync stays tight while the
  authentic stepping feel is preserved [ADR-007 §Resolution 4, C19, C27].
- The control tick may be driven faster than 1.5–3.5 ms when MODERN control is
  selected [ADR-016 R-1]; the VINTAGE pole uses the fixed ~2 ms tick and is the
  bit-exact reference [ADR-005; ADR-016 R-1]. The tick rate is supplied by the
  control-rate subsystem, not minted here.

### 2.3 Data flow (block boundary)

```text
MIDI/MPE in --> KeyEvents --> SequencerEngine::processBlock(blockCtx)
                                  |
        +-------------------------+--------------------------+
        |             |              |              |        |
   TriggerSource  Arpeggiator   StepSequencer    Clock     ModRouter
        |             |              |              |        |
        +-------------+------+-------+------+-------+        |
                             v              v               v
                    ControlEvents (pitch, gate, trig, porta, modValues)
                             |
                  ---> Voice / VCO / VCF / VCA / ADSR (other docs)
```

`processBlock` consumes a per-block context (transport as the core
`TransportInfo` POD defined in doc 00 §5.3 — the plugin layer extracts host PPQ
from `juce::AudioPlayHead` and fills it; no `juce::*` type enters core per
[ADR-001]/D2 — plus sample count and current parameter snapshot) and emits
time-stamped `ControlEvent`s into a pre-sized output buffer the voice layer reads.

## 3. Fixed modulation routing (`ModRouter`)

### 3.1 Routing model (fixed, not a matrix)

One LFO scales the **same instantaneous selected-LFO value** into three fixed
destinations through independent per-destination depth gains; one shared ADSR
routes to three destinations. This is fixed routing, never a patch matrix
[ADR-007 §Decision 1, C1; research/04 §3.6, §2.1].

LFO destinations [ADR-007 C1; research/04 §3.6]:

- **VCO pitch** — single MOD depth (vibrato); LFO→pitch amount is
  `mw101.lfo.depth_pitch`.
- **PWM** — own source switch (ENV / MANUAL / LFO) + PWM depth; PWM consumes the
  LFO only when its source switch = LFO, the ADSR only when = ENV, a static value
  when = MANUAL [ADR-007 C2; research/04 §3.6]. The LFO→PWM amount is
  `mw101.lfo.depth_pwm`; the MANUAL static width is `mw101.vco.pwm_depth`
  (distinct params).
- **VCF cutoff** — own MOD depth (`mw101.lfo.depth_cutoff`), alongside the ADSR
  ENV depth (`mw101.vcf.env_mod`) and Key Follow 0–100% (`mw101.vcf.kbd_track`;
  consumed by the filter doc; routed here as a gain) [research/04 §3.6].

Shared ADSR destinations [ADR-007 §Decision 1, C3; research/04 §2.1]:

- **VCF cutoff** (ENV depth).
- **VCA** via the VCA ENV/GATE switch [research/04 §2.4].
- **PWM** when PWM source = ENV.

Additionally keep the SH-101-specific **LFO→VCA tremolo** path: the VCA control
node sums the ENV/GATE level with an LFO tremolo contribution
[ADR-007 §Decision 1; research/04 §3.6, §4.4]. There is exactly one ADSR — no
separate filter/amp envelopes, no sine LFO core, no six-position selector
[ADR-007 §Decision 1; research/04 §3.2, §6.1].

### 3.2 Signature

```cpp
namespace mw::control {

enum class PwmSource : uint8_t { Env = 0, Manual = 1, Lfo = 2 };
enum class VcaSource : uint8_t { Env = 0, Gate = 1 };

// Per-block scalar inputs supplied by the LFO and ADSR subsystems.
struct ModInputs {
    float lfoValue;     // selected LFO instantaneous value, normalized [-1, 1]
    float envValue;     // shared ADSR output, normalized [0, 1]
    float pwmManual;    // MANUAL PWM source value, normalized [0, 1]
};

// Depth gains; all normalized 0..1 (or -1..1 where bipolar). Values come from
// the param snapshot; IDs/ranges/skews are owned by docs/design/06 §2.
struct ModDepths {
    float lfoToPitch;    // VCO pitch MOD depth
    float lfoToPwm;      // PWM depth when PwmSource::Lfo
    float lfoToCutoff;   // VCF cutoff MOD depth (LFO)
    float envToCutoff;   // VCF cutoff ENV depth
    float envToPwm;      // PWM depth when PwmSource::Env
    float lfoToVca;      // LFO->VCA tremolo depth (SH-101-specific)
};

// Resolved per-destination modulation, consumed by VCO/VCF/VCA/PWM DSP.
struct ModOutputs {
    float pitchMod;      // added to pitch CV (semitone units, pitch doc scales)
    float pwmMod;        // pulse-width modulation amount, normalized
    float cutoffMod;     // VCF cutoff modulation, normalized
    float vcaTremolo;    // VCA tremolo contribution, normalized
};

class ModRouter {
public:
    void prepare (double sampleRate) noexcept;            // pre-size; no alloc after
    void setPwmSource (PwmSource s) noexcept;
    void setVcaSource (VcaSource s) noexcept;
    void setDepths (const ModDepths& d) noexcept;

    // Hot path: pure arithmetic, no alloc, no lock, noexcept.
    ModOutputs resolve (const ModInputs& in) const noexcept;

    PwmSource pwmSource() const noexcept { return pwmSource_; }
    VcaSource vcaSource() const noexcept { return vcaSource_; }
private:
    ModDepths depths_ {};
    PwmSource pwmSource_ { PwmSource::Lfo };
    VcaSource vcaSource_ { VcaSource::Env };
};

} // namespace mw::control
```

`resolve()` is a fixed expression with no branches that allocate:
`pitchMod = in.lfoValue * depths_.lfoToPitch`; `cutoffMod = in.lfoValue *
depths_.lfoToCutoff + in.envValue * depths_.envToCutoff`;
`pwmMod = (pwmSource_==Lfo ? in.lfoValue*depths_.lfoToPwm : pwmSource_==Env ?
in.envValue*depths_.envToPwm : in.pwmManual)`;
`vcaTremolo = in.lfoValue * depths_.lfoToVca`.

### 3.3 Routing table

| Source | Destination | Depth gain | Param ID (doc 06) | Active when | Trace |
| --- | --- | --- | --- | --- | --- |
| LFO | VCO pitch | `lfoToPitch` | `mw101.lfo.depth_pitch` | always | research/04 §3.6 |
| LFO | PWM | `lfoToPwm` | `mw101.lfo.depth_pwm` | PwmSource = LFO | research/04 §3.6 |
| LFO | VCF cutoff | `lfoToCutoff` | `mw101.lfo.depth_cutoff` | always | research/04 §3.6 |
| LFO | VCA (tremolo) | `lfoToVca` | (PI; no live ID) | always (summed) | research/04 §4.4 |
| ADSR | VCF cutoff | `envToCutoff` | `mw101.vcf.env_mod` | always | research/04 §2.1 |
| ADSR | VCA | (gain at VCA) | `mw101.vca.mode` selects | VcaSource = ENV | research/04 §2.4 |
| ADSR | PWM | `envToPwm` | (PI; no live ID) | PwmSource = ENV | research/04 §3.6 |

The exact mod-depth scaling constants (V/oct, Hz/V, %/V) are not numeric in any
primary source [research/04 §5.3]; the gains are normalized here and the
physical mapping constants are tagged **(PI)** and centralized in
`core/calibration/Calibration.h` (owned by the calibration table, not minted
here).

## 4. Trigger-source switch (`TriggerSource`, S7)

### 4.1 One control, two coupled behaviors

The S7 selector is a single control binding both note priority and envelope
retrigger — never two independent params [ADR-007 §Decision 2, C4–C6;
research/07 §3.2–3.3]. Three positions:

| S7 position | Note priority | Envelope retrigger | Trace |
| --- | --- | --- | --- |
| `GateTrig` | last-note | retrigger on every new key/step | C4; research/07 §3.2–3.3 |
| `Gate` | lowest-note | no legato retrigger (mono-legato); single gate sustains | C5; research/07 §3.2–3.3 |
| `Lfo` | lowest-note | envelope re-fired each LFO/clock cycle | C6; research/07 §3.2–3.3 |

GATE-mode priority is **lowest-note**, not high-note — this corrects a forum
misreading and must not regress [research/07 §3.2, §8.2; research/06 §5].

### 4.2 Signature

```cpp
namespace mw::control {

enum class TrigMode : uint8_t { GateTrig = 0, Gate = 1, Lfo = 2 };
enum class NotePriority : uint8_t { LastNote, LowestNote };

struct KeyState {
    // 32-key held bitmap; bit i set => key i held. 4 bytes / 32 keys.
    uint32_t held = 0;            // research/07 §3.1; research/06 §2.4
    uint32_t justPressed = 0;     // keys newly down since last scan (XOR-of-changed)
    uint32_t justReleased = 0;
};

struct TriggerDecision {
    int   selectedKey = -1;   // -1 == no note (all keys up, gate off)
    bool  retrigger   = false;// fire envelope this tick
    bool  gateOn      = false;
    bool  legato      = false;// new key while another already held (no retrig in Gate)
};

class TriggerSource {
public:
    void setMode (TrigMode m) noexcept;            // sets priority + retrigger rule
    NotePriority priority() const noexcept;        // derived from mode_
    TrigMode mode() const noexcept { return mode_; }

    // Resolve held bitmap to a monophonic decision. noexcept; no alloc.
    // lfoEdge: true on an H->L clock edge (drives Lfo-mode re-fire).
    TriggerDecision resolve (const KeyState& ks, bool lfoEdge) const noexcept;
private:
    TrigMode mode_ { TrigMode::GateTrig };
};

} // namespace mw::control
```

### 4.3 Resolution rules (normative)

- Priority: `LastNote` for `GateTrig`; `LowestNote` for `Gate` and `Lfo`
  [research/07 §3.2]. `LowestNote` selects the lowest set bit in `held`;
  `LastNote` selects the lowest of the keys in `justPressed`, falling back to the
  most-recent still-held key (track last-pressed index) [research/07 §3.2].
- `gateOn = (held != 0)` for `GateTrig`/`Gate`; in `Lfo` mode the gate is
  asserted while a key is held but the envelope re-fires on each `lfoEdge`
  [research/04 §2.3; research/06 §5].
- `retrigger`:
  - `GateTrig`: true on any `justPressed` (every new key) and on every seq/arp
    step (§5, §6) [research/07 §3.3].
  - `Gate`: true only on a transition from `held == 0` to `held != 0`
    (non-legato); a legato keypress (already-held note present) does NOT
    retrigger [research/06 §5; research/04 §2.3].
  - `Lfo`: true on each `lfoEdge` while a key is held [research/07 §3.3].
- This decision is consumed by the ADSR (retrigger), the VCA (gate), and the
  voice/pitch layer (selectedKey). Voice mode (MONO default per [ADR-016 R-3];
  POLY/UNISON) is owned by [ADR-006]; in MONO this is the single active voice.

### 4.4 Real-time invariants

`resolve()` is `noexcept`, branch-on-mode only, no allocation, no lock. The held
bitmap is a fixed `uint32_t`. Last-pressed tracking uses a fixed
`std::array<int8_t, 32>` stamped with a monotonic press counter — pre-sized in
`prepare`, never resized [ADR-007 C26].

## 5. Arpeggiator (`Arpeggiator`)

### 5.1 Model

Exactly three play directions, mutually exclusive: UP, U&D (up-and-down), DOWN
[ADR-007 §Decision 3, C7; research/06 §2.1; research/07 §5.4]. The arp on/off +
direction is modeled by `mw101.arp.mode`; octave span is `mw101.arp.range`; and
the persisted HOLD/latch state is `mw101.arp.latch` (the live arp latch control;
the persisted-latch flag is an `<extras>` member). Up to **32 held
keys** in a fixed 32-bit bitmap — this is a held-note bitmap, NOT 4-note
polyphony (explicit misreading guard) [ADR-007 C8; research/06 §2.4, §8.3;
research/07 §3.1]. Keys are cycled in held order with **no automatic octave
expansion**; octave shift is only via the global TRANSPOSE [ADR-007 C8, C9;
research/06 §2.2].

Engagement [ADR-007 C10; research/06 §2.3]:

- Arp engages on a chord / legato (two or more keys, or a key pressed while
  another is held).
- A single non-legato note plays normally (arp inactive).
- HOLD latch (panel button + DP-2 pedal) keeps the arp running after keys are
  released; a new chord while latched replaces the held set.

Advance: one step per clock H→L edge, on the SAME edge as the sequencer and
RANDOM [ADR-007 C17; research/06 §6.3; research/07 §5.4].

### 5.2 U&D turnaround (documented switchable choice)

The exact U&D turnaround math (whether the top and bottom notes repeat at the
turnaround) is only partially traced in the recovered firmware
[research/06 §2.1, §8.2; research/07 §5.4, §9]. It is therefore exposed as a
documented switchable choice, NOT asserted as bit-exact [ADR-007 C11]:

- `UandDRepeatEndpoints` (default = `false`, i.e. `1 2 3 4 3 2` not
  `1 2 3 4 4 3 2 1` for a 4-note set) **(PI)** — the default choice is a
  pragmatic invention centralized in `core/calibration/Calibration.h`
  (`kArpUandDRepeatEndpoints`), since no source fixes it.

### 5.3 Octave / transpose interaction

While the sequencer is running, global TRANSPOSE is disabled; KEY TRANSPOSE may
transpose a running sequence [ADR-007 C9; research/06 §2.2]. The arpeggiator
applies the global TRANSPOSE offset (consumed from the pitch layer); it adds no
octave range of its own [research/06 §2.2].

### 5.4 Signature

```cpp
namespace mw::control {

enum class ArpMode : uint8_t { Up = 0, UandD = 1, Down = 2 };

class Arpeggiator {
public:
    void prepare() noexcept;                  // clear bitmap; no alloc after
    void setMode (ArpMode m) noexcept;
    void setHold (bool latched) noexcept;     // panel + pedal OR'd by caller
    void setUandDRepeatEndpoints (bool b) noexcept;

    // Held-key maintenance (driven by Keyboard Read). RT-safe.
    void noteOn  (int key) noexcept;          // set bit
    void noteOff (int key) noexcept;          // clear bit unless HOLD latched
    bool isEngaged() const noexcept;          // chord/legato or HOLD latched

    // Advance one step on a clock H->L edge; returns the key to sound, or -1.
    int advanceOnEdge() noexcept;

    uint32_t heldBitmap() const noexcept { return held_; }
    int      heldCount()  const noexcept;     // popcount(held_)
private:
    uint32_t held_ = 0;
    ArpMode  mode_ = ArpMode::Up;
    bool     hold_ = false;
    bool     repeatEndpoints_ = false;
    int      cursor_ = 0;       // index into the ordered held list
    int      dir_    = +1;      // for UandD
};

} // namespace mw::control
```

`advanceOnEdge()` walks the set bits of `held_` in ascending key order
(UP / DOWN choose direction; U&D oscillates per §5.2). It is `noexcept`, uses
only the fixed bitmap + integer cursor, never allocates [ADR-007 C26].

## 6. 100-step sequencer (`StepSequencer`)

### 6.1 Step model

A 100-slot buffer, **one event per slot**. A note = one slot, a REST = one slot,
and each tie/long-note extension = one slot [ADR-007 §Decision 4, C12;
research/06 §3.2; research/07 §5.5]. The seq mode (Off/Play/Record) is modeled by
`mw101.seq.mode`. Per-step payload — **note / rest / tie / gate only**
[ADR-007 C13; ADR-025; research/06 §6.2; research/07 §5.5]:

- 6-bit pitch (low 6 bits, matching the 6-bit DAC range).
- REST flag.
- TIE/legato(slide) flag.
- **No per-step accent. No per-step gate-time** — per-step accent is REMOVED per
  [ADR-025]; accent/per-step gate-time are TB-303/MC-202 features the 101 lacks
  [ADR-007 §Resolution 3, C13; research/06 §8.1; research/07 §5.5, §8.1].

The internal byte/bit layout is a community-inferred, not Roland-documented,
detail [research/06 §8.2; research/07 §9 Q1]; the layout below is therefore an
**implementation choice**, NOT asserted bit-exact.

### 6.2 Storage layout

```cpp
namespace mw::control {

// One sequencer slot. POD, trivially copyable, fixed 1 byte.
struct SeqStep {
    uint8_t bits = 0;   // [b5:b0]=pitch (0..63), b6=REST, b7=TIE/legato
    static constexpr uint8_t kPitchMask = 0x3F; // research/07 §5.5 (anl #3fh)
    static constexpr uint8_t kRestFlag  = 0x40; // implementation choice; research/06 §8.2
    static constexpr uint8_t kTieFlag   = 0x80; // implementation choice; research/06 §8.2

    bool isRest() const noexcept { return bits & kRestFlag; }
    bool isTie()  const noexcept { return bits & kTieFlag; }
    int  pitch()  const noexcept { return bits & kPitchMask; }
};

inline constexpr int kMaxSteps = 100;            // research/06 §3.1, §7
using SeqBuffer = std::array<SeqStep, kMaxSteps>; // preallocated; never resized
```

The REST/TIE bit assignment is centralized symbolically so the
calibration/preset layer can pin it; the magic constants are flagged
implementation-choice per research/06 §8.2.

### 6.3 Transport

- `LOAD` toggles record; writes are taken **only from the instrument keyboard**;
  record auto-exits when all 100 slots are filled [ADR-007 C14; research/06 §3.1;
  research/07 §5.5].
- `PLAY` toggles wrap-around loop playback; advances one slot per clock H→L edge;
  wraps from slot `count-1` back to slot 0 [ADR-007 C15; research/06 §3.1].
- REST entry consumes one slot (gate dropped). TIE entry (next note played while
  LEGATO held) consumes one slot and sustains the envelope + engages portamento
  [ADR-007 C16; research/06 §3.2; research/07 §5.6].

### 6.4 Articulation (no per-step gate-time)

Articulation = stored ties + the global GATE/TRIG mode only
[ADR-007 §Decision 4, C16; research/06 §3.3; research/07 §5.6]:

- TIE step: sustains the envelope (no re-gate) and engages portamento (slide).
- REST step: drops the gate.
- Non-tie note: gates per the S7 mode (§4) — GATE+TRIG retriggers every step;
  GATE retriggers across non-legato transitions; LFO repeats at clock rate.

Whether a TIE strictly holds the envelope vs merely sustains depends on the
GATE/TRIG + portamento state and rests partly on community usage
[research/07 §5.6, §8.3]; the model honors the S7 contract above and treats the
TIE→sustain coupling as the defined behavior.

### 6.5 Signature

```cpp
namespace mw::control {

struct SeqPlayResult {
    int  pitch6   = 0;    // 6-bit pitch of the current slot
    bool gateOn   = true; // false on REST
    bool tie      = false;// sustain envelope + portamento
    bool retrigger= true; // false on TIE (and per S7 mode)
    int  slotIndex= 0;
};

class StepSequencer {
public:
    void prepare() noexcept;                 // zero buffer; no alloc after

    // LOAD path (message/key thread): keyboard-only writes.
    void setRecord (bool on) noexcept;       // LOAD toggle
    bool isRecording() const noexcept;
    void recordNote (int pitch6) noexcept;   // append note slot (if recording)
    void recordRest() noexcept;              // append REST slot
    void recordTie  (int pitch6) noexcept;   // append TIE slot
    void clear() noexcept;                   // reset count_ = 0

    // PLAY path (control tick).
    void setPlay (bool on) noexcept;         // PLAY toggle
    bool isPlaying() const noexcept;
    void resetToStart() noexcept;            // clock-reset re-phase (see Clock §7.5)

    // Advance one slot on a clock H->L edge; wraps. RT-safe, noexcept.
    SeqPlayResult advanceOnEdge() noexcept;

    int  count() const noexcept { return count_; }   // filled slots (<= 100)
    const SeqBuffer& buffer() const noexcept { return steps_; }
    void loadBuffer (const SeqBuffer& b, int count) noexcept; // preset restore

private:
    SeqBuffer steps_ {};
    int  count_   = 0;
    int  playPos_ = 0;
    bool recording_ = false;
    bool playing_   = false;
};

} // namespace mw::control
```

`advanceOnEdge()` reads `steps_[playPos_]`, decodes the flags, advances
`playPos_ = (playPos_ + 1) % count_` (no-op when `count_ == 0`), and is
`noexcept` with no allocation [ADR-007 C26]. Recording appends at `count_` and
auto-clears `recording_` when `count_` reaches `kMaxSteps` [ADR-007 C14;
research/06 §3.1].

## 7. Clock and host sync (`Clock`)

### 7.1 The single edge node

One H→L edge detector on a single clock node drives arp + seq + RANDOM reload
together; whatever source feeds the node, the downstream behavior is
phase-identical [ADR-007 §Decision 5, C17; research/07 §5.1, §6.3]. The hardware
senses the clock on the T1 pin as a high-to-low edge (the external rising edge at
+2.5 V is inverted by TR11) [research/07 §5.1, §8.1].

### 7.2 Three mutually-exclusive sources

| Source | Tempo driver | RATE role | Trace |
| --- | --- | --- | --- |
| `Internal` | LFO/CLK, 0.1–30 Hz | RATE sets tempo | C18; research/06 §4.1; research/04 §3.1 |
| `HostSync` | DAW PPQ via `AudioPlayHead` | RATE sets ONLY the LFO mod, NOT tempo | C19, C21; research/06 §4.2 |
| `Ext` | external CV/automation pulse, 1 pulse = 1 step | RATE sets ONLY the LFO mod, NOT tempo | C20, C21; research/06 §4.2 |

The sources are mutually exclusive, mirroring the hardware internal-vs-EXT
selector [ADR-007 §Decision 5]. Under `HostSync` or `Ext`, RATE controls only the
LFO as a mod source — the "EXT CLK cuts the internal clock" semantic — and must
be surfaced honestly in the UI, never silently re-mapped to tempo
[ADR-007 C21, §Consequences; research/06 §4.2; research/07 §5.1].

### 7.3 Internal clock

`Internal` runs the modeled LFO/CLK at **0.1–30 Hz** set by the RATE control;
edges are produced by the LFO core (consumed here) and are bit-identical to the
modeled hardware [ADR-007 C18; research/06 §4.1; research/04 §3.1]. The
knob-to-frequency taper within 0.1–30 Hz is undocumented [research/06 §4.1,
§8.4]; the taper is owned by the LFO/param subsystem (doc 06 §2), not minted
here. The endpoints 0.1 Hz and 30 Hz are the normative range
[research/04 §3.1; research/06 §7].

### 7.4 Host sync (contract)

Host-sync feeds the SAME edge detector as EXT CLK IN — no second sequencer
engine, no separate per-step durations or retrigger rules
[ADR-007 §Resolution 1, C19]. Per block:

1. Read transport from the core `TransportInfo` POD (doc 00 §5.3): PPQ position,
   BPM, `isPlaying`, time-signature. The plugin layer reads
   `juce::AudioPlayHead::PositionInfo` and fills this POD; no `juce::*` type
   crosses into core per [ADR-001]/D2.
2. Compute step period in quarter-notes from the host-rate selector (§8).
3. From the **absolute PPQ** at block start, compute the PPQ of the next step
   boundary; map each boundary that falls inside the block to a sample offset
   `(boundaryPpq - blockStartPpq) / ppqPerSample` and place an H→L edge there
   [ADR-007 C19, §Consequences].
4. Because phase is recomputed from absolute PPQ every block, tempo automation,
   loop wrap, and scrub just re-derive the next edge — no free-running counter to
   desync [ADR-007 §Resolution 1, §Consequences].

When `isPlaying` is false (transport stopped), no host edges are generated;
arp/seq hold position. One host pulse = one step, exactly as the hardware maps
one EXT pulse = one step [ADR-007 C19; research/06 §4.2].

### 7.5 Clock reset on keypress

On any new keypress while in **LFO-trigger mode OR arpeggio mode**, re-phase the
clock to that keypress; **default-on** [ADR-007 §Decision 5, C22; research/06
§4.3; research/07 §5.2]. Re-phase means: reset the edge-node phase accumulator
(Internal), or reset the next-boundary reference to the keypress sample so the
next host/EXT edge is measured from it; and call `StepSequencer::resetToStart()`
/ arp cursor reset as applicable [research/07 §5.2]. This is musically
load-bearing — arpeggios and LFO-clocked patterns lock to the keypress
[research/06 §4.3; research/07 §5.2]. The reset path is reused for host/EXT
re-phase per [ADR-007 §Consequences].

### 7.6 Swing (host-sync only)

SWING delays even-numbered step edges as a deterministic sample offset, **active
only under `HostSync`**, disabled/grayed under `Internal` and `Ext`, and labeled
a non-101 modern addition [ADR-007 §Resolution 2, C24; ADR-016 §accepted].
Range 50–75%, default 50% (= off) [ADR-007 C24]. At swing fraction `s`
(0.50–0.75), the offset added to even step boundaries (0-based, i.e. the 2nd,
4th, … step of each pair) is:

`swingOffsetSamples = (s - 0.5) * 2.0 * stepPeriodSamples` **(PI)** — the taper
has no hardware oracle; this linear map (50%→0, 75%→half a step) is a pragmatic
invention centralized in `core/calibration/Calibration.h`
(`kSwingTaper`) [ADR-007 §Resolution 2, §Owner-ratification; research/06 §8 —
swing has no oracle]. Swing never perturbs `Internal`/`Ext` timing
[ADR-007 C24].

### 7.7 Signature

```cpp
namespace mw::control {

enum class ClockSource : uint8_t { Internal = 0, HostSync = 1, Ext = 2 };

// Discrete musical rates for host-sync (the host-rate selector, C23).
enum class HostRate : uint8_t {
    Quarter = 0, Eighth, EighthT, Sixteenth, SixteenthT, ThirtySecond,
    DottedEighth, DottedSixteenth   // dotted variants (C23)
};

// Transport is the single core `mw::TransportInfo` POD defined in doc 00 §5.3
// (absolute PPQ at block start, BPM, isPlaying, sampleRate, numSamples). This
// doc does NOT re-declare it — the plugin layer fills it from
// `juce::AudioPlayHead`; no `juce::*` type appears in this core header (D2,
// ADR-001).

// An edge to be placed at a sub-block sample offset.
struct ClockEdge { int sampleOffset = 0; };

class Clock {
public:
    void prepare (double sampleRate) noexcept;   // size internal state; no alloc
    void setSource (ClockSource s) noexcept;
    void setInternalRateHz (float hz) noexcept;  // 0.1..30 (Internal only)
    void setHostRate (HostRate r) noexcept;      // host-sync only (C23)
    void setSwing (float fraction) noexcept;     // 0.50..0.75 (host-sync only, C24)
    void setClockResetOnKeypress (bool on) noexcept; // default true (C22)

    // Produce all H->L edges for this block into a pre-sized output (no alloc).
    // For Ext, extPulseOffsets are caller-supplied per-block pulse positions.
    void renderEdges (const mw::TransportInfo& t,   // doc 00 §5.3 core POD
                      std::span<const int> extPulseOffsets,
                      std::span<ClockEdge>  out,
                      int& outCount) noexcept;

    // Re-phase to a keypress at sampleOffset (clock-reset, C22).
    void resetToKeypress (int sampleOffset) noexcept;

    ClockSource source() const noexcept { return source_; }
    bool swingActive() const noexcept { return source_ == ClockSource::HostSync; }
private:
    ClockSource source_ = ClockSource::Internal;
    double sampleRate_ = 48000.0;
    double internalPhase_ = 0.0;     // Internal edge accumulator
    float  internalRateHz_ = 1.0f;
    HostRate hostRate_ = HostRate::Sixteenth;
    float  swing_ = 0.5f;
    bool   resetOnKeypress_ = true;
    bool   lastClockHigh_ = false;   // for H->L edge detection on the node
};

} // namespace mw::control
```

`renderEdges()` is `noexcept`, writes into the caller's pre-sized `out` span (no
allocation), and is the single producer of edges for all three sources
[ADR-007 C26, C27]. The `out` span is sized in `prepare` to the worst-case edges
per block (at the max sample rate / max host rate).

### 7.8 Host-rate to quarter-note table

| `HostRate` | Quarter-notes per step | Trace |
| --- | --- | --- |
| `Quarter` | 1.0 | C23 |
| `Eighth` | 0.5 | C23 |
| `EighthT` | 1.0/3.0 | C23 |
| `Sixteenth` | 0.25 | C23 |
| `SixteenthT` | 0.25/1.5 = 0.16667 | C23 |
| `ThirtySecond` | 0.125 | C23 |
| `DottedEighth` | 0.75 | C23 |
| `DottedSixteenth` | 0.375 | C23 |

The set of offered rates is fixed by [ADR-007 C23]; the formal parameter ID,
display strings, and automation flag are owned by `docs/design/06` §2.

## 8. Modern additions: gating and labeling

The host-rate selector (§7.8) and SWING (§7.6) are discrete automatable
parameters, **active only under `HostSync`** and grayed under `Internal`/`Ext`,
so they can never perturb modeled internal timing [ADR-007 §Decision 6, C23, C24;
ADR-016 §accepted]. They are labeled non-101 modern additions per the
honesty-labels requirement [ADR-013; ADR-016 §accepted]. Enable/gray logic:

- `Internal`: RATE = tempo; host-rate + swing grayed/inactive.
- `HostSync`: host-rate active, swing active, RATE = LFO-mod only.
- `Ext`: RATE = LFO-mod only; host-rate + swing grayed/inactive.

The UI keeps these visually segregated from the faithful single-knob core
[ADR-007 §Owner-ratification; ADR-015 — UI owns layout].

## 9. Persistence

### 9.1 What persists

The full control-core state is saved/restored to mirror the hardware's
battery-backed RAM [ADR-007 §Decision 7, C25; research/06 §3.4; research/07 §6]:

- 100-slot sequencer buffer + filled count (seq mode is `mw101.seq.mode`; sync is
  `mw101.seq.tempo_sync` / `mw101.seq.sync_div`).
- Arp mode (`mw101.arp.mode`), HOLD/latch (`mw101.arp.latch`; persisted latch is
  an `<extras>` member), arp range (`mw101.arp.range`), arp sync
  (`mw101.arp.tempo_sync` / `mw101.arp.sync_div`), U&D-repeat choice (PI).
- Clock source, internal rate (`mw101.lfo.rate`), host-rate, swing,
  clock-reset flag.
- Trigger-source mode (`mw101.key.trigger_priority`); PWM source; VCA source
  (`mw101.vca.mode`).

Per [ADR-016 §accepted / ADR-008], the 100 steps are saved **patch/project
state, not per-step host-automation lanes**. The container format and schema
version live in `docs/design/06`; this doc defines only the payload and the
RT-safe swap. Schema is versioned from **v1** [ADR-007 C25].

### 9.2 Snapshot model (RT-safe)

State I/O runs on the message thread; the audio thread reads an
atomically-swapped **immutable** snapshot — never a lock [ADR-007 §Decision 7,
C26; research/07 §11].

```cpp
namespace mw::control {

// Immutable, trivially copyable control-core snapshot. Read on audio thread.
// (Defined in the mw::control namespace alongside the engine's components.)
struct ControlSnapshot {
    SeqBuffer  seq {};
    int        seqCount = 0;
    ArpMode    arpMode  = ArpMode::Up;
    bool       arpHold  = false;
    bool       uAndDRepeatEndpoints = false;
    ClockSource clockSource = ClockSource::Internal;
    float      internalRateHz = 1.0f;
    HostRate   hostRate = HostRate::Sixteenth;
    float      swing = 0.5f;
    bool       clockResetOnKeypress = true;
    TrigMode   trigMode = TrigMode::GateTrig;
    PwmSource  pwmSource = PwmSource::Lfo;
    VcaSource  vcaSource = VcaSource::Env;
    uint32_t   schemaVersion = 1;     // versioned from v1 (C25)
};

} // namespace mw::control

// mw::seq::SequencerEngine — the arp/seq state machine (renamed from this doc's
// former "ControlCore" per D3, so it never collides with doc 04's
// `mw::ControlCore`). Lives in core/control/SequencerEngine.h. No `juce::*` type
// appears here (D2, ADR-001): (de)serialization to `juce::ValueTree` and host
// transport from `juce::AudioPlayHead` live in plugin/; this core class exposes
// only POD snapshots and consumes the doc-00 `mw::TransportInfo` POD.
namespace mw::seq {

class SequencerEngine {
public:
    void prepare (double sampleRate, int maxBlockSamples) noexcept; // pre-size

    // Message thread: build a fresh snapshot, publish via atomic swap.
    void publishSnapshot (const mw::control::ControlSnapshot& s); // may alloc OFF audio thread

    // Audio thread: hot. Reads current snapshot pointer atomically; no lock.
    void processBlock (const mw::TransportInfo& t,      // doc 00 §5.3 core POD
                       std::span<const mw::control::KeyEvent> keyIn,
                       std::span<mw::control::ControlEvent>   out,
                       int& outCount) noexcept;

    // POD-only state I/O (message thread). The engine reads/writes the POD
    // ControlSnapshot; juce::ValueTree (de)serialization of that snapshot is the
    // plugin layer's job (owned format = doc 06). No juce::* in this header.
    mw::control::ControlSnapshot captureState() const;                // off audio thread
    void restoreState (const mw::control::ControlSnapshot& s);        // off audio thread
private:
    std::atomic<const mw::control::ControlSnapshot*> live_ { nullptr };
    // double-buffered snapshot storage owned by the message thread
};

} // namespace mw::seq
```

The audio thread does `const ControlSnapshot* s = live_.load(std::memory_order_acquire);`
and reads through it; the message thread fills an inactive buffer and
`live_.store(newPtr, std::memory_order_release)`. No heap allocation, no lock on
the audio thread [ADR-007 C26]. The plugin layer maps the POD `ControlSnapshot`
to/from `juce::ValueTree` off the audio thread (D2).

### 9.3 Load failure

Malformed/forward-version state falls back to the INIT defaults (§1.3) per the
state/preset failure-handling policy [ADR-021 — referenced]; `schemaVersion`
mismatch handling is owned by `docs/design/06`.

## 10. Real-time invariants (summary)

- All hot paths (`ModRouter::resolve`, `TriggerSource::resolve`,
  `Arpeggiator::advanceOnEdge`, `StepSequencer::advanceOnEdge`,
  `Clock::renderEdges`, `SequencerEngine::processBlock`) are `noexcept`, allocate no
  heap, and take no lock [ADR-007 C26; research/07 §11].
- The arp 32-bit bitmap and the 100-slot `SeqBuffer` are fixed-size, pre-sized in
  `prepare`; LOAD/record and arp re-sort never heap-allocate [ADR-007
  §Consequences, C26].
- Parameter changes flow through the lock-free APVTS/atomic path; IDs/ranges are
  owned by `docs/design/06` §2 [ADR-007 C26; ADR-008].
- State I/O is off the audio thread; the audio thread reads an atomically-swapped
  immutable snapshot [ADR-007 C26].
- Clock edges are sample-accurate; the control state machine runs on a fixed
  control tick independent of edge placement [ADR-007 C27].

## 11. Acceptance hooks

A backlog task's tests MUST verify, objectively:

- **One edge → all three advance.** A single H→L edge on the clock node advances
  arp cursor, sequencer slot, and RANDOM reload on the same edge; they stay
  phase-consistent across Internal / HostSync / Ext sources [C17, C19, C20].
- **S7 coupling.** `GateTrig` ⇒ last-note priority + retrigger on every new key;
  `Gate` ⇒ lowest-note priority + no legato retrigger (single sustained gate);
  `Lfo` ⇒ lowest-note priority + envelope re-fired on each clock edge
  [C4, C5, C6]. GATE-mode priority is lowest-note, not high-note [research/07
  §8.2].
- **Fixed routing.** The same instantaneous LFO value reaches pitch/PWM/cutoff
  scaled by independent depths; PWM uses ENV only when source=ENV, LFO only when
  source=LFO; the shared ADSR drives cutoff/VCA/PWM with no second envelope
  [C1, C2, C3].
- **Arp 32-key bitmap, not 4-note poly.** 32 distinct held keys are all cycled;
  no automatic octave expansion; arp engages on chord/legato and a single
  non-legato note plays normally; HOLD latch survives key release [C7, C8, C10].
- **U&D switchable.** Turnaround behavior follows `UandDRepeatEndpoints`; both
  values produce the documented sequences; default is the calibration value
  [C11].
- **Seq slot model.** note/REST/each tie-extension each consume exactly one slot;
  payload is 6-bit pitch + REST + TIE only (note/rest/tie/gate); NO accent field
  (removed per [ADR-025]), NO per-step gate-time exists in the type [C12, C13;
  ADR-025].
- **Seq transport.** LOAD records keyboard-only and auto-exits at 100 slots; PLAY
  loops with wrap from last→first; advance is one slot per H→L edge [C14, C15].
- **Articulation.** REST drops the gate; TIE sustains the envelope + engages
  portamento; articulation derives only from ties + S7 mode [C16].
- **RATE under sync.** Under HostSync or Ext, RATE changes the LFO mod rate and
  does NOT change step tempo; under Internal, RATE sets tempo over 0.1–30 Hz
  [C18, C21].
- **Host phase from absolute PPQ.** Step edges are derived from absolute PPQ; a
  tempo change, loop wrap, or scrub re-derives the next edge with no cumulative
  drift; 1 host pulse = 1 step [C19].
- **Clock reset.** A new keypress in LFO-trigger or arpeggio mode re-phases the
  clock to that keypress sample; default-on; togglable off [C22].
- **Host-rate selector.** Each `HostRate` maps to its quarter-note period per
  §7.8; selector is active only under HostSync, grayed otherwise [C23].
- **Swing.** 50% = no offset; 75% = half-step offset on even steps; swing is
  inert (no edge change) under Internal and Ext; default 50% [C24].
- **Persistence round-trip.** Save→reload reproduces the full 100-slot buffer +
  arp + clock + trigger/PWM/VCA state bit-for-bit; `schemaVersion == 1` is
  written [C25].
- **RT-safety.** No heap allocation and no lock occur on the audio thread during
  `processBlock` across all sources and during a snapshot swap (verified by an
  allocation/lock sentinel in tests) [C26].
- **Tick vs edge decoupling.** Clock edges land at the expected sub-block sample
  offsets regardless of the control-tick period; the control tick defaults to the
  ~2 ms vintage rate [C27].

## 12. References

ADRs (normative):

- [ADR-007] Modulation routing, arpeggiator and 100-step sequencer.
- [ADR-016] Owner ratifications — out-of-box defaults.
- [ADR-008] Parameter / state / preset schema (parameter IDs owned here).
- [ADR-005] Control rate / 6-bit quantization (VINTAGE tick reference).
- [ADR-006] Voice / poly / unison model (MONO default).
- [ADR-012] MIDI / MPE / tuning.
- [ADR-013] Honesty labels.
- [ADR-015] UI architecture (panel layout / graying).
- [ADR-021] State / preset load-failure handling.

Research (cited factual ground truth):

- [research/06] `docs/research/06-arpeggiator-sequencer.md` — arp modes, 100-step
  model, clocking, clock-reset, persistence.
- [research/07] `docs/research/07-cpu-key-assigner.md` — single-CPU firmware,
  note priority, trigger semantics, edge detector, seq RAM layout.
- [research/04] `docs/research/04-envelope-lfo-vca.md` — shared ADSR, single LFO,
  fixed modulation routing, trigger-source switch, LFO→VCA tremolo.
