// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/ControlTypes.h — the shared POD header for the control core (the
// modulation / arpeggiator / 100-step sequencer stream) [task 081].
//
// Realizes docs/design/05 §3.2 (PwmSource/VcaSource, ModInputs/ModDepths/ModOutputs),
// §4.2 (TrigMode/NotePriority, KeyState/TriggerDecision), §5.4 (ArpMode), §6.2
// (SeqStep storage layout + SeqBuffer/kMaxSteps), §6.5 (SeqPlayResult), §7.7
// (ClockSource/HostRate, ClockEdge) and §9.2 (ControlSnapshot).
//
// This header holds ONLY trivially-copyable POD types and the stream's enums — no
// logic, no class implementations (those land in later tasks: ModRouter,
// TriggerSource, Arpeggiator, StepSequencer, Clock, SequencerEngine). No `juce::*`
// type appears here; mwcore is JUCE-free [ADR-001/D2]. Parameter IDs/ranges/skews
// are NOT minted here — they are owned by docs/design/06 §2 [ADR-008].

#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace mw::control {

// ---------------------------------------------------------------------------
// §3.2 — Fixed modulation routing enums + scalar PODs.
// ---------------------------------------------------------------------------

// PWM source switch: PWM consumes the ADSR (Env), a static MANUAL width, or the
// LFO (Lfo) [docs/design/05 §3.2; ADR-007 C2].
enum class PwmSource : std::uint8_t { Env = 0, Manual = 1, Lfo = 2 };

// VCA control: driven by the ADSR (Env) or by the raw GATE level (Gate)
// [docs/design/05 §3.2; research/04 §2.4].
enum class VcaSource : std::uint8_t { Env = 0, Gate = 1 };

// Per-block scalar inputs supplied by the LFO and ADSR subsystems [§3.2].
struct ModInputs {
    float lfoValue = 0.0f;   // selected LFO instantaneous value, normalized [-1, 1]
    float envValue = 0.0f;   // shared ADSR output, normalized [0, 1]
    float pwmManual = 0.0f;  // MANUAL PWM source value, normalized [0, 1]
};

// Depth gains; all normalized 0..1 (or -1..1 where bipolar). Values come from the
// param snapshot; IDs/ranges/skews are owned by docs/design/06 §2 [§3.2].
struct ModDepths {
    float lfoToPitch = 0.0f;   // VCO pitch MOD depth
    float lfoToPwm = 0.0f;     // PWM depth when PwmSource::Lfo
    float lfoToCutoff = 0.0f;  // VCF cutoff MOD depth (LFO)
    float envToCutoff = 0.0f;  // VCF cutoff ENV depth
    float envToPwm = 0.0f;     // PWM depth when PwmSource::Env
    float lfoToVca = 0.0f;     // LFO->VCA tremolo depth (SH-101-specific)
};

// Resolved per-destination modulation, consumed by VCO/VCF/VCA/PWM DSP [§3.2].
struct ModOutputs {
    float pitchMod = 0.0f;    // added to pitch CV (semitone units, pitch doc scales)
    float pwmMod = 0.0f;      // pulse-width modulation amount, normalized
    float cutoffMod = 0.0f;   // VCF cutoff modulation, normalized
    float vcaTremolo = 0.0f;  // VCA tremolo contribution, normalized
};

// ---------------------------------------------------------------------------
// §4.2 — Trigger-source switch (S7): note priority + retrigger coupling.
// ---------------------------------------------------------------------------

// S7 selector: one control binding note priority and envelope retrigger [§4.2].
enum class TrigMode : std::uint8_t { GateTrig = 0, Gate = 1, Lfo = 2 };

// Note priority derived from the S7 mode [§4.2/§4.3].
enum class NotePriority : std::uint8_t { LastNote = 0, LowestNote = 1 };

// Keyboard scan state. 32-key held bitmap; bit i set => key i held [§4.2].
struct KeyState {
    std::uint32_t held = 0;          // research/07 §3.1; research/06 §2.4
    std::uint32_t justPressed = 0;   // keys newly down since last scan
    std::uint32_t justReleased = 0;  // keys newly released since last scan
};

// Resolved monophonic trigger decision for a tick [§4.2].
struct TriggerDecision {
    int selectedKey = -1;   // -1 == no note (all keys up, gate off)
    bool retrigger = false; // fire envelope this tick
    bool gateOn = false;
    bool legato = false;    // new key while another already held (no retrig in Gate)
};

// ---------------------------------------------------------------------------
// §5.4 — Arpeggiator play direction.
// ---------------------------------------------------------------------------

// Exactly three mutually-exclusive play directions [§5.4; ADR-007 C7].
enum class ArpMode : std::uint8_t { Up = 0, UandD = 1, Down = 2 };

// ---------------------------------------------------------------------------
// §6.2 — 100-step sequencer storage layout.
// ---------------------------------------------------------------------------

// One sequencer slot. POD, trivially copyable, fixed 1 byte [§6.2].
// Byte layout (implementation choice, NOT asserted bit-exact per research/06 §8.2):
//   [b5:b0] = pitch (0..63), b6 = REST, b7 = TIE/legato.
struct SeqStep {
    std::uint8_t bits = 0;
    static constexpr std::uint8_t kPitchMask = 0x3F;  // research/07 §5.5 (anl #3fh)
    static constexpr std::uint8_t kRestFlag = 0x40;   // implementation choice; research/06 §8.2
    static constexpr std::uint8_t kTieFlag = 0x80;    // implementation choice; research/06 §8.2

    bool isRest() const noexcept { return (bits & kRestFlag) != 0; }
    bool isTie() const noexcept { return (bits & kTieFlag) != 0; }
    int pitch() const noexcept { return bits & kPitchMask; }
};

inline constexpr int kMaxSteps = 100;             // research/06 §3.1, §7
using SeqBuffer = std::array<SeqStep, kMaxSteps>; // preallocated; never resized

// ---------------------------------------------------------------------------
// §6.5 — Sequencer play result (one slot decoded).
// ---------------------------------------------------------------------------

struct SeqPlayResult {
    int pitch6 = 0;          // 6-bit pitch of the current slot
    bool gateOn = true;      // false on REST
    bool tie = false;        // sustain envelope + portamento
    bool retrigger = true;   // false on TIE (and per S7 mode)
    int slotIndex = 0;
};

// ---------------------------------------------------------------------------
// §2.3 / §9.2 — Block-boundary events into/out of SequencerEngine::processBlock.
// ---------------------------------------------------------------------------

// Inbound key/control event (MIDI/MPE-decoded), time-stamped at a sub-block
// sample offset [docs/design/05 §2.3, §9.2]. POD, no JUCE type.
struct KeyEvent {
    int pitch = 0;          // note/key (semitone)
    bool gate = false;      // gate on/off for this event
    bool trig = false;      // request envelope retrigger
    bool porta = false;     // portamento/slide engaged (tie/legato)
    float mod = 0.0f;       // associated modulation scalar (normalized)
    int sampleOffset = 0;   // within the current block
};

// Outbound, time-stamped control event the voice layer reads [§2.3, §9.2].
// Carries pitch / gate / trig / porta / mod, placed at a sub-block sample offset.
struct ControlEvent {
    int pitch = 0;          // pitch CV (semitone units; pitch doc scales)
    bool gate = false;      // gate state
    bool trig = false;      // envelope retrigger this event
    bool porta = false;     // portamento/slide engaged
    float mod = 0.0f;       // modulation scalar carried alongside (normalized)
    int sampleOffset = 0;   // within the current block
};

// ---------------------------------------------------------------------------
// §7.7 — Clock source / host-rate selector + the edge POD.
// ---------------------------------------------------------------------------

// Three mutually-exclusive clock sources [§7.7; ADR-007 §Decision 5].
enum class ClockSource : std::uint8_t { Internal = 0, HostSync = 1, Ext = 2 };

// Discrete musical rates for host-sync (the host-rate selector, C23) [§7.7].
enum class HostRate : std::uint8_t {
    Quarter = 0, Eighth, EighthT, Sixteenth, SixteenthT, ThirtySecond,
    DottedEighth, DottedSixteenth
};

// An H->L edge to be placed at a sub-block sample offset [§7.7].
struct ClockEdge {
    int sampleOffset = 0;
};

// ---------------------------------------------------------------------------
// §9.2 — Immutable, trivially-copyable control-core persistence snapshot.
// Read on the audio thread via an atomically-swapped pointer; schemaVersion from 1.
// ---------------------------------------------------------------------------

struct ControlSnapshot {
    SeqBuffer seq{};
    int seqCount = 0;
    ArpMode arpMode = ArpMode::Up;
    bool arpHold = false;
    bool uAndDRepeatEndpoints = false;
    ClockSource clockSource = ClockSource::Internal;
    float internalRateHz = 1.0f;
    HostRate hostRate = HostRate::Sixteenth;
    float swing = 0.5f;
    bool clockResetOnKeypress = true;
    TrigMode trigMode = TrigMode::GateTrig;
    PwmSource pwmSource = PwmSource::Lfo;
    VcaSource vcaSource = VcaSource::Env;
    std::uint32_t schemaVersion = 1;  // versioned from v1 (C25)
};

// ---------------------------------------------------------------------------
// Compile-time POD guarantees: every type in this header is trivially copyable
// (audio-thread handoff / atomic snapshot swap is memcpy-safe) [§9.2; ADR-007 C26].
// ---------------------------------------------------------------------------

static_assert(sizeof(SeqStep) == 1, "SeqStep MUST be exactly 1 byte [docs/design/05 §6.2].");
static_assert(kMaxSteps == 100, "kMaxSteps MUST be 100 [docs/design/05 §6.2; research/06 §3.1].");

static_assert(std::is_trivially_copyable_v<ModInputs>, "ModInputs MUST be a POD [§3.2].");
static_assert(std::is_trivially_copyable_v<ModDepths>, "ModDepths MUST be a POD [§3.2].");
static_assert(std::is_trivially_copyable_v<ModOutputs>, "ModOutputs MUST be a POD [§3.2].");
static_assert(std::is_trivially_copyable_v<KeyState>, "KeyState MUST be a POD [§4.2].");
static_assert(std::is_trivially_copyable_v<TriggerDecision>, "TriggerDecision MUST be a POD [§4.2].");
static_assert(std::is_trivially_copyable_v<SeqStep>, "SeqStep MUST be a POD [§6.2].");
static_assert(std::is_trivially_copyable_v<SeqBuffer>, "SeqBuffer MUST be a POD [§6.2].");
static_assert(std::is_trivially_copyable_v<SeqPlayResult>, "SeqPlayResult MUST be a POD [§6.5].");
static_assert(std::is_trivially_copyable_v<KeyEvent>, "KeyEvent MUST be a POD [§2.3/§9.2].");
static_assert(std::is_trivially_copyable_v<ControlEvent>, "ControlEvent MUST be a POD [§2.3/§9.2].");
static_assert(std::is_trivially_copyable_v<ClockEdge>, "ClockEdge MUST be a POD [§7.7].");
static_assert(std::is_trivially_copyable_v<ControlSnapshot>, "ControlSnapshot MUST be a POD [§9.2].");

} // namespace mw::control
