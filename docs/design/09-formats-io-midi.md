<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Plugin Formats, I/O, MIDI & MPE-lite

## 1. Scope and ownership

### 1.1 What this document specifies

This is the single source of truth for the **host-facing boundary** of mwAudio101:
the set of plugin formats and their per-format validators, the configure-time
"never ship an unvalidated artifact" gate, the MIDI / MPE-lite front-end that
translates host events into the engine's documented key-assigner / CV model, the
tuning reference, the cross-format capability-fallback contract, and the constant
plugin-delay-compensation (PDC) reporting policy.

All code described here lives in **`plugin/`** (the JUCE shell library `mwplugin`,
per ADR-001). The DSP engine in **`core/`** (`mwcore`, zero JUCE dependency,
per ADR-001) is format-agnostic and MIDI-agnostic: it consumes a normalized POD
`BlockContext` and never sees a `juce::MidiMessage`, a wrapper event queue, or a
host playhead. Every wrapper drains its native event/parameter/transport surface
into one normalized representation, so the macOS arm64 bit-exact bless reference
[ADR-001 C8, C14; ADR-011 C11] holds verbatim across all formats.

### 1.2 What this document does NOT own (reference only)

- **Parameter IDs, ranges, units, skews, defaults, and automatable classification**
  are owned by **docs/design/06 §2** (the schema), per ADR-008. This document
  REFERENCES parameter IDs by their stable `mw101.`-namespaced string (e.g.
  `mw101.vcf.cutoff`) and never re-mints an ID, range, or default. Where a MIDI CC
  or note-expression lands on a parameter, the binding is given as
  `CC -> mw101.<id>`; the authoritative range/skew is doc 06.
- **The key-assigner, voice/poly/unison model, and 6-bit DAC quantizer** are owned
  by the modulation/voice design docs (ADR-006, ADR-007); the MIDI front-end FEEDS
  them and is bound by their semantics (ADR-012 §1, §4.2 of `07`).
- **The arp + 100-step sequencer DSP and clock edge detector** are owned by the
  modulation design doc (ADR-007). This document owns only the per-format
  *transport capability normalization* that feeds them (ADR-022).
- **Oversampling, Drive placement, FX, and the group-delay sources** are owned by
  ADR-004 / ADR-010 / ADR-017; this document owns only the *PDC reporting* of the
  resulting constant latency (ADR-017).
- **Calibration constants** flagged `(PI)` (pragmatic invention) below centralize in
  `core/calibration/Calibration.h` (created by the backlog); this document names
  them but does not own their numeric tuning.

### 1.3 Real-time invariants (apply to every section)

- No heap allocation and no lock on the audio thread. Every buffer, voice-pool
  slot, smoother, normalized event buffer, and padding delay line is sized once in
  `prepare`/`prepareToPlay` and never resized from `processBlock` [ADR-011 C9;
  ADR-012 §Consequences; ADR-017 L10].
- Hot paths are `noexcept`. The MIDI parser, capability shim, CC/bend/pressure
  de-zipper, and PDC read are all allocation-free and lock-free on the audio thread.
- The CC/learn map and the resolved capability rungs are published from the message
  thread via a single-writer lock-free atomic-pointer swap (double-buffer); the
  audio thread only reads the current pointer [ADR-012 C16; ADR-022 C11-C12].
- Capability rungs (note-expression, transport) are resolved at init /
  `prepareToPlay` and re-checked per block via a branch-free cached-pointer read; no
  rung transition allocates or locks [ADR-022 C11].

## 2. Plugin formats and the validator gate

### 2.1 Format / platform / toolchain matrix

Formats are built from **one** `juce_add_plugin` target over the shared
`AudioProcessor` (the thin shell wrapping `mwcore`); there is no DSP fork
[ADR-011 Decision; ADR-024 Decision]. The matrix is sparse and tied to the
owner-locked platform tiers [ADR-011 §Decision table; ADR-024 Contract table].

| Format | Toolchain | Platforms | Validator(s) | Tier |
| --- | --- | --- | --- | --- |
| VST3 | JUCE native exporter | macOS arm64, Linux x64, Windows x64 | `pluginval` + Steinberg `validator` | macOS bless / Linux hard gate / Windows goal |
| AU | JUCE native exporter | macOS arm64 only | `auval` + `pluginval` | macOS bless (macOS-only by construction) |
| CLAP | `clap-juce-extensions` (wraps shared `AudioProcessor`) | macOS arm64, Linux x64, Windows x64 | `clap-validator` (+ `pluginval` where it exercises CLAP) | macOS bless / Linux hard gate / Windows goal |
| Standalone | JUCE native exporter | macOS arm64, Linux x64, Windows x64 | headless smoke-launch (+ `pluginval` engine checks) | macOS bless / Linux hard gate / Windows goal |
| LV2 | JUCE **native** LV2 exporter ONLY (no `clap-wrapper`) | Linux x64 only at launch | `lv2lint` + `lv2_validate` (lilv) | goal-tier, non-blocking; ships only when green |
| AAX | none (permanently excluded) | none | none (no open validator) | out of scope — configure-time error on any platform |

Sources: format scope and tiers [ADR-011 §Decision]; LV2 = JUCE-native-only,
`clap-wrapper` route withdrawn [ADR-024 Decision, C1-C2]; AAX permanent exclusion
[ADR-011 C4; ADR-024 C6]; market rationale for the Linux/CLAP/LV2 breadth
[research/12 §7.3].

### 2.2 Configure-time "no unvalidated artifact" gate

The rule — *never build a format on a platform where its validator is not wired* —
is enforced at CMake configure time, not in CI or code review [ADR-011 Decision,
C5]. Each requested `(format, platform)` pair maps to a required validator target;
if that validator is not found/wired for the platform the format is **hard-removed**,
and force-adding it is a **configure-time error**.

Backlog files (created by the backlog, named here as the citation address):

- `cmake/Formats.cmake` — resolves `MWAUDIO_FORMATS` per platform, maps each
  `(format, platform)` to its required validator target, hard-removes unwired
  formats, and `message(FATAL_ERROR ...)` on any forced unwired or excluded format.
- `cmake/Validators.cmake` — locates / declares the validator targets
  (`pluginval`, Steinberg `validator`, `auval`, `clap-validator`, headless
  Standalone smoke, `lv2lint`, `lv2_validate`).
- `CMakePresets.json` — per-platform format sets: macOS = VST3+AU+CLAP+Standalone;
  Linux = VST3+CLAP+Standalone (+LV2 goal); Windows = VST3+CLAP+Standalone
  [ADR-024 Decision, unchanged from ADR-014].

Normative configure cases [ADR-011 C1-C8; ADR-024 C1-C6]:

| ID | Configure condition | Required behavior |
| --- | --- | --- |
| F-1 | VST3 / CLAP / Standalone on macOS, Linux, or Windows | Format builds; its validator(s) per §2.1 are required CI gates. |
| F-2 | AU on macOS | AU builds; `auval` + `pluginval` are required gates. |
| F-3 | AU on Linux or Windows | Configure-time error: AU is hard-removed (no `auval` off macOS). |
| F-4 | Any AAX target on any platform | Configure-time error: AAX is unconditionally, permanently excluded. |
| F-5 | A format whose validator is not wired for that platform | Configure-time error; the format is hard-removed; force-adding it fails the configure. |
| F-6 | LV2 (Linux x64) | Emitted by JUCE's native LV2 exporter behind `MW_BUILD_LV2`; no `clap-wrapper` vendored/pinned/invoked. Ships only when `lv2lint` AND `lv2_validate` are wired and green; absence never blocks the Linux gate. |
| F-7 | Linux x64 release gate | Must ship VST3 + CLAP + Standalone, each with its validator green; LV2 optional. |
| F-8 | macOS arm64 release | Must ship VST3 + AU + CLAP + Standalone, each green; this build is the bit-exact bless reference. |

### 2.3 Why AAX is permanently excluded

AAX requires Avid PACE signing/licensing, which is incompatible with shipping
GPL-3.0-or-later binaries, and has **no open validator**, so it can satisfy neither
the license lock nor the validator rule. This is a deliberate, permanent exclusion,
not a deferral — there is no GPL-3.0 condition under which it is reconsidered
[ADR-011 C4; ADR-024 C6, Decision].

## 3. Shell architecture and the normalized event boundary

### 3.1 The thin-shell adapter (`plugin/`)

```
core/   (mwcore, JUCE-free)        plugin/   (mwplugin, JUCE shell)
                                   ┌──────────────────────────────────────────┐
                                   │ MwAudioProcessor : juce::AudioProcessor   │
  Engine                           │   - HostEventNormalizer  (§3.2)           │
  ::prepare(sr, maxBlock, maxV) <──┤   - MidiFrontEnd         (§4)             │
  ::process(const BlockContext&)<──┤   - CapabilityShim       (§7)             │
  ::reset()                        │   - LatencyReporter      (§8)             │
                                   └──────────────────────────────────────────┘
   (no juce_* include in core/, CI-guarded, ADR-001 C1)
```

Every format wrapper (VST3 / AU / CLAP / Standalone / LV2) differs **only** in
`plugin/`; the `core` render path is identical across all formats [ADR-001 C14;
ADR-011 Decision; ADR-024 C5]. CLAP is produced by `clap-juce-extensions` wrapping
the same `MwAudioProcessor`; LV2 by JUCE's native exporter on the same processor.

### 3.2 Normalized event buffer

Each wrapper's native parameter/event/note model — CLAP's sample-accurate typed
event queue, VST3 param queues + `INoteExpression`, AU's per-block model + raw
multi-channel MIDI, LV2 atom ports, Standalone raw MIDI — is drained into **one**
fixed-capacity, lock-free internal event buffer, sized at `prepareToPlay`, then fed
into the engine via `BlockContext` [ADR-011 C9; ADR-024 C7]. A no-allocation
assertion is compiled into debug/CI builds around `processBlock`.

```cpp
// plugin/host/HostEvent.h
namespace mw::plugin {

enum class HostEventType : uint8_t {
    NoteOn, NoteOff, PitchBend, ChannelPressure, PolyPressure,
    ControlChange, ProgramChange, ClockEdge, ParamValue
};

struct HostEvent {                 // POD; trivially copyable
    HostEventType type;
    uint8_t       channel;         // 1..16 (MPE member or global)
    int32_t       sampleOffset;    // sub-block sample position (>=0)
    int32_t       data0;           // note number / CC number / param index
    float         value;           // normalized value or signed offset
    int32_t       noteId;          // CLAP note id; -1 if none / MIDI-derived
};

class NormalizedEventBuffer {      // pre-sized; no alloc on audio thread
public:
    void prepare (int maxEventsPerBlock) noexcept;   // alloc here only
    bool push (const HostEvent&) noexcept;           // false if full (drop, never grow)
    void clear () noexcept;
    const HostEvent* begin() const noexcept;
    const HostEvent* end()   const noexcept;
    int  size() const noexcept;
private:
    std::vector<HostEvent> storage_;   // capacity fixed in prepare()
    int count_ = 0;
};

} // namespace mw::plugin
```

Capacity default: `maxEventsPerBlock = 4 * maxBlockSize + 256` (PI) — generous head
room for dense automation + per-sample MPE; centralizes in
`core/calibration/Calibration.h`. Overflow drops the lowest-priority surplus event
(ParamValue before NoteOn/Off) and asserts in debug; it never allocates [ADR-011 C9].

### 3.3 The core-side `mw::core::MidiEvent` POD and the `HostEvent` -> `MidiEvent` translation

`HostEvent` (§3.2) is the **plugin-side** drain of each wrapper's native surface and
carries JUCE/format-shaped fields (channel 1..16, CLAP note id, raw CC numbers). The
**core** engine never sees a `HostEvent`, a `juce::MidiMessage`, or a wrapper queue;
it consumes a normalized, JUCE-free POD owned and defined **here** (D1 resolution): the
`mw::core::MidiEvent`. It lives in `core/` (zero JUCE dependency, per ADR-001) and is
the sole note/gate/expression representation the engine ingests via `BlockContext`.

```cpp
// core/midi/MidiEvent.h   (mwcore; NO JUCE)
namespace mw::core {

enum class NormalizedType : uint8_t {
    NoteOn, NoteOff, PitchBend, ChannelPressure, PolyPressure,
    ControlChange, ClockEdge, ParamValue
};

struct MidiEvent {            // POD; trivially copyable; JUCE-free
    NormalizedType type;
    int8_t  channel;          // MPE member or global (lower-zone master = 1)
    int16_t noteId;           // engine note id; -1 if none / MIDI-derived
    float   data0;            // note number / CC number / param index (as float)
    float   value;            // normalized value or signed offset
    int     sampleOffset;     // sub-block sample position (>=0)
};

} // namespace mw::core
```

The plugin shell performs the `HostEvent` -> `mw::core::MidiEvent` translation in
`plugin/` (the only place that touches both types); `core/` defines and consumes
`MidiEvent` only. The mapping is field-for-field and allocation-free:

| `HostEvent` field | `mw::core::MidiEvent` field | Translation |
| --- | --- | --- |
| `type` (`HostEventType`) | `type` (`NormalizedType`) | enum remap; `ProgramChange` is consumed in `plugin/` (preset recall) and is NOT forwarded; all other types map 1:1 |
| `channel` (`uint8_t` 1..16) | `channel` (`int8_t`) | narrowing copy; lower-zone master = 1 |
| `noteId` (`int32_t`; CLAP id or -1) | `noteId` (`int16_t`) | engine note id; -1 preserved for MIDI-derived events |
| `data0` (`int32_t`) | `data0` (`float`) | note/CC/param index widened to float; CC numbers resolved through the §6 learn map to a param index before forwarding |
| `value` (`float`) | `value` (`float`) | copied verbatim (normalized value or signed offset) |
| `sampleOffset` (`int32_t`) | `sampleOffset` (`int`) | sub-block offset copied |

This translation is the single boundary at which format-specific shape is erased: all
five wrappers emit identical `mw::core::MidiEvent` streams for identical input, so the
macOS arm64 bit-exact bless reference holds across formats [ADR-001 C11, C14;
ADR-011 C11]. Doc 00 cross-references **this section** (not doc 06) for the
`mw::core::MidiEvent` definition and the `HostEvent` -> `MidiEvent` translation.

## 4. MIDI front-end (note / gate -> engine)

The MIDI front-end is a lock-free, allocation-free clone-layer translator that feeds
the documented key-assigner / DAC-CV model; it is never a parallel control path
[ADR-012 §Decision]. There is no SH-101 MIDI oracle — the stock instrument has zero
MIDI [research/08 §2.1, §2.3] — so every choice here is a disciplined clone-layer
policy that translates *into* documented CV/gate/key-assigner behavior.

### 4.1 `MidiFrontEnd` responsibilities and signature

File: `plugin/midi/MidiFrontEnd.h/.cpp`.

```cpp
// plugin/midi/MidiFrontEnd.h
namespace mw::plugin {

class MidiFrontEnd {
public:
    void prepare (double sampleRate, int maxBlockSize) noexcept;  // sizes smoothers
    void reset () noexcept;

    // Drains the wrapper's juce::MidiBuffer (already in the wrapper's MPE/raw form)
    // and the resolved capability rungs into normalized HostEvents + per-voice
    // expression. RT-safe; no alloc/lock.
    void processMidi (const juce::MidiBuffer& midi,
                      const CcLearnMap& map,        // §6 (read via atomic ptr)
                      NoteExpressionRung neRung,    // §7 (resolved by CapabilityShim)
                      NormalizedEventBuffer& out) noexcept;

    // A4 reference + TUNE; bend ranges; modern-unquantized flag (read from params)
    void setTuning (float a4Hz, float tuneCents) noexcept;        // §5
    void setBendRange (float channelSemis, float mpeNoteSemis,
                       float mpeMasterSemis) noexcept;            // §4.4
    void setModernUnquantized (bool on) noexcept;                 // §4.3 C7
private:
    // fixed-cost one-pole de-zippers (O(1)/sample), no branch on message arrival
    OnePoleSmoother bendSmoother_, channelPressureSmoother_;
    // bend/pressure are continuous, applied as offsets; quantizer lives in core
};

} // namespace mw::plugin
```

### 4.2 Trigger / Priority (the coupled S7 switch)

Note-on/off drive one key-assigner mirroring the coupled S7 switch as a **single**
"Trigger/Priority" parameter with three values; priority and trigger are NEVER two
independent parameters [ADR-012 C1-C4; research/07 §3.2-3.3, §11]. The parameter is
owned by doc 06 (`mw101.key.trigger_priority`, 3-value choice); this front-end only
routes notes into the assigner.

| Value | Priority | Retrigger behavior |
| --- | --- | --- |
| GATE | lowest-note | legato keypress does NOT retrigger envelopes (gate held) |
| GATE+TRIG | last-note | every new note retriggers both envelopes |
| LFO | lowest-note | envelopes (re)triggered by LFO/clock, not the keyboard |

Poly/unison instantiate N copies of this same assigner; the mono path stays
bit-exact [ADR-012 §Decision item 1]. Out-of-range notes clamp to the modeled 32-key
span edge [ADR-012 C6; research/07 §3.1; research/08 §3.1 CV-IN 0-7 V semantics].

### 4.3 Pitch path (6-bit quantized, 1 V/oct)

Note number assembles additively (key + range + octave + key-shift) and passes
through the **6-bit DAC quantizer at exactly 1 V/oct** before exponentiation, in
`core` — MIDI must NOT bypass quantization, preserving the stair-stepped portamento
fingerprint [ADR-012 C5; research/07 §4.2; research/08 §4, §10 item 1]. Range/Transpose
CV mapping is the documented hardware table (this front-end passes the assembled
semitone index; the CV scaling lives in `core`):

| Control | CV | Confidence | Source |
| --- | --- | --- | --- |
| Range 16' | 1 V | high | research/08 §4 |
| Range 8' | 2 V | high | research/08 §4 |
| Range 4' | 3 V | high | research/08 §4 |
| Range 2' | 4 V | high | research/08 §4 |
| Transpose lowest F at 16' | 0.417 V | medium (anchor verified) | research/08 §4 |

A clearly-labeled, **off-by-default** "modern un-quantized pitch" option may bypass
the quantizer for smooth MPE glides; OFF is the shipped default [ADR-012 C7; flagged
clone deviation]. The toggle is parameter `mw101.pitch.modern_unquantized` (owned by
doc 06). Note: this is distinct from the out-of-box MODERN-SMOOTH control default
[ADR-016 R-1], which is the ADR-005 Vintage Control macro, not this MPE escape hatch.

### 4.4 Pitch-bend

Channel pitch-bend is a **continuous** offset applied *before* quantization (the
analog bender + front-panel TUNE are the only continuous pitch controls)
[ADR-012 C8; research/08 §4, §7.4]. Bend ranges (parameters owned by doc 06):

| Bend | Default | Range | Source |
| --- | --- | --- | --- |
| Channel bend range | ±2 semitones | 0..24 | ADR-012 C8 |
| MPE per-note bend range | ±48 semitones | 0..96 | ADR-012 C11 (MPE spec default) |
| MPE master bend range | ±48 semitones | 0..96 | ADR-012 C11 (MPE spec default) |

### 4.5 Velocity (ON by default, per ADR-016)

The stock SH-101 keyboard has no velocity [research/07 §3.2], so velocity is a
clone-layer policy choice. Per the owner ratification superseding ADR-012 C9:

- **Default = velocity ON.** Velocity routes to **VCA level** and **VCF cutoff
  amount** — the documented physical nodes [ADR-016 R-2, Decision item 2;
  research/08 §7.2, §5.3]. This is additive over real circuitry, not invented
  structure.
- The faithful **no-velocity** behavior is a one-action switch (the velocity
  switch); it also remains the correct setting for faithful A/B testing
  [ADR-016 R-2].

The velocity-on/off switch and its routing depth are parameters owned by doc 06; the
front-end applies the routing to the normalized per-voice level / cutoff offsets.

## 5. Tuning reference (A4) and TUNE

A4 is a **single float parameter, default 440 Hz** (owner mandate), over a
~400-460 Hz range [ADR-012 C21; ADR-012 §Decision item 7]. The documented hardware
calibration is **A4 = 442 Hz** via the VR-7 procedure [research/08 §4, §5.2, §11,
§12 — "high" confidence], provided as a recallable "hardware-accurate" preset, never
the default [ADR-012 C22].

| Item | Value | Unit | Default | Source |
| --- | --- | --- | --- | --- |
| A4 tuning reference (param `mw101.tune.a4`, owned by doc 06) | 400..460 | Hz | **440** | ADR-012 C21 |
| "Hardware-accurate" preset A4 | 442 | Hz | (preset only) | research/08 §5.2; ADR-012 C22 |
| Front-panel TUNE (param `mw101.vco.fine`, owned by doc 06) | ±1.0 | semitones | 0 | research/08 §7.4, §12; ADR-012 C23 |

TUNE is a **separate** ±1.0-semitone continuous control (`mw101.vco.fine`, the sole
fine-tune; doc 09 TUNE re-points here) layered on top of the A4 reference
[ADR-012 C23; research/08 §7.4]. MTS-ESP / MIDI Tuning Standard is honored
only if cheap; the master reference is the single A4 float param [ADR-012 §Decision
item 7]. Because the default path is 6-bit quantized, continuous microtuning fights
the quantizer; the "modern un-quantized pitch" flag (§4.3) is the escape hatch
[ADR-012 §Consequences].

UX note (residual risk to signpost in UI/preset notes): the 440-vs-442 duality must
be surfaced or users mistrust tuning [ADR-012 §Options, §Consequences].

## 6. CC / automation map

### 6.1 Two-layer model

- **Primary, sample-accurate surface:** every modeled panel control is an APVTS host
  automation parameter (IDs/ranges owned by doc 06) [ADR-012 C14]. This is the
  canonical automation path.
- **Secondary default MIDI-CC map** + a **fully user-remappable MIDI-learn table**
  [ADR-012 C15]. No CC invents a control the hardware lacks.

### 6.2 Default CC map

CC numbers bind to parameter IDs owned by doc 06 (this table is the *binding*, not
the parameter definition) [ADR-012 C15, C20; research/08 §2.1; research/07 §7.1 T0]:

| CC | Function | Binds to (doc 06 ID) | Source |
| --- | --- | --- | --- |
| CC1 | Modulation | `mw101.mod.lfo_mod_wheel` | ADR-012 C15 |
| CC7 | Volume | `mw101.vca.level` | ADR-012 C15 |
| CC11 | Expression | `mw101.amp.expression` | ADR-012 C15 |
| CC74 | Cutoff / brightness | `mw101.vcf.cutoff` | ADR-012 C15 |
| CC71 | Resonance | `mw101.vcf.resonance` | ADR-012 C15 |
| CC5 | Portamento time | `mw101.glide.time` | ADR-012 C15 |
| CC64 | Sustain -> HOLD | HOLD / external-HOLD semantics (real stock jack) | ADR-012 C20; research/08 §2.1 |

CC64 sustain drives the HOLD / external-HOLD input semantics — a real stock jack
(DP-2 footswitch) [research/08 §2.1, §12; ADR-012 C20], not an invented latch.

### 6.3 RT-safe learn map

File: `plugin/midi/CcLearnMap.h/.cpp`.

```cpp
// plugin/midi/CcLearnMap.h
namespace mw::plugin {

struct CcBinding {                 // POD
    uint8_t ccNumber;              // 0..127
    int32_t paramIndex;            // index into the doc-06 ParamDefs registry
    bool    enabled;
};

class CcLearnMap {                 // double-buffered, single-writer atomic swap
public:
    // message thread: edit a copy, then publish() to swap the live pointer.
    CcBinding* editableCopy() noexcept;          // returns the inactive buffer
    void publish() noexcept;                     // atomic store of live pointer
    // audio thread: read-only, branch-free.
    int32_t lookup (uint8_t ccNumber) const noexcept;   // -1 if unmapped
private:
    std::array<CcBinding, 128> bufferA_, bufferB_;
    std::atomic<const std::array<CcBinding,128>*> live_;  // points to A or B
};

} // namespace mw::plugin
```

Runtime MIDI-learn / map edit is a lock-free single-writer atomic swap; no mutex or
allocation on the audio thread [ADR-012 C16]. The same atomic-pointer pattern
publishes the capability rungs to the UI (§7.4) [ADR-022 C12].

### 6.4 De-zipper

Bend / pressure / CC value changes are de-zippered via fixed-cost one-pole / linear
smoothers, O(1) per sample, with no branch on message arrival [ADR-012 C24]. CC
events are timestamped (sampleOffset) and applied at block sub-sample offsets without
allocation [ADR-012 §Consequences]. Smoothing time-constants are governed by the
parameter-smoothing policy (ADR-020) and centralize in `core/calibration/Calibration.h`.

## 7. MPE-lite and the cross-format capability ladder

### 7.1 MPE-lite scope (ADR-012)

"Lite" = **lower zone only**, configurable member-channel count, **default OFF
(0 members); opt-in 1..15** [ADR-012 C10; ADR-016 affirms scope]. Bounded to:

- **Per-note pitch-bend** -> per-voice continuous **pre-quantizer** pitch offset
  [ADR-012 C11].
- **Per-note pressure** (channel pressure / CC74) -> **ONE assignable destination,
  default VCF cutoff CV** [ADR-012 C12; research/08 §7.2, §5.3]. The cutoff CV path
  is the documented, SDT-1000-scaled physical node.
- **No per-note timbre matrix** — the hardware has one VCF and one VCA, so faithful
  modeling forbids inventing per-note analog structure [ADR-012 C12; research/08
  §7.2-7.3].

In mono mode, MPE collapses to channel pitch-bend + channel pressure [ADR-012 C13].
The assignable pressure-destination selector is a parameter owned by doc 06; the
default value is the VCF cutoff CV node.

### 7.2 The note-expression capability ladder (ADR-022)

Host note-expression support varies per format, so each wrapper resolves a
`NoteExpressionRung` once and feeds the engine the *same* normalized per-voice
bend/pressure regardless of rung [ADR-022 §Decision item 1, C1-C3]. The engine is
capability-agnostic; the wrapper is capability-aware.

```cpp
// plugin/host/Capabilities.h
namespace mw::plugin {

enum class NoteExpressionRung : uint8_t {
    Native,        // CLAP typed note-expressions
    MpeOverMidi,   // VST3/AU/LV2/Standalone: reconstruct per-channel MIDI -> per-voice
    Collapsed      // global channel bend + channel pressure (universal floor)
};

enum class TransportRung : uint8_t {
    SampleAccurate, // CLAP transport event, sub-block edges
    BlockQuantized, // VST3/AU/LV2: one PositionInfo/block, edge at block boundary
    FreeRun         // no transport: INTERNAL clock at RATE knob
};

struct ResolvedCapabilities {       // POD; published via atomic ptr (§6.3 pattern)
    NoteExpressionRung noteExpr;
    TransportRung      transport;
};

} // namespace mw::plugin
```

| Rung | Formats | Behavior | Source |
| --- | --- | --- | --- |
| Native | CLAP | `CLAP_NOTE_EXPRESSION_TUNING` -> per-voice pre-Q pitch offset; `..._PRESSURE` -> assignable destination | ADR-022 C1 |
| MPE-over-MIDI | VST3, AU, LV2, Standalone (MPE-lite ON) | reconstruct per-channel bend + channel pressure / CC74 into the SAME per-voice offset; lower-zone only, members 1..15 | ADR-022 C2 |
| Collapsed (global) | any format, MPE-lite OFF or no per-note channel data | per-note expression -> channel bend + channel pressure globally; bit-identical to running without MPE | ADR-022 C3 |

All three rungs feed the identical engine path; only the *source* of the per-voice
offset differs, so the macOS bless reference holds per rung [ADR-022 C4; ADR-011 C11].

### 7.3 The MPE-over-MIDI reconstruction parser

File: `plugin/midi/MpeReconstructor.h/.cpp`. For formats with no note-expression API
(VST3 without MPE-mode handling, AU/Logic, LV2, Standalone), raw per-channel MIDI is
reconstructed into the per-voice offsets the Native rung produces [ADR-022 §Decision
item 1, C2; §Consequences]. Lower-zone only, member channels 1..15 [ADR-012 §4].

```cpp
// plugin/midi/MpeReconstructor.h
namespace mw::plugin {

class MpeReconstructor {           // per-channel rotation parser; RT-safe
public:
    void prepare (int maxVoices) noexcept;   // sizes per-channel state
    void reset () noexcept;
    // Returns the per-voice pre-quantizer pitch offset + pressure for a channel.
    // No alloc; lower-zone master = channel 1, members 2..16 (configurable count).
    void noteOn  (uint8_t channel, uint8_t note, uint8_t vel) noexcept;
    void noteOff (uint8_t channel, uint8_t note) noexcept;
    void pitchBend (uint8_t channel, float semis) noexcept;     // per-note Pre-Q
    void pressure  (uint8_t channel, float norm) noexcept;      // -> destination
private:
    std::array<int, 16> channelToVoice_;   // fixed; -1 if free
};

} // namespace mw::plugin
```

### 7.4 Active-rung visibility

Both resolved rungs are published to the UI via the lock-free atomic-pointer path
(§6.3) so a Collapsed or Free-run state is **user-visible, not a silent surprise**
[ADR-022 C12, §Consequences]. The UI shows note-expression rung (Native /
MPE-over-MIDI / Collapsed) and transport rung (Sample-accurate / Block-quantized /
Free-run).

## 8. Transport normalization and constant PDC

### 8.1 Transport capability ladder (ADR-022)

The arp + 100-step seq clock is normalized per format. The engine always runs
ADR-007's single H-to-L edge detector; the wrapper feeds it the best available
transport representation [ADR-022 §Decision item 2, C5-C10].

| Rung | Formats | Edge placement | Source |
| --- | --- | --- | --- |
| Sample-accurate | CLAP | H-to-L edges at exact sub-block sample offsets | ADR-022 C5; ADR-007 C19 |
| Block-quantized | VST3, AU, LV2 | one `AudioPlayHead::PositionInfo`/block; edge at the block boundary containing its absolute-PPQ position; phase recomputed from absolute PPQ each block; count/order identical to Sample-accurate (≤1 block jitter) | ADR-022 C6; ADR-007 C19 + Consequences |
| Free-run | Standalone; stopped / playhead-less host | falls back to ADR-007 INTERNAL clock (LFO/CLK 0.1-30 Hz) at the RATE knob; reading the absent playhead is allocation-free | ADR-022 C7; ADR-007 C18; ADR-011 C10 |

Per-format capability matrix [ADR-022 Contract table]:

| Format | Note-expression rung | Transport rung | Tempo / PPQ |
| --- | --- | --- | --- |
| VST3 | MPE-over-MIDI when ON; else Collapsed | Block-quantized; Free-run if no transport | Present with host transport; else Free-run |
| AU | MPE-over-MIDI when ON (no NE API); else Collapsed | Block-quantized; Free-run if no transport | Present with host transport; else Free-run |
| CLAP | Native; Collapsed if none sent | Sample-accurate; Free-run if no transport | Present with host transport; else Free-run |
| LV2 | MPE-over-MIDI (raw atoms) when ON; else Collapsed | Block-quantized; Free-run if no transport | Present with host transport; else Free-run |
| Standalone | MPE-over-MIDI (raw port) when ON; else Collapsed | Free-run (no host transport) | Absent -> Free-run (RATE drives INTERNAL) |

Additional transport behavior: the single H-to-L edge detector advances arp + seq +
RANDOM together, and RATE decouples from tempo under HOST-SYNC/EXT [ADR-022 C9;
ADR-007 C17, C21]. SWING and the host-rate selector are active only under
Sample-accurate or Block-quantized rungs; greyed under Free-run [ADR-022 C10;
ADR-007 C23-C24]. If the user selects HOST-SYNC but no transport exists, HOST-SYNC
silently behaves as INTERNAL until a transport appears, then re-locks from absolute
PPQ with no allocation [ADR-022 C8]. The EXT-CLK rising-edge model (threshold ~+2.5 V,
no song-position) is the documented hardware behavior [research/08 §3.3, §10 item 2;
ADR-012 C18-C19]. CLOCK RESET re-phases on note-on in LFO-trigger or ARPEGGIO mode
[ADR-012 C19; research/07 §5.2].

### 8.2 `CapabilityShim` signature

File: `plugin/host/CapabilityShim.h/.cpp`.

```cpp
// plugin/host/CapabilityShim.h
namespace mw::plugin {

class CapabilityShim {
public:
    // Resolved once at init / prepareToPlay (per-format static capability +
    // current host query). No alloc/lock.
    ResolvedCapabilities resolve (PluginFormat fmt,
                                  bool mpeLiteEnabled,
                                  const juce::AudioPlayHead* playhead) noexcept;

    // Per-block: branch-free recheck of transport presence via cached pointer.
    // Returns the (possibly updated) rungs; never allocates / locks.
    ResolvedCapabilities recheckPerBlock (const juce::AudioPlayHead* playhead) noexcept;

    void publishToUi (const ResolvedCapabilities&) noexcept;  // atomic ptr swap
};

enum class PluginFormat : uint8_t { VST3, AU, CLAP, Standalone, LV2 };

} // namespace mw::plugin
```

### 8.3 Constant PDC reporting (ADR-017)

The plugin reports a **CONSTANT total latency** to the host via `setLatencySamples`,
declared from `plugin/` [ADR-017 Decision, L4; ADR-001 the PDC path lives in
`plugin/`]. It is **independent of FX on/off and independent of the Quality tier
(1x/2x/4x)**; shorter configurations are delay-aligned (padded) up to the worst case
so the reported number never changes for the lifetime of the instance [ADR-017 L4-L5,
L7-L8].

File: `plugin/latency/LatencyReporter.h/.cpp`.

```cpp
// plugin/latency/LatencyReporter.h
namespace mw::plugin {

class LatencyReporter {
public:
    // Computed and sized in prepare() ONLY; never recomputed on the audio thread.
    // Returns the single constant worst-case latency in samples.
    int computeWorstCaseLatency (double sampleRate) const noexcept;
    // Preallocated padding delay lines align shorter configs up to the worst case.
    void preparePadding (int worstCaseSamples, int numChannels);
};

} // namespace mw::plugin
```

What contributes to reported PDC [ADR-017 §Decision, Contract L1-L3]:

| Source | Contributes? | Note |
| --- | --- | --- |
| Per-voice oversampled-zone group delay (IR3109 ladder + diode-clamp resonance + BA662 VCA drive; realtime polyphase IIR halfband, ADR-004) | YES | Fixed, measured; in the worst-case pad; part of the blessed FX-off bit-exact contract; identical on macOS arm64 and Linux x64 [ADR-017 L1] |
| FX Drive 2x oversampling group delay (its own post-voice up/down pair, ADR-017) | YES | Always counted toward the reported worst case even when Drive is bypassed (constant-padded); FX Drive runs once on the mono voice-sum, not per-voice [ADR-017 L2, L9] |
| FX Delay musical time + Chorus modulation delay (ADR-010) | NO | User-set / intended musical delay; never reported as PDC; never host-compensated [ADR-017 L3] |

Reported latency is **invariant** to (a) master/per-block FX bypass, (b) the Quality
tier, and (c) build-to-build changes; any change is a deliberate, reviewed event with
a golden re-bless [ADR-017 L5, L11]. The value is sized in `prepare`, padding delay
lines preallocated to worst case; no alloc/lock on the audio thread and the reported
number is never mutated from `process` [ADR-017 L10]. With all FX bypassed the audio
is FX-off bit-exact at the declared worst-case offset [ADR-017 L6].

## 9. State / preset round-trip surface

The MIDI-learn table, the dual tuning convention (440 default param + 442 preset),
the modern-unquantized flag, the MPE member-channel count, the assignable
pressure-destination selector, the velocity on/off + routing depth, and the bend
ranges all round-trip in presets and project state [ADR-012 §Consequences]. The
parameter members live in the APVTS tree (owned by doc 06); non-parameter side state
(the CC-learn bindings) lives in the `<extras>` subtree per ADR-008 C8 and is
serialized by the single (de)serializer [ADR-008 C8-C9, C13]. This document does not
define the serialization format — see docs/design/06 §2 and ADR-008.

## 10. File map (created by the backlog)

| File | Responsibility | Owning sections |
| --- | --- | --- |
| `cmake/Formats.cmake` | Per-platform format resolution + configure-time validator gate | §2.2 |
| `cmake/Validators.cmake` | Locate/declare validator targets | §2.1-2.2 |
| `plugin/host/HostEvent.h` | `HostEvent` POD + `NormalizedEventBuffer` | §3.2 |
| `core/midi/MidiEvent.h` | core-side `mw::core::MidiEvent` POD (JUCE-free); fed to the engine via `BlockContext` | §3.3 |
| `plugin/host/Capabilities.h` | `NoteExpressionRung`, `TransportRung`, `ResolvedCapabilities`, `PluginFormat` | §7.2 |
| `plugin/host/CapabilityShim.h/.cpp` | Resolve + per-block recheck + UI publish of capability rungs | §7.2-7.4, §8.1-8.2 |
| `plugin/midi/MidiFrontEnd.h/.cpp` | Note/gate/bend/pressure/CC -> normalized events + per-voice expression | §4 |
| `plugin/midi/CcLearnMap.h/.cpp` | Double-buffered atomic-swap CC/learn map | §6.3 |
| `plugin/midi/MpeReconstructor.h/.cpp` | MPE-over-MIDI per-channel reconstruction parser | §7.3 |
| `plugin/latency/LatencyReporter.h/.cpp` | Constant worst-case PDC compute + padding delay lines | §8.3 |
| `core/calibration/Calibration.h` | Centralizes all `(PI)` constants named here | §3.2, §6.4 |

## Acceptance hooks

Objectively-testable properties a backlog task's tests must verify:

- **Configure gate:** configuring AU on Linux/Windows is a configure-time error;
  configuring any AAX target on any platform is a configure-time error; force-adding
  a format whose validator is not wired for the platform fails the configure
  [§2.2 F-3, F-4, F-5; ADR-011 C3-C5; ADR-024 C6].
- **Validator matrix green:** macOS arm64 release passes `auval`, `pluginval`,
  Steinberg `validator`, `clap-validator`, headless Standalone smoke; Linux x64
  passes the same minus AU; LV2 ships only when `lv2lint` AND `lv2_validate` are
  green and its absence never blocks the Linux gate [§2.1; F-6, F-7, F-8].
- **Cross-format bit-exactness:** identical patch/state + identical normalized event
  sequence yields bit-identical DSP output across VST3/AU/CLAP/Standalone on macOS
  arm64 (single shared engine) [§3.1; ADR-011 C11; ADR-022 C4].
- **No-alloc / no-lock on the audio thread:** wrapper event drain, MIDI parse,
  capability recheck, CC/learn lookup, de-zipper, and PDC read perform zero heap
  allocation and take no lock (sentinel-allocator / assertion in debug/CI)
  [§1.3, §3.2; ADR-011 C9].
- **Trigger/Priority coupling:** GATE = lowest-note + no legato retrigger; GATE+TRIG
  = last-note + retrigger every key; LFO = lowest-note + clock-triggered envelopes;
  priority and trigger are one 3-value parameter, never two [§4.2; ADR-012 C1-C4].
- **Pitch quantization:** default note pitch passes through the 6-bit 1 V/oct
  quantizer; `mw101.pitch.modern_unquantized` = ON bypasses it; OFF is the shipped
  default; out-of-range notes clamp to the span edge [§4.3; ADR-012 C5-C7, C6].
- **Bend Pre-Q:** channel bend (default ±2, range 0..24) and MPE per-note bend
  (default ±48) are continuous offsets applied before the quantizer [§4.4; ADR-012
  C8, C11].
- **Velocity default ON:** velocity routes to VCA level + VCF cutoff amount by
  default; the no-velocity switch disables it [§4.5; ADR-016 R-2].
- **Tuning:** A4 parameter default = 440 Hz over ~400-460 Hz; the hardware-accurate
  preset recalls 442 Hz and is never the default; TUNE (`mw101.vco.fine`) is a
  separate ±1.0-semitone control layered on top [§5; ADR-012 C21-C23].
- **Default CC map + learn:** CC1/7/11/74/71/5 bind to their listed parameters;
  CC64 drives HOLD semantics; MIDI-learn edits swap the live map via a lock-free
  single-writer atomic pointer with no audio-thread mutex/alloc [§6.2-6.3; ADR-012
  C15-C16, C20].
- **De-zipper:** bend/pressure/CC changes are smoothed O(1)/sample with no branch on
  message arrival [§6.4; ADR-012 C24].
- **MPE-lite scope:** member channels default OFF (0), opt-in 1..15, lower zone only;
  per-note pitch -> per-voice Pre-Q offset; per-note pressure -> one assignable
  destination (default VCF cutoff CV); no other per-note timbre routing; mono mode
  collapses to channel bend + channel pressure [§7.1; ADR-012 C10-C13].
- **Note-expression ladder:** Native (CLAP), MPE-over-MIDI (VST3/AU/LV2/Standalone
  when ON), and Collapsed (OFF / no per-note data) all feed the SAME per-voice
  offset; Collapsed is bit-identical to running without MPE [§7.2-7.3; ADR-022
  C1-C4].
- **Transport ladder:** CLAP places sub-block-accurate edges; VST3/AU/LV2
  block-quantize edges to the containing block boundary with phase/count/order
  identical to sample-accurate (≤1 block jitter); Standalone / playhead-less hosts
  free-run the INTERNAL clock at RATE; HOST-SYNC-without-transport behaves as
  INTERNAL then re-locks from absolute PPQ [§8.1; ADR-022 C5-C10].
- **Active-rung visibility:** both resolved rungs are published to the UI via the
  lock-free atomic-pointer path; Collapsed / Free-run states are user-visible
  [§7.4, §8.2; ADR-022 C12].
- **Constant PDC:** reported latency is a single constant = worst-case total group
  delay across all FX on/off and all Quality tiers; it is invariant to FX bypass,
  Quality tier, and build-to-build; the per-voice IIR zone group delay contributes
  and is inside the FX-off golden; FX Delay/Chorus musical time never contributes
  [§8.3; ADR-017 L1-L11].

## References

ADRs (normative contracts):

- ADR-001 — DSP core / plugin-shell boundary & real-time contract
  (`plan/decisions/001-core-plugin-boundary.md`).
- ADR-008 — Parameter / state / preset schema (parameter-ID ownership;
  `plan/decisions/008-parameter-state-preset-schema.md`).
- ADR-011 — Plugin formats & wrapper strategy
  (`plan/decisions/011-plugin-formats-wrappers.md`).
- ADR-012 — MIDI / MPE-lite mapping & tuning reference
  (`plan/decisions/012-midi-mpe-tuning-map.md`).
- ADR-016 — Owner ratifications: out-of-box defaults (velocity ON, MODERN-SMOOTH
  default, mono, subtle drift; `plan/decisions/016-owner-ratifications-2026-06-18.md`).
- ADR-017 — Plugin latency (PDC) policy & Drive placement
  (`plan/decisions/017-plugin-latency-pdc-and-drive-placement.md`).
- ADR-022 — MPE-lite & arp/seq cross-format behavior
  (`plan/decisions/022-mpe-arp-seq-cross-format-contract.md`).
- ADR-024 — LV2 export path & AAX exclusion
  (`plan/decisions/024-lv2-export-path-and-aax-exclusion.md`).

Research docs (cited factual ground truth):

- research/08 — Power, CV/Gate, Calibration & I/O Inventory
  (`docs/research/08-power-cv-io.md`): stock jack set (§2.1), no MIDI/DCB/DIN-sync
  (§2.3), CV/Gate electrical (§3), EXT CLK rising-edge no-song-position (§3.3),
  pitch CV / Range / Transpose / A4=442 (§4, §11, §12), VR-7 442 Hz (§5.2), VCF
  cutoff CV path / SDT-1000 (§7.2, §5.3), TUNE ±50 cents (§7.4), design implications
  (§10).
- research/12 — Competitive Landscape & Trademark/Trade-dress
  (`docs/research/12-market-legal-landscape.md`): Linux + CLAP/LV2/VST3 breadth as
  the differentiator the commercial field ignores (§7.3); AAX/commercial format set
  context (§3); naming/trademark distance (§5, §7.1).

Cross-references (owned elsewhere, referenced not redefined):

- docs/design/06 §2 — parameter IDs, ranges, units, skews, defaults, automatable
  classification (the schema; per ADR-008).
- ADR-006 / ADR-007 — key-assigner / voice model and arp + 100-step sequencer DSP +
  clock edge detector (fed by this document's MIDI and transport normalization).
- ADR-004 / ADR-010 — oversampling, FX, and Drive placement (the group-delay sources
  this document's PDC policy reports).
