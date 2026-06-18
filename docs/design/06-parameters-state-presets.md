<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Parameter / State / Preset Schema (the contract)

This document is the single source of truth for the mwAudio101 parameter
contract: the full APVTS parameter tree (stable IDs, ranges, units, skews,
defaults, automatable flags, smoothing class), the structural Quality parameter,
the versioned state save/restore format with its migration chain and
load-failure fallback, `renderVersion`, the on-disk preset file format, and the
~64-preset category taxonomy.

It OWNS parameter IDs. Every other design doc REFERENCES the IDs defined here in
§3 (the master index is §3.0) and the calibration constants centralized in
§3.10; no other doc mints a parameter ID. DSP behavior keyed off these
parameters is owned by the relevant subsystem doc (oscillator, filter,
envelope/LFO, mixer/glide, arp/seq, voice, FX, MIDI/MPE, drift/vintage, UI,
testing) — this doc defines only the contract surface, not the DSP that consumes
it. The "owning-doc-for-behavior" column of §3.0 names that subsystem doc per ID.

This contract is the realization of ADR-008 (schema), ADR-018 (Quality param),
ADR-020 (smoothing policy), ADR-021 (load-failure handling), ADR-023
(renderVersion + blessed sample rates) and ADR-016 (out-of-box defaults). It also
registers the FX surface of ADR-010 (Drive/Chorus/Delay), the drift/vintage
surface of ADR-009, and the MIDI/tuning/MPE surface of ADR-012/ADR-016/ADR-022.
Where this doc and an ADR appear to disagree, the ADR wins and this doc is the
bug.

## 1. Scope and ownership

### 1.1 What this doc owns

- The complete, append-only set of `mw101.`-namespaced parameter IDs [ADR-008
  C1].
- For each parameter: type, range/skew/step, unit (advisory display string),
  default, `isAutomatable`, `isDiscrete`, browser group, smoothing class, and
  `versionAdded` [ADR-008 §1].
- The single declarative parameter registry (`params/ParamDefs.h`) that
  generates the APVTS `ParameterLayout`, the preset (de)serializer, the CI
  validators and the docs parameter reference [ADR-008 §1].
- The serialized state tree shape: root attributes (`schemaVersion`,
  `pluginVersion`, `engineVersion`, `renderVersion`), the APVTS subtree, and the
  `<extras>` subtree [ADR-008 §5; ADR-023 V1-V3].
- The migration chain interface, the migration-version table, and the
  load-failure fallback ladder [ADR-008 §5, C10-C12; ADR-021 L1-L14].
- The on-disk `.mw101preset` JSON format and the ~64-preset category taxonomy
  [ADR-008 §6, C13-C18; research/11 §7.1].
- The smoothing-class enum and the per-class de-zipper time-constant table
  (numbers live in the calibration table) [ADR-020 S1-S14].
- The renderVersion lifecycle as it touches saved state and load [ADR-023
  V3-V10].

### 1.2 What this doc references, never redefines

- The DSP that reads a parameter (oscillator/sub/noise, IR3109 VCF, ADSR/LFO/VCA,
  mixer/glide, arp/seq, voice/poly/unison, FX, drift/vintage, MIDI/MPE) lives in
  the relevant subsystem design doc; this doc gives the ID and contract surface
  only. The §3.0 index names the owning doc per ID.
- The control-rate tick cadence the smoothers advance on is owned by the
  control-rate doc [ADR-005/ADR-016]; this doc states only that smoothers MUST
  advance on it [ADR-020 S11].
- The bless tool, MANIFEST, golden determinism classes and the renderVersion
  bump/CI governance are owned by the testing doc [ADR-013; ADR-023 V5-V7,
  V11-V16]; this doc owns only how `renderVersion` rides in saved state and the
  load-time opt-in flag [ADR-023 V3-V4, V8-V10].
- Voice/unison/oversampling DSP reconfiguration mechanics are owned by their docs
  [ADR-004/006/019]; this doc owns only that those params are structural,
  non-automatable, and applied off the audio thread.
- FX DSP topology/algorithms (doc 07 / ADR-010/017), drift DSP (doc 08 /
  ADR-009), and the MIDI/MPE/tuning front-end (doc 09 / ADR-012/022) are owned by
  those docs; this doc owns the ID/range/default/skew/smoothing surface they bind
  to.

### 1.3 Owner-locked invariants this contract honors

- Normalized modeled ranges, no physical-unit oracle: the host-visible
  automation value is always 0..1; `unit` strings are advisory display only and
  MUST NOT affect the stored/automation value [ADR-008 C4].
- Real-time safe: all parse/migrate/IO/preset application on the message thread;
  the audio thread reads only lock-free APVTS atomics and the pre-sized SPSC
  `<extras>` double-buffer; no heap allocation and no locks on the audio thread
  [ADR-008 C19-C20; ADR-021 L13].
- Stable IDs decouple the contract from the reimagined UI; display names/order
  may change freely [ADR-008 §2; ADR-016 §5 UI position].

## 2. File and module map

The backlog creates the following. Paths are normative; signatures below are the
contract.

| File | Responsibility |
| --- | --- |
| `core/params/ParamDefs.h` | The single declarative `ParamDefs` registry (constexpr table; §3). Generates the APVTS layout, preset (de)serializer, CI validators, docs. No parameter declared anywhere else [ADR-008 §1]. |
| `core/params/ParamIDs.h` | Compile-time string-ID constants (one `constexpr const char*` per param) so call sites never hand-type an ID. |
| `core/params/ParameterLayout.cpp` | `buildParameterLayout()` — mechanically builds `juce::AudioProcessorValueTreeState::ParameterLayout` from `ParamDefs` (§4). |
| `core/params/SmoothingClass.h` | The `SmoothingClass` enum and the per-class time-constant accessor reading the calibration table (§3.9, ADR-020). |
| `core/calibration/Calibration.h` | The single `(PI)` calibration table all invented constants live in (smoothing time constants, default-patch values, drift bands, FX constants). Referenced by `ParamDefs`, never inlined [ADR-008 §1; ADR-020 S13]. |
| `core/state/StateTree.h` | Root-tree identifiers, attribute keys, `<extras>` child schema (§5). |
| `core/state/StateSerializer.h/.cpp` | The ONE canonical (de)serializer: APVTS + `<extras>` -> `juce::ValueTree` -> host blob and back (§5, §6). |
| `core/state/Migration.h/.cpp` | The ordered migration chain `migrateV(n)->V(n+1)`, the version table, and `migrateToCurrent()` (§7). |
| `core/state/LoadFailure.h/.cpp` | Graded recovery / fallback ladder, the recovery-report struct, raw-blob retention (§8, ADR-021). |
| `core/state/Extras.h` | POD `<extras>` payload (fixed-capacity 100-step sequence, arp latch, drift seed + lock, CC-learn bindings, UI size) handed to the audio thread via SPSC double-buffer (§5.4). |
| `core/version/EngineVersion.h` | `CURRENT_SCHEMA_VERSION`, `CURRENT_RENDER_VERSION`, `kEngineVersion`, `kPluginVersion` constants (§9). |
| `core/preset/PresetFormat.h/.cpp` | `.mw101preset` JSON <-> canonical ValueTree projection; the JSON validator (§6, §10). |
| `core/preset/PresetManager.h/.cpp` | In-memory bank loaded from embedded BinaryData at construction; category index; per-slot INIT fallback (§10, ADR-021 L9). |
| `presets/<Category>/*.mw101preset` | The ~64 factory presets, organized by category folder; mirrored 1:1 into BinaryData by CI (§10, ADR-008 C18). |

## 3. The parameter registry

### 3.0 Canonical parameter index

This is the master index: a flat table of EVERY parameter ID the contract
defines. It is the sibling-doc citation surface — docs 07 (FX), 08
(vintage/drift) and 09 (formats/IO/MIDI), and the voice/arp/seq/filter/osc docs,
align to the IDs, types, ranges, defaults, automatable flags and smoothing
classes here, never re-minting any of them. Non-parameter state (sequencer
pattern, arp latch, drift seed + lock, CC-learn bindings) lives in `<extras>`
(§5.4) and is listed at the bottom for completeness but is NOT an
`AudioProcessorParameter` [ADR-008 C8]. Every continuous range is in normalized
modeled units; the host automation value is always 0..1 [ADR-008 C4]. Smoothing
class is one of {NoSmooth, Pitch(S1), Fast(S2), PulseWidth(S3), Level(S4),
Glide(S5)} per §3.9 / ADR-020. `(PI)` marks values invented in this doc (no
measured oracle); they live in the calibration table (§3.10).

| ID | Type | Range / units | Default | Automatable | Smoothing class | Owning doc (behavior) |
| --- | --- | --- | --- | --- | --- | --- |
| `mw101.vco.tune` | Continuous | -24 .. +24 semitones, linear | 0 | yes | Pitch (S1) | oscillator |
| `mw101.vco.fine` | Continuous | -1.0 .. +1.0 semitones, linear symmetric | 0 | yes | Pitch (S1) | oscillator (sole fine-tune; doc 09 TUNE re-points here) |
| `mw101.vco.pw` | Continuous | 0.0 .. 1.0 duty, linear | 0.5 | yes | PulseWidth (S3) | oscillator |
| `mw101.vco.pwm_depth` | Continuous | 0.0 .. 1.0 depth, linear | 0.0 | yes | PulseWidth (S3) | oscillator (manual width; distinct from `lfo.depth_pwm`) |
| `mw101.vco.range` | Choice | 0:16' 1:8' 2:4' 3:2' \| 4:32'(sw) 5:64'(sw) | 1 (8') | yes | NoSmooth | oscillator |
| `mw101.saw.level` | Continuous | 0.0 .. 1.0 level, linear | 0.8 | yes | Level (S4) | mixer |
| `mw101.pulse.level` | Continuous | 0.0 .. 1.0 level, linear | 0.0 | yes | Level (S4) | mixer |
| `mw101.sub.level` | Continuous | 0.0 .. 1.0 level, linear | 0.0 | yes | Level (S4) | mixer |
| `mw101.sub.mode` | Choice | 0:-1 Oct Sq 1:-2 Oct Sq 2:-2 Oct Pulse | 0 | yes | NoSmooth | oscillator/sub (canonical ID; NOT `sub.shape`) |
| `mw101.noise.level` | Continuous | 0.0 .. 1.0 level, linear | 0.0 | yes | Level (S4) | mixer |
| `mw101.vcf.cutoff` | Continuous | 0.0 .. 1.0 norm freq, log-ish (PI) | 1.0 | yes | Fast (S2) | filter |
| `mw101.vcf.resonance` | Continuous | 0.0 .. 1.0, linear | 0.0 | yes | Fast (S2) | filter |
| `mw101.vcf.env_mod` | Continuous | 0.0 .. 1.0 depth, linear | 0.0 | yes | Fast (S2) | filter |
| `mw101.vcf.lfo_mod` | Continuous | 0.0 .. 1.0 depth, linear | 0.0 | yes | Fast (S2) | filter/LFO |
| `mw101.vcf.kbd_track` | Continuous | 0.0 .. 1.0 amount, linear | 0.0 | yes | Fast (S2) | filter |
| `mw101.env.attack` | Continuous | 0.0 .. 1.0 time, log (PI) | 0.0 | yes | NoSmooth | envelope |
| `mw101.env.decay` | Continuous | 0.0 .. 1.0 time, log (PI) | 0.3 | yes | NoSmooth | envelope |
| `mw101.env.sustain` | Continuous | 0.0 .. 1.0 level, linear | 1.0 | yes | Level (S4) | envelope |
| `mw101.env.release` | Continuous | 0.0 .. 1.0 time, log (PI) | 0.1 | yes | NoSmooth | envelope |
| `mw101.lfo.rate` | Continuous | 0.1 .. 30.0 Hz, log | 5.0 | yes | Fast (S2) | LFO |
| `mw101.lfo.shape` | Choice | 0:Tri 1:Sq 2:Random 3:Noise \| 4:Sine(sw) | 0 (Tri) | yes | NoSmooth | LFO |
| `mw101.lfo.dest` | Choice | 0:Pitch 1:Filter 2:PWM | 0 | yes | NoSmooth | LFO |
| `mw101.lfo.delay` | Continuous | 0.0 .. 1.0 time, log (PI) | 0.0 | yes | NoSmooth | LFO |
| `mw101.lfo.depth_pitch` | Continuous | 0.0 .. 1.0 depth, linear | 0.0 | yes | Fast (S2) | LFO (LFO->pitch amount) |
| `mw101.lfo.depth_pwm` | Continuous | 0.0 .. 1.0 depth, linear | 0.0 | yes | Fast (S2) | LFO (LFO->PWM amount; distinct from `vco.pwm_depth`) (PI) |
| `mw101.lfo.depth_cutoff` | Continuous | 0.0 .. 1.0 depth, linear | 0.0 | yes | Fast (S2) | LFO (LFO->cutoff amount) (PI) |
| `mw101.lfo.tempo_sync` | Bool | false / true | false | yes | NoSmooth | LFO |
| `mw101.lfo.sync_div` | Choice | 0:1/4 1:1/8 2:1/8T 3:1/16 4:1/16T 5:1/32 | 1 | yes | NoSmooth | LFO |
| `mw101.vca.level` | Continuous | 0.0 .. 1.0 level, linear | 0.8 | yes | Level (S4) | VCA (also CC7 volume target, doc 09) |
| `mw101.vca.mode` | Choice | 0:ENV 1:GATE | 0 (ENV) | yes | NoSmooth | VCA/envelope (PI) |
| `mw101.glide.time` | Continuous | 0.0 .. 5.0 s, log | 0.0 | yes | Glide (S5) | glide |
| `mw101.glide.mode` | Choice | 0:Off 1:Auto 2:On | 0 (Off) | yes | NoSmooth | glide |
| `mw101.mod.bend_range_vco` | Continuous | 0 .. 1200 cents, linear | 200 | yes | Pitch (S1) | mod |
| `mw101.mod.bend_range_vcf` | Continuous | 0 .. 1200 cents, linear | 0 | yes | Fast (S2) | mod |
| `mw101.mod.bend_dest` | Choice | 0:VCO 1:VCF 2:Both | 0 | yes | NoSmooth | mod |
| `mw101.mod.lfo_mod_wheel` | Continuous | 0.0 .. 1.0 depth, linear | 0.0 | yes | Fast (S2) | mod (CC1 mod-wheel target, doc 09) |
| `mw101.arp.mode` | Choice | 0:Off 1:Up 2:Down 3:Up-Down | 0 (Off) | yes | NoSmooth | arp |
| `mw101.arp.range` | Choice | 0:1 Oct 1:2 Oct 2:3 Oct | 0 | yes | NoSmooth | arp |
| `mw101.arp.tempo_sync` | Bool | false / true | true | yes | NoSmooth | arp |
| `mw101.arp.sync_div` | Choice | 0:1/4 1:1/8 2:1/8T 3:1/16 4:1/16T 5:1/32 | 1 | yes | NoSmooth | arp |
| `mw101.arp.latch` | Bool | false / true | false | yes | NoSmooth | arp (live control; persisted latch is `<extras>`) |
| `mw101.seq.mode` | Choice | 0:Off 1:Play 2:Record | 0 (Off) | yes | NoSmooth | seq |
| `mw101.seq.tempo_sync` | Bool | false / true | true | yes | NoSmooth | seq |
| `mw101.seq.sync_div` | Choice | 0:1/4 1:1/8 2:1/8T 3:1/16 4:1/16T 5:1/32 | 3 | yes | NoSmooth | seq |
| `mw101.key.trigger_priority` | Choice | 0:GATE 1:GATE+TRIG 2:LFO | 0 (GATE) | yes | NoSmooth | MIDI/voice (coupled S7 priority+retrigger) |
| `mw101.tune.a4` | Continuous | 400 .. 460 Hz, linear | 440 | yes | Pitch (S1) | MIDI/tuning (442 is a preset, not the default) |
| `mw101.tune.slop` | Continuous | 0 .. 20 cents, linear | 2.5 | yes | Pitch (S1) | vintage/drift (per-note-on slop, ADR-009 VV-4) |
| `mw101.pitch.modern_unquantized` | Bool | false / true | false | yes | NoSmooth | MIDI (off-by-default 6-bit-quantizer bypass) |
| `mw101.vel.enable` | Bool | false / true | true | yes | NoSmooth | MIDI (velocity sensing; ADR-016 R-2) |
| `mw101.vel.depth` | Continuous | 0.0 .. 1.0 amount, linear | 0.5 (PI) | yes | Fast (S2) | MIDI (velocity -> VCA+VCF amount) (PI) |
| `mw101.amp.expression` | Continuous | 0.0 .. 1.0 scaler, linear | 1.0 | yes | Level (S4) | MIDI (CC11 expression VCA scaler) |
| `mw101.mpe.enable` | Bool | false / true | false | yes | NoSmooth | MIDI/MPE (lower-zone members; OFF by default) |
| `mw101.mpe.bend_range` | Continuous | 0 .. 96 semitones, linear | 48 | yes | Pitch (S1) | MIDI/MPE (per-note + master bend range) |
| `mw101.mpe.pressure_dest` | Choice | 0:VCF Cutoff 1:VCA Level 2:PW | 0 (VCF Cutoff) | yes | NoSmooth | MIDI/MPE (one assignable pressure destination) (PI choice set) |
| `mw101.vintage.age` | Continuous | 0.0 .. 1.0 amount (0-100%), linear | 0.0 | yes | Level (S4) | vintage/drift (Age macro; param default 0) |
| `mw101.vintage.enable` | Bool | false / true | false | yes | NoSmooth | vintage/drift (drift enable) |
| `mw101.vintage.cal_spread` | Continuous | 0.0 .. 1.0 (0-100%), linear | 0.25 | yes | Level (S4) | vintage/drift (Tier-1 band width multiplier, VV-6) |
| `mw101.vintage.detune_amt` | Continuous | 0.0 .. 1.0 (0-100%), linear | 0.0 | yes | Level (S4) | vintage/drift (unison/poly per-voice spread scaler, VV-11) |
| `mw101.drift.depth` | Continuous | 0 .. 50 cents, linear | 4 | yes | Level (S4) | vintage/drift (OU/pink amplitude, VV-2) |
| `mw101.drift.rate` | Continuous | 0.01 .. 1 Hz, log | 0.1 | yes | Level (S4) | vintage/drift (OU bandwidth, VV-3) |
| `mw101.warmup.time` | Continuous | 0 .. 30 min (0 == off), linear | 0 (off) | yes | Level (S4) | vintage/drift (warm-up transient; off by default, VV-5) |
| `mw101.var.cutoff` | Continuous | 0.0 .. 1.0 (0-100%), linear | 0.0 | yes | Level (S4) | vintage/drift (frozen-at-note-on cutoff offset, VV-7) |
| `mw101.var.env_time` | Continuous | 0.0 .. 1.0 (0-100% -> ±5..20%), linear | 0.0 | yes | Level (S4) | vintage/drift (frozen A/D/R multiplier, VV-8) |
| `mw101.var.pw` | Continuous | 0.0 .. 1.0 (0-100%), linear | 0.0 | yes | Level (S4) | vintage/drift (frozen PW offset, VV-9) |
| `mw101.var.glide` | Continuous | 0.0 .. 1.0 (0-100%), linear | 0.0 | yes | Level (S4) | vintage/drift (frozen glide multiplier, VV-10) |
| `mw101.fx.bypass` | Bool | false / true (true = bypassed) | true (bypassed) | yes | NoSmooth | FX (master bypass; FX-default-OFF, ADR-010 FX-13) |
| `mw101.fx.drive_enable` | Bool | false / true | false | yes | NoSmooth | FX (Drive per-block early-out) |
| `mw101.fx.drive_amount` | Continuous | 0.0 .. 1.0 amount, linear | 0.0 | yes | Fast (S2) | FX (Drive input gain) |
| `mw101.fx.drive_tone` | Continuous | 0.0 .. 1.0 (0.5 = flat), linear | 0.5 | yes | Fast (S2) | FX (Drive pre/de-emphasis tilt) |
| `mw101.fx.drive_output` | Continuous | 0.0 .. 1.0 makeup, linear | 0.5 | yes | Level (S4) | FX (Drive output makeup) |
| `mw101.fx.chorus_enable` | Bool | false / true | false | yes | NoSmooth | FX (Chorus enable; mode Off is the early-out) |
| `mw101.fx.chorus_mode` | Choice | 0:Off 1:I 2:II 3:I+II | 0 (Off) | yes | NoSmooth | FX (Chorus mode) |
| `mw101.fx.chorus_rate` | Continuous | 0.0 .. 1.0 rate, linear | 0.3 | yes | Fast (S2) | FX (Chorus LFO rate override) |
| `mw101.fx.chorus_depth` | Continuous | 0.0 .. 1.0 depth, linear | 0.5 | yes | Fast (S2) | FX (Chorus modulation excursion) |
| `mw101.fx.chorus_width` | Continuous | 0.0 .. 1.0 (0 = centered mono), linear | 1.0 | yes | Level (S4) | FX (Chorus stereo separation) |
| `mw101.fx.chorus_mix` | Continuous | 0.0 .. 1.0 wet, linear | 0.0 | yes | Level (S4) | FX (Chorus dry/wet) |
| `mw101.fx.delay_enable` | Bool | false / true | false | yes | NoSmooth | FX (Delay per-block early-out) |
| `mw101.fx.delay_sync` | Bool | false / true | false | yes | NoSmooth | FX (Delay tempo sync on/off) |
| `mw101.fx.delay_division` | Choice | 0:1/4 1:1/8 2:1/8. 3:1/8T 4:1/16 5:1/16T | 1 (1/8) | yes | NoSmooth | FX (Delay note division when synced) |
| `mw101.fx.delay_time` | Continuous | 0.0 .. 1.0 time (free, ms-mapped), log (PI) | 0.3 | yes | Fast (S2) | FX (Delay free time) |
| `mw101.fx.delay_feedback` | Continuous | 0.0 .. 0.95 amount, linear | 0.3 | yes | Fast (S2) | FX (Delay recirculation; 0.95 ceiling (PI)) |
| `mw101.fx.delay_damp` | Continuous | 0.0 .. 1.0 (0 = dark), linear | 0.5 | yes | Fast (S2) | FX (Delay feedback LPF cutoff) |
| `mw101.fx.delay_width` | Continuous | 0.0 .. 1.0 (0 = centered mono), linear | 1.0 | yes | Level (S4) | FX (Delay stereo spread) |
| `mw101.fx.delay_mix` | Continuous | 0.0 .. 1.0 wet, linear | 0.0 | yes | Level (S4) | FX (Delay dry/wet) |
| `mw101.fx.delay_pingpong` | Bool | false / true | false | yes | NoSmooth | FX (Delay ping-pong routing) |
| `mw101.out.mono` | Bool | false / true | false | yes | NoSmooth | FX (global Mono Output collapse, ADR-010 FX-9) |
| `mw101.quality` | Choice | 0:Eco 1:Standard 2:HQ | 1 (Standard) | **no** (structural) | NoSmooth | quality (sole live structural quality param, ADR-018) |
| `mw101.voice.mode` | Choice | 0:Mono 1:Poly 2:Unison | 0 (Mono) | **no** (structural) | NoSmooth | voice |
| `mw101.voice.count` | Choice | 0:2 1:4 2:6 3:8 | 1 (4) | **no** (structural) | NoSmooth | voice |
| `mw101.unison.count` | Choice | 0:2 1:3 2:4 | 0 (2) | **no** (structural) | NoSmooth | voice |
| `mw101.control.vintage` | Bool | 0:Modern 1:Vintage | 0 (Modern) | **no** (structural) | NoSmooth | control-rate (6-bit CV topology switch) |

Deprecated / alias-only (NOT live; retained for migration only):

| ID | Status | Migrates to |
| --- | --- | --- |
| `mw101.os.factor` | deprecated migration alias (ADR-018 Q8; only if ever minted at v1) | `mw101.quality` |

`<extras>` non-parameter state (NOT in the host parameter list, §5.4):
`<seq>` 100-step pattern (per-step note/rest/tie/gate; NO accent — §5.4),
`arpLatch` bool, `driftSeed` int64, `seedLocked` bool, the CC-learn bindings,
`uiWidth`/`uiHeight`, `renderOptIn`, optional `rawNewerBlob` [ADR-008 C8;
ADR-009 §8.2; ADR-012 §Consequences].

Parameter count: **91 live `AudioProcessorParameter`s** (every row in the live
table above; of these, 5 are structural / non-automatable — `mw101.quality`,
`mw101.voice.mode`, `mw101.voice.count`, `mw101.unison.count`,
`mw101.control.vintage` — so 86 appear in host automation lists), plus the
deprecated `mw101.os.factor` alias slot (not counted as a live host parameter),
plus the `<extras>` non-parameter state.

### 3.1 Registry entry shape

`ParamDefs.h` declares one constexpr array `kParamDefs` whose element is:

```cpp
enum class ParamType : uint8_t { Continuous, Choice, Bool };

enum class ParamGroup : uint8_t {
    Vco, Sub, Noise, Mixer, Vcf, Env, Lfo, Vca,
    Glide, Mod, Arp, Seq, Key, Tune, Vel, Mpe,
    Vintage, Drift, Var, Warmup, Fx, Out, Voice, Global
};

struct ParamDef
{
    const char*   id;            // immutable "mw101.*" snake_case  [ADR-008 C1]
    const char*   label;         // advisory display name; may change freely
    ParamGroup    group;         // browser/UI grouping only
    ParamType     type;
    float         minValue;      // normalized modeled units (Continuous)
    float         maxValue;
    float         step;          // 0 => continuous; Choice uses choiceCount
    float         defaultValue;  // engine default (NOT the INIT patch; see §11)
    float         skew;          // NormalisableRange skew; 1.0 => linear
    bool          symmetricSkew; // skew about the centre (e.g. fine tune)
    const char*   unit;          // advisory display string only  [ADR-008 C4]
    const char**  choices;       // Choice/Bool labels; nullptr otherwise
    uint8_t       choiceCount;   // Choice option count (append-only)  [ADR-008 C5]
    uint8_t       canonicalChoiceCount; // hardware-canon indices; extras above [C6]
    bool          isAutomatable; // false => structural / not in automation list
    bool          isDiscrete;    // host hint for stepped params
    SmoothingClass smoothing;    // §3.9; defaults to NoSmooth  [ADR-020 S9]
    uint16_t      versionAdded;  // schemaVersion in which this ID first shipped
    bool          isSoftwareExt; // true => Roland-Cloud-only artifact  [ADR-008 C6/C15]
};
```

Invariants enforced by `static_assert` and CI: IDs unique and `mw101.`-prefixed;
choice params have `choices != nullptr` and `choiceCount >= canonicalChoiceCount`;
structural params (§3.8) have `isAutomatable == false` and `smoothing ==
NoSmooth`; any `isSoftwareExt` choice index sits at or above
`canonicalChoiceCount` [ADR-008 C1, C5, C6, C7; ADR-018 Q1-Q2; ADR-020 S8-S9].

### 3.2 ID naming and numeric-ID rule

- Every ID is an immutable `mw101.<group>.<name>` snake_case string declared only
  in `kParamDefs` [ADR-008 C1]. No dotted-camelCase or alternate-namespace
  variant is ever live: sibling docs that wrote `fx.chorus.rate`, `var.envTime`,
  `vintage.detuneAmt`, `amp.volume`, `tune.fine` or similar are normalizing TO the
  canonical snake_case IDs in §3.0 (e.g. `mw101.fx.chorus_rate`,
  `mw101.var.env_time`, `mw101.vintage.detune_amt`, `mw101.vca.level`,
  `mw101.vco.fine`). IDs are never deleted, reused, or renumbered; a deprecated
  param becomes a hidden no-op slot; a rename is a migration alias, never an
  in-place edit [ADR-008 C2; §7.4].
- VST3/AU/CLAP numeric IDs are derived deterministically by JUCE hashing the
  string ID. No hand-maintained numeric table exists [ADR-008 C3]. The backlog
  MUST construct every parameter with its string ID as the `ParameterID` so the
  hash is stable across builds.

### 3.3 Continuous sonic parameters

Continuous params are `juce::NormalisableRange<float>` in normalized modeled
units; the host automation value is always 0..1 [ADR-008 C4]. Units are advisory
display only. Skews are musical-feel choices with no physical oracle; re-skewing
later is a migration event, not a silent reinterpretation [ADR-008 §3]. All skew
values are `(PI)` and live in the calibration table; the table below states the
intended shape. The full set is also in the §3.0 index; this section groups them
by subsystem with the load-bearing notes.

| ID | Label | Range (modeled units) | Unit (advisory) | Skew | Default | Smoothing | Trace |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `mw101.vco.tune` | VCO Coarse | -24 .. +24 | semitones | linear | 0 | Pitch (S1) | osc doc; [ADR-008 §3] |
| `mw101.vco.fine` | VCO Fine | -1.0 .. +1.0 | semitones | linear, symmetric | 0 | Pitch (S1) | osc doc; SOLE fine tune (doc 09 TUNE re-points here) |
| `mw101.vco.pw` | Pulse Width | 0.0 .. 1.0 | duty | linear | 0.5 | PW (S3) | osc doc; [research/11 §4.5] |
| `mw101.vco.pwm_depth` | PWM Depth (manual) | 0.0 .. 1.0 | depth | linear | 0.0 | PW (S3) | osc doc; manual width, distinct from `lfo.depth_pwm` |
| `mw101.saw.level` | Saw Level | 0.0 .. 1.0 | level | linear | 0.8 | Level (S4) | mixer doc; [research/11 §7.2] |
| `mw101.pulse.level` | Pulse Level | 0.0 .. 1.0 | level | linear | 0.0 | Level (S4) | mixer doc; [research/11 §7.2] |
| `mw101.sub.level` | Sub Level | 0.0 .. 1.0 | level | linear | 0.0 | Level (S4) | mixer doc; [research/11 §4.4] |
| `mw101.noise.level` | Noise Level | 0.0 .. 1.0 | level | linear | 0.0 | Level (S4) | mixer doc; [research/11 §7.2] |
| `mw101.vcf.cutoff` | Cutoff | 0.0 .. 1.0 | norm freq | log-ish (PI) | 1.0 | Fast (S2) | filter doc; [research/11 §4.3] |
| `mw101.vcf.resonance` | Resonance | 0.0 .. 1.0 | resonance | linear | 0.0 | Fast (S2) | filter doc; [ADR-008 §3] |
| `mw101.vcf.env_mod` | Env Mod | 0.0 .. 1.0 | depth | linear | 0.0 | Fast (S2) | filter doc; [research/11 §4.3] |
| `mw101.vcf.lfo_mod` | VCF LFO Mod | 0.0 .. 1.0 | depth | linear | 0.0 | Fast (S2) | filter/LFO doc |
| `mw101.vcf.kbd_track` | Keyboard Track | 0.0 .. 1.0 | amount | linear | 0.0 | Fast (S2) | filter doc |
| `mw101.env.attack` | Env Attack | 0.0 .. 1.0 | time | log (PI) | 0.0 | NoSmooth | env doc; [research/11 §4.3] |
| `mw101.env.decay` | Env Decay | 0.0 .. 1.0 | time | log (PI) | 0.3 | NoSmooth | env doc; [research/11 §4.3] |
| `mw101.env.sustain` | Env Sustain | 0.0 .. 1.0 | level | linear | 1.0 | Level (S4) | env doc; [research/11 §4.3] |
| `mw101.env.release` | Env Release | 0.0 .. 1.0 | time | log (PI) | 0.1 | NoSmooth | env doc; [research/11 §4.3] |
| `mw101.lfo.rate` | LFO Rate | 0.1 .. 30.0 | Hz | log | 5.0 | Fast (S2) | LFO doc; [ADR-008 §3] |
| `mw101.lfo.depth_pitch` | LFO->Pitch | 0.0 .. 1.0 | depth | linear | 0.0 | Fast (S2) | LFO doc |
| `mw101.lfo.depth_pwm` | LFO->PWM | 0.0 .. 1.0 | depth | linear | 0.0 | Fast (S2) | LFO doc; LFO->PWM amount (PI) |
| `mw101.lfo.depth_cutoff` | LFO->Cutoff | 0.0 .. 1.0 | depth | linear | 0.0 | Fast (S2) | LFO doc; LFO->cutoff amount (PI) |
| `mw101.lfo.delay` | LFO Delay | 0.0 .. 1.0 | time | log (PI) | 0.0 | NoSmooth | LFO doc |
| `mw101.vca.level` | VCA Level | 0.0 .. 1.0 | level | linear | 0.8 | Level (S4) | VCA doc; CC7 volume target [ADR-016 R-2] |
| `mw101.glide.time` | Glide Time | 0.0 .. 5.0 | s | log | 0.0 | Glide (S5) | glide doc; [research/05 §1] |
| `mw101.mod.bend_range_vco` | Bend Range (VCO) | 0 .. 1200 | cents | linear | 200 | Pitch (S1) | mod doc; [research/05 §1] |
| `mw101.mod.bend_range_vcf` | Bend Range (VCF) | 0 .. 1200 | cents | linear | 0 | Fast (S2) | mod doc; [research/05 §1] |
| `mw101.mod.lfo_mod_wheel` | Mod Wheel->LFO | 0.0 .. 1.0 | depth | linear | 0.0 | Fast (S2) | mod doc; CC1 mod-wheel target (doc 09) |
| `mw101.tune.a4` | A4 Reference | 400 .. 460 | Hz | linear | 440 | Pitch (S1) | MIDI doc; [ADR-012 C21] (442 is a preset only) |
| `mw101.tune.slop` | Tuning Slop | 0 .. 20 | cents | linear | 2.5 | Pitch (S1) | drift doc; [ADR-009 VV-4] |
| `mw101.vel.depth` | Velocity Depth | 0.0 .. 1.0 | amount | linear | 0.5 (PI) | Fast (S2) | MIDI doc; velocity->VCA+VCF [ADR-016 R-2] |
| `mw101.amp.expression` | Expression | 0.0 .. 1.0 | scaler | linear | 1.0 | Level (S4) | MIDI doc; CC11 [ADR-012 C15] |
| `mw101.mpe.bend_range` | MPE Bend Range | 0 .. 96 | semitones | linear | 48 | Pitch (S1) | MIDI doc; [ADR-012 C11] |
| `mw101.vintage.age` | Vintage Age | 0.0 .. 1.0 | amount | linear | 0.0 | Level (S4) | drift doc; [ADR-016 R-4; ADR-009 VV-1] |
| `mw101.vintage.cal_spread` | Cal Spread | 0.0 .. 1.0 | percent | linear | 0.25 | Level (S4) | drift doc; [ADR-009 VV-6] |
| `mw101.vintage.detune_amt` | Detune Amount | 0.0 .. 1.0 | percent | linear | 0.0 | Level (S4) | drift doc; [ADR-009 VV-11] |
| `mw101.drift.depth` | Drift Depth | 0 .. 50 | cents | linear | 4 | Level (S4) | drift doc; [ADR-009 VV-2] |
| `mw101.drift.rate` | Drift Rate | 0.01 .. 1 | Hz | log | 0.1 | Level (S4) | drift doc; [ADR-009 VV-3] |
| `mw101.warmup.time` | Warm-Up Time | 0 .. 30 (0=off) | min | linear | 0 (off) | Level (S4) | drift doc; [ADR-009 VV-5] |
| `mw101.var.cutoff` | Cutoff Variance | 0.0 .. 1.0 | percent | linear | 0.0 | Level (S4) | drift doc; [ADR-009 VV-7] |
| `mw101.var.env_time` | Env-Time Variance | 0.0 .. 1.0 | percent | linear | 0.0 | Level (S4) | drift doc; [ADR-009 VV-8] |
| `mw101.var.pw` | PW Variance | 0.0 .. 1.0 | percent | linear | 0.0 | Level (S4) | drift doc; [ADR-009 VV-9] |
| `mw101.var.glide` | Glide Variance | 0.0 .. 1.0 | percent | linear | 0.0 | Level (S4) | drift doc; [ADR-009 VV-10] |
| `mw101.fx.drive_amount` | Drive | 0.0 .. 1.0 | amount | linear | 0.0 | Fast (S2) | FX doc; [ADR-010 FX-5/017] |
| `mw101.fx.drive_tone` | Drive Tone | 0.0 .. 1.0 (0.5=flat) | tilt | linear | 0.5 | Fast (S2) | FX doc; [ADR-010 FX-5] |
| `mw101.fx.drive_output` | Drive Output | 0.0 .. 1.0 | makeup | linear | 0.5 | Level (S4) | FX doc; [ADR-010 FX-5] |
| `mw101.fx.chorus_rate` | Chorus Rate | 0.0 .. 1.0 | rate | linear | 0.3 | Fast (S2) | FX doc; [ADR-010 FX-6] |
| `mw101.fx.chorus_depth` | Chorus Depth | 0.0 .. 1.0 | depth | linear | 0.5 | Fast (S2) | FX doc; [ADR-010 FX-6] |
| `mw101.fx.chorus_width` | Chorus Width | 0.0 .. 1.0 (0=mono) | width | linear | 1.0 | Level (S4) | FX doc; [ADR-010 FX-6] |
| `mw101.fx.chorus_mix` | Chorus Mix | 0.0 .. 1.0 | wet | linear | 0.0 | Level (S4) | FX doc; [ADR-010 FX-6] |
| `mw101.fx.delay_time` | Delay Time | 0.0 .. 1.0 | time | log (PI) | 0.3 | Fast (S2) | FX doc; [ADR-010 FX-7/FX-8] |
| `mw101.fx.delay_feedback` | Delay Feedback | 0.0 .. 0.95 | amount | linear | 0.3 | Fast (S2) | FX doc; 0.95 ceiling (PI) [ADR-010 FX-8] |
| `mw101.fx.delay_damp` | Delay Damp | 0.0 .. 1.0 (0=dark) | tone | linear | 0.5 | Fast (S2) | FX doc; [ADR-010 FX-8] |
| `mw101.fx.delay_width` | Delay Width | 0.0 .. 1.0 (0=mono) | width | linear | 1.0 | Level (S4) | FX doc; [ADR-010 FX-8] |
| `mw101.fx.delay_mix` | Delay Mix | 0.0 .. 1.0 | wet | linear | 0.0 | Level (S4) | FX doc; [ADR-010 FX-8] |

Notes:

- Bend-range params are clamped to a 0..1200 cents ceiling [research/05 §1;
  ADR-008 §3]; they are stored as a `NormalisableRange` whose maximum is 1200,
  the host automation value still 0..1. `mw101.mpe.bend_range` is the MPE
  per-note + master range (0..96 semitones, default 48) per [ADR-012 C11]; it is
  distinct from the channel bend ranges `mw101.mod.bend_range_*`.
- `mw101.tune.a4` defaults to 440 Hz (owner mandate, [ADR-012 C21]); the
  documented hardware 442 Hz value ships only as a "hardware-accurate" preset,
  never the default [ADR-012 C22]. Front-panel TUNE is `mw101.vco.fine` (the
  single fine-tune ID; doc 09's "TUNE" re-points here), NOT a separate
  `tune.fine`.
- `mw101.lfo.depth_pwm` (LFO->PWM amount) is DISTINCT from `mw101.vco.pwm_depth`
  (the manual pulse-width amount). `mw101.lfo.depth_pitch`,
  `mw101.lfo.depth_cutoff` and `mw101.lfo.depth_pwm` are the three faithful
  per-destination LFO depths; `mw101.lfo.dest` (§3.4) selects which destination
  the single hardware LFO routing emphasizes [LFO doc].
- Envelope time params and LFO/delay-time params are de-zippered only when noted;
  envelope segment times are NOT de-zippered (NoSmooth) because the envelope DSP
  re-reads them per stage and a ramp would fight the envelope (env doc owns this).
- `mw101.fx.delay_feedback` maximum is 0.95 to keep the feedback loop stable
  (PI); doc 07 §5.2.7 must match this ceiling. FX engine defaults are OFF via the
  master `mw101.fx.bypass` (default ON = bypassed, §3.5) and the `*_enable`/mode
  bools, even though component params have musical defaults [ADR-016 §Accepted;
  ADR-010 FX-13].
- Drift/vintage continuous params: `mw101.vintage.age` and `mw101.var.*` /
  `mw101.drift.*` PARAMETER defaults are 0 / in-tune-on-load; the INIT PATCH (§11)
  moves Age low and drift on — a patch overlay, not a parameter-default change
  [ADR-016 R-4; ADR-009 §10.2]. Ranges/units match the ADR-009 / doc 08 §10
  contract table verbatim.
- `mw101.vel.depth` and `mw101.amp.expression` are the velocity->(VCA+VCF) amount
  and the CC11 expression VCA scaler respectively; CC7 volume maps to the EXISTING
  `mw101.vca.level` and CC1 mod-wheel to the EXISTING `mw101.mod.lfo_mod_wheel`
  (no `amp.volume` ID is minted) [ADR-012 C15; doc 09 §6.2].

### 3.4 Choice (stepped) parameters

`juce::AudioParameterChoice`. Enum indices are fixed and append-only; new options
append at higher indices; existing indices never shift [ADR-008 C5]. Software-only
extensions append ABOVE the hardware-canonical indices behind the capability flag
and never occupy a canonical index [ADR-008 C6; ADR-016 §Honesty].

| ID | Label | Indices (0-based) | Canonical count | Default | Trace |
| --- | --- | --- | --- | --- | --- |
| `mw101.vco.range` | VCO Range | 0:16' 1:8' 2:4' 3:2' \| 4:32'(sw) 5:64'(sw) | 4 | 1 (8') | [research/11 §5,§6.2; ADR-008 C6] |
| `mw101.sub.mode` | Sub Mode | 0:-1 Oct Sq 1:-2 Oct Sq 2:-2 Oct Pulse | 3 | 0 | canonical ID (NOT `sub.shape`); [research/11 §4.4; ADR-008 §4] |
| `mw101.lfo.shape` | LFO Shape | 0:Tri 1:Sq 2:Random 3:Noise \| 4:Sine(sw) | 4 | 0 (Tri) | [research/11 §5,§6.1; ADR-008 C6] |
| `mw101.lfo.dest` | LFO Dest | 0:Pitch 1:Filter 2:PWM | 3 | 0 | LFO doc |
| `mw101.arp.mode` | Arp Mode | 0:Off 1:Up 2:Down 3:Up-Down | 4 | 0 (Off) | [research/11 §4.7; arp doc] |
| `mw101.arp.range` | Arp Range | 0:1 Oct 1:2 Oct 2:3 Oct | 3 | 0 | arp doc |
| `mw101.arp.sync_div` | Arp Sync Div | 0:1/4 1:1/8 2:1/8T 3:1/16 4:1/16T 5:1/32 | 6 | 1 | arp doc |
| `mw101.lfo.sync_div` | LFO Sync Div | 0:1/4 1:1/8 2:1/8T 3:1/16 4:1/16T 5:1/32 | 6 | 1 | LFO doc |
| `mw101.seq.mode` | Seq Mode | 0:Off 1:Play 2:Record | 3 | 0 (Off) | seq doc |
| `mw101.seq.sync_div` | Seq Sync Div | 0:1/4 1:1/8 2:1/8T 3:1/16 4:1/16T 5:1/32 | 6 | 3 | seq doc |
| `mw101.glide.mode` | Glide Mode | 0:Off 1:Auto 2:On | 3 | 0 (Off) | glide doc; [research/11 §4.3] |
| `mw101.mod.bend_dest` | Bend Dest | 0:VCO 1:VCF 2:Both | 3 | 0 | mod doc |
| `mw101.key.trigger_priority` | Trigger / Priority | 0:GATE 1:GATE+TRIG 2:LFO | 3 | 0 (GATE) | coupled S7 priority+retrigger; [ADR-012 C1-C4; ADR-016] |
| `mw101.vca.mode` | VCA Mode | 0:ENV 1:GATE | 2 | 0 (ENV) | VCA/env doc; (PI) |
| `mw101.mpe.pressure_dest` | MPE Pressure Dest | 0:VCF Cutoff 1:VCA Level 2:PW | 3 | 0 (VCF Cutoff) | [ADR-012 C12; ADR-022] (PI choice set) |
| `mw101.fx.chorus_mode` | Chorus Mode | 0:Off 1:I 2:II 3:I+II | 4 | 0 (Off) | FX doc; [ADR-010 FX-6] |
| `mw101.fx.delay_division` | Delay Division | 0:1/4 1:1/8 2:1/8. 3:1/8T 4:1/16 5:1/16T | 6 | 1 (1/8) | FX doc; [ADR-010 FX-7] |

Choice params are NOT value-smoothed; an audibly-discontinuous switch uses the
ADR-005 click-safe CV crossfade owned by the consuming DSP module, not a smoother
on the index [ADR-020 S7]. `isSoftwareExt` choice indices (32'/64' on
`mw101.vco.range`; Sine on `mw101.lfo.shape`) are the only ones above the
canonical count and force `sound_ext: true` on any preset that uses them
[ADR-008 C6/C15; §6.4].

`mw101.key.trigger_priority` is the SINGLE coupled priority+retrigger switch (it
is never split into two params): GATE = lowest-note + no legato retrigger;
GATE+TRIG = last-note + retrigger every key; LFO = lowest-note + LFO/clock-
triggered envelopes [ADR-012 C1-C4; doc 09 §4.2]. `mw101.vca.mode` selects whether
the VCA follows the envelope (ENV) or the raw gate (GATE) (PI). The
`mw101.fx.delay_division` set mirrors doc 07 §5.2.3 `{1/4, 1/8, 1/8., 1/8T, 1/16,
1/16T}` and ADR-010 FX-7.

### 3.5 Boolean parameters

`juce::AudioParameterBool`, index 0=false, 1=true (append-only like Choice).

| ID | Label | Default | Trace |
| --- | --- | --- | --- |
| `mw101.lfo.tempo_sync` | LFO Tempo Sync | false | LFO doc; [ADR-008 §4] |
| `mw101.arp.tempo_sync` | Arp Tempo Sync | true | arp doc |
| `mw101.arp.latch` | Arp Latch (live) | false | arp doc — NOTE: persisted latch state is `<extras>`, not this control |
| `mw101.seq.tempo_sync` | Seq Tempo Sync | true | seq doc |
| `mw101.vintage.enable` | Drift Enable | false | drift doc; [ADR-016 R-4] |
| `mw101.vel.enable` | Velocity Sensing | true | MIDI doc; [ADR-016 R-2] |
| `mw101.pitch.modern_unquantized` | Modern Un-Quantized Pitch | false | MIDI doc; [ADR-012 C7] |
| `mw101.mpe.enable` | MPE-lite Enable | false | MIDI doc; [ADR-012 C10] |
| `mw101.fx.bypass` | FX Master Bypass | **true (bypassed)** | FX doc; FX-default-OFF [ADR-010 FX-13; ADR-016] |
| `mw101.fx.drive_enable` | Drive Enable | false | FX doc; [ADR-010 FX-13] |
| `mw101.fx.chorus_enable` | Chorus Enable | false | FX doc; [ADR-010 FX-13] |
| `mw101.fx.delay_enable` | Delay Enable | false | FX doc; [ADR-010 FX-13] |
| `mw101.fx.delay_sync` | Delay Tempo Sync | false | FX doc; [ADR-010 FX-7] |
| `mw101.fx.delay_pingpong` | Delay Ping-Pong | false | FX doc; [ADR-010 FX-8] |
| `mw101.out.mono` | Mono Output | false | FX doc; global phase-coherent mono collapse [ADR-010 FX-9] |

`mw101.arp.latch` is the live momentary control; the saved arp latch state at
load time is restored from `<extras>` (§5.4) per ADR-008 C8 — the control and the
persisted state are distinct and the backlog MUST NOT conflate them.

`mw101.fx.bypass` defaults to **true = bypassed** so the engine/INIT state is FX
OFF (a reviewer hears the bare blessed mono voice first); this is the master
early-out of ADR-010 FX-1/FX-13 and maps to doc 07's `FxParams::masterBypass`
(also default-true). `mw101.fx.chorus_enable` pairs with `mw101.fx.chorus_mode`
(mode Off is the per-block early-out); both being off means no chorus DSP runs.
`mw101.out.mono` maps to doc 07's `FxParams::monoOutput`.

### 3.6 Tempo-sync rate model

For LFO/arp/seq, a free-vs-sync toggle (`mw101.*.tempo_sync`) selects between the
free continuous rate (`mw101.lfo.rate`, free arp/seq rate owned by their docs)
and the choice subdivision param (`mw101.*.sync_div`) [ADR-008 §4]. The toggle is
a Bool param (§3.5); the subdivision is a Choice param (§3.4). For the FX Delay
the equivalent toggle is `mw101.fx.delay_sync` selecting between
`mw101.fx.delay_time` (free) and `mw101.fx.delay_division` (synced) [ADR-010
FX-7]. The actual host-tempo read and division math are owned by the
arp/seq/LFO/FX docs.

### 3.7 The Quality structural parameter (ADR-018)

There is EXACTLY ONE quality control. It is the only structural quality surface;
no second oscillator-AA or oversample-factor parameter exists [ADR-018 Q1]. It is
the SOLE live structural quality param; `mw101.os.factor` is a deprecated
migration alias only (§3.0, §7.4) [ADR-018 Q8].

| ID | Label | Type | Indices | Default | Automatable | Smoothing | versionAdded |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `mw101.quality` | Quality | Choice | 0:Eco 1:Standard 2:HQ | 1 (Standard) | false | NoSmooth | 1 |

- Standard (index 1) is the default and the blessed bit-exact reference on macOS
  arm64 and the Linux x64 gate [ADR-018 Q4; ADR-023 V13].
- Quality is `withAutomatable(false)`, per-instance saved state, serialized with
  the canonical ValueTree, carried through the migration chain, applied only via
  prepareToPlay-style reconfiguration against pre-allocated max-factor buffers; it
  MUST NOT allocate or lock on the audio thread [ADR-018 Q2-Q3, Q5; ADR-008
  C7/C19].
- The enum-to-engine derivation (the backlog implements verbatim) [ADR-018
  Contract table]:

| Quality | Oversample factor | Oscillator AA mode | Blessed reference |
| --- | --- | --- | --- |
| Eco | 1x | PolyBLEP (base rate) | no |
| Standard | 2x | PolyBLEP (base rate) | yes (default) |
| HQ | 4x | minBLEP HQ tier | no |

- The derivation lives in a centralized table (`core/calibration/Calibration.h`
  or an engine-side `QualityMap`) referenced by the engine, not inlined [ADR-018
  Consequences].
- The `>~2 kHz`-fundamental minBLEP auto-escalation is internal model behavior in
  every tier and MUST NOT be exposed as a parameter [ADR-018 Q6].
- The CPU governor MAY transiently drop the active oversample stride toward 1x;
  this MUST NOT write the saved `mw101.quality` value [ADR-018 Q7].
- ID note: ADR-008 §4 named a structural slot `mw101.os.factor`. Per append-only
  rules the canonical live ID is `mw101.quality`; if `mw101.os.factor` was ever
  minted it is retained ONLY as a migration alias to `mw101.quality`, never
  renamed in place. Only one live structural quality ID exists [ADR-018 Q8; §7.4].

### 3.8 Other structural (non-automatable) parameters

Buffer-reallocating / topology-changing params. `isAutomatable == false`,
`smoothing == NoSmooth`, applied via prepareToPlay-style reconfiguration against
pre-allocated max-size buffers; smoothing them is forbidden [ADR-008 C7; ADR-018;
ADR-020 S8].

| ID | Label | Type | Indices / Range | Default | Trace |
| --- | --- | --- | --- | --- | --- |
| `mw101.voice.mode` | Voice Mode | Choice | 0:Mono 1:Poly 2:Unison | 0 (Mono) | [ADR-006; ADR-016 R-3] |
| `mw101.voice.count` | Poly Voices | Choice | 0:2 1:4 2:6 3:8 | 1 (4) | voice doc; [ADR-006] |
| `mw101.unison.count` | Unison Count | Choice | 0:2 1:3 2:4 | 0 (2) | voice doc; [ADR-006] |
| `mw101.control.vintage` | Vintage Control | Bool | 0:Modern 1:Vintage | 0 (Modern) | [ADR-005; ADR-016 R-1] |

`mw101.control.vintage` is structural because it switches the control-rate / CV
topology between the modern hi-res path and the 6-bit additive-integer path; the
ADR-005 click-free crossfade handles the audible transition [ADR-016 R-1;
ADR-020 S7]. Voice/unison counts enumerate the supported set owned by ADR-006.
`mw101.quality` (§3.7) is the fifth structural param.

### 3.9 Smoothing class (ADR-020)

```cpp
enum class SmoothingClass : uint8_t {
    NoSmooth = 0,   // default; stepped/structural/envelope-time
    Pitch,          // S1  ~2 ms  (PI)
    Fast,           // S2  ~10 ms (PI)
    PulseWidth,     // S3  ~5 ms  (PI)
    Level,          // S4  ~15 ms (PI)
    Glide           // S5  ~20 ms (PI)  (the glide-time CONTROL value only)
};
```

- The default class for any new param is `NoSmooth`; a continuous de-zipper is an
  explicit per-param opt-in in `ParamDefs` [ADR-020 S9].
- Smoothed params use a single one-pole exponential de-zipper (the same kind as
  the ADR-009 drift output smoother) with a fixed snap-to-target threshold so the
  integer "is-smoothing" state is deterministic [ADR-020 S10].
- De-zippers advance on the control-rate tick / per-block cadence owned by
  ADR-005/ADR-016; they do not run at an independent rate [ADR-020 S11].
- The portamento / S&H pitch slew between held steps is glide DSP, NOT a
  parameter de-zipper, and is owned by the control-rate/voice path; it MUST NOT be
  duplicated as a smoother [ADR-020 S5-S6]. `mw101.glide.time` (class Glide / S5)
  de-zippers only the user knob value.
- Class-to-param mapping used by §3.0: Pitch (S1) = `mw101.vco.tune`,
  `mw101.vco.fine`, `mw101.mod.bend_range_vco`, `mw101.tune.a4`,
  `mw101.tune.slop`, `mw101.mpe.bend_range`; Fast (S2) = VCF cutoff/resonance/
  env_mod/lfo_mod/kbd_track, `mw101.lfo.rate`, the three `mw101.lfo.depth_*`,
  `mw101.mod.bend_range_vcf`, `mw101.mod.lfo_mod_wheel`, `mw101.vel.depth`, and the
  fast FX continuous params (drive amount/tone, chorus rate/depth, delay
  time/feedback/damp); PulseWidth (S3) = `mw101.vco.pw`, `mw101.vco.pwm_depth`;
  Level (S4) = all level/mix/wet/percent params (saw/pulse/sub/noise levels,
  env.sustain, vca.level, amp.expression, vintage.age/cal_spread/detune_amt,
  drift.depth/rate, warmup.time, all `var.*`, drive_output, chorus width/mix,
  delay width/mix); Glide (S5) = `mw101.glide.time`. Everything else is NoSmooth
  (envelope A/D/R times, all choice/bool params, all structural params).
- Structural params (§3.7, §3.8) MUST NOT have a smoother [ADR-020 S8].

Real-time invariant: all smoother state is POD, sized and reset at
`prepareToPlay`, runs on the audio thread with no heap allocation and no locks
[ADR-020 S14]. The de-zipper VALUE is CLASS-FP; its block-boundary update and snap
bookkeeping are CLASS-EXACT (owned by the testing doc) [ADR-020 S12].

### 3.10 Calibration constants (PI)

All invented numeric constants this contract references — the per-class smoothing
time constants (S1 ~2 ms, S2 ~10 ms, S3 ~5 ms, S4 ~15 ms, S5 ~20 ms), the
de-zipper snap threshold, the continuous-param skew factors, the
`mw101.fx.delay_feedback` 0.95 ceiling, the `mw101.vel.depth` default, the drift
band widths and FX `(PI)` constants named by docs 07/08, and the INIT-patch
values (§11) — are `(PI)` and live in the single calibration table
`core/calibration/Calibration.h` [ADR-008 §1; ADR-020 S13]. `ParamDefs`
references named constants from that table; no smoothing time, skew, or default
is inlined at a DSP call site. A re-tune is one localized edit; re-skewing a
shipped range is additionally a migration event (§7) and a de-zipper
time-constant change forces a re-bless (testing doc) [ADR-020 Consequences].

## 4. APVTS layout generation

```cpp
// core/params/ParameterLayout.cpp
juce::AudioProcessorValueTreeState::ParameterLayout buildParameterLayout();
```

- Iterates `kParamDefs` and emits one parameter per entry: `Continuous` ->
  `AudioParameterFloat` with `NormalisableRange<float>{min, max, step, skew}`
  (symmetric skew where `symmetricSkew`); `Choice` -> `AudioParameterChoice` with
  the fixed label list; `Bool` -> `AudioParameterBool`.
- Each is constructed with `juce::ParameterID{def.id, def.versionAdded}` so the
  string ID drives the deterministic host numeric ID [ADR-008 C3]. (The
  `versionHint` is the JUCE VST3 group-version hint, set to `versionAdded`.)
- Structural params (§3.7, §3.8) are constructed `withAutomatable(false)`;
  software-ext choice indices are present in the label list but flagged so the UI
  can fence them [ADR-008 C6/C7; ADR-018 Q2].
- No parameter is constructed anywhere else; the layout is a pure function of
  `kParamDefs` [ADR-008 §1].

Real-time invariant: layout construction happens once at processor construction
on the message thread; the audio thread holds cached `std::atomic<float>*`
pointers obtained via `apvts.getRawParameterValue(id)` and reads them lock-free
[ADR-008 C19].

## 5. State tree and serialization

### 5.1 Canonical tree shape

The canonical state is ONE `juce::ValueTree` (ADR-008 §5):

```
<MW101_STATE schemaVersion="N" pluginVersion="x.y.z"
             engineVersion="A.B.C" renderVersion="R">
  <PARAMS> ... APVTS state (juce::AudioProcessorValueTreeState::state) ... </PARAMS>
  <extras> ... §5.4 ... </extras>
</MW101_STATE>
```

- `schemaVersion` (int) versions state SHAPE and drives the migration chain
  [ADR-008 C9-C10].
- `pluginVersion` (string) is the marketing version [ADR-008 C9].
- `engineVersion` (`MAJOR.MINOR.PATCH` string) is informational and MUST NOT
  trigger migration [ADR-023 V1-V2].
- `renderVersion` (int) versions rendered AUDIO; orthogonal to `schemaVersion`;
  drives the legacy-render path and the load-time opt-in (§9) [ADR-023 V3-V4].
- `<PARAMS>` is the APVTS `state` ValueTree (every parameter ID's current value).
- `<extras>` holds non-parameter state (§5.4) [ADR-008 C8].

### 5.2 Serializer interface

```cpp
// core/state/StateSerializer.h
namespace mw101::state {

// Build the canonical tree from live APVTS + extras (message thread).
juce::ValueTree captureState(const juce::AudioProcessorValueTreeState& apvts,
                             const Extras& extras,
                             int schemaVersion, juce::String pluginVersion,
                             juce::String engineVersion, int renderVersion);

// Serialize canonical tree -> host opaque blob (JUCE binary).  [ADR-008 C9]
void writeToBlob(const juce::ValueTree& canonical, juce::MemoryBlock& dest);

// Parse host blob -> canonical tree; returns {} on structural failure.
std::optional<juce::ValueTree> readFromBlob(const void* data, int sizeBytes);

} // namespace mw101::state
```

`getStateInformation` => `captureState` then `writeToBlob`. There is exactly ONE
serializer and ONE migration chain shared by sessions and presets [ADR-008 §5,
§6]. All of this runs on the message thread [ADR-008 C19].

### 5.3 setStateInformation flow (message thread)

1. `readFromBlob` -> canonical tree, or structural-parse failure -> §8 L1.
2. Read `schemaVersion`; run the migration chain to CURRENT (§7) [ADR-008 C10].
3. Read `renderVersion`; if `< CURRENT_RENDER_VERSION`, pin the legacy-render
   constant-set and raise the opt-in flag (§9) [ADR-023 V8-V9].
4. Validate and bind: clamp out-of-range continuous values into their
   `NormalisableRange`, reset invalid choice indices to default, default missing
   params, preserve unknown future params for round-trip [ADR-008 C11; ADR-021
   L2-L4].
5. Assemble the recovered state FULLY on the message thread, then hand off:
   parameters via APVTS atomic stores; `<extras>` via the pre-allocated lock-free
   SPSC double-buffer (§5.4) [ADR-008 C19-C20; ADR-021 L7].
6. Coalesce any deviations into exactly one non-modal warning (§8 L12).

The audio thread MUST NOT observe a half-applied load; if a complete valid state
cannot be assembled, the previously-running state is left untouched [ADR-021 L7].

### 5.4 The `<extras>` subtree and audio-thread handoff

`<extras>` carries the non-parameter state [ADR-008 C8]:

| Child / attribute | Type | Notes |
| --- | --- | --- |
| `<seq>` | child | The 100-step pattern (§5.5). |
| `arpLatch` | bool | Persisted arp latch state [ADR-008 C8]. |
| `driftSeed` | int64 | Per-instance drift RNG seed [ADR-008 C8; ADR-009 §8.2]. |
| `seedLocked` | bool | When true, INIT/preset load preserves the seed [ADR-009 §8.3]. |
| `<ccLearn>` | child | The MIDI CC-learn bindings (CC# -> param index) [ADR-012 §Consequences; doc 09 §9]. |
| `uiWidth`, `uiHeight` | int | UI editor size (advisory). |
| `renderOptIn` | bool | Sticky "updated to CURRENT renderVersion" flag (§9). |
| `rawNewerBlob` | binary (optional) | Retained raw blob when a newer-than-current state was loaded (§8 L6). |

POD payload handed to the audio thread:

```cpp
// core/state/Extras.h
struct SeqStep {                 // POD, no heap
    int8_t  noteSemitone;        // relative to base; clamped on load
    bool    gate;                // gate on/off
    bool    tie;                 // legato/slur tie  [research/11 §4.7 — labelled gap]
    bool    rest;                // rest step        [research/11 §4.7 — labelled gap]
    // NO per-step accent field — the SH-101 sequencer has NO per-step accent in
    // v1 [ADR-025; ADR-007]. Step state is note/rest/tie/gate ONLY.
};

inline constexpr int kMaxSeqSteps = 100;   // fixed capacity  [ADR-008 C20; research/11 §4.7]

struct Extras {                  // trivially copyable, no heap, no locks
    std::array<SeqStep, kMaxSeqSteps> steps{};
    int     stepCount = 0;       // 0..100 active steps
    bool    arpLatch  = false;
    int64_t driftSeed = 0;
    bool    seedLocked = false;
};
```

The sequence is a fixed-capacity 100-step array; a stored pattern shorter or
longer is padded/clamped without allocation [ADR-008 C20; ADR-021 L8]. The
audio-thread handoff is a single pre-allocated lock-free SPSC double-buffer swap;
no heap allocation and no locks on the audio thread [ADR-008 C19-C20; ADR-021
L7]. `tie`/`rest` are modeled per the cultural idiom but are a LABELLED research
gap [research/11 §4.7, §6.4] — they ship as state fields; whether the sequencer
DSP honors them is owned by the seq doc, not this contract.

Per-step ACCENT is REMOVED in v1: the SH-101 hardware sequencer has no per-step
accent, so the contract carries note/rest/tie/gate only [ADR-025; aligns to
ADR-007]. The CC-learn bindings and the drift seed/lock are non-parameter state
co-located here so the entire round-trip surface is one atomic tree
[ADR-012 §Consequences; ADR-009 §8.3].

### 5.5 `<seq>` serialization

`<seq>` carries `stepCount` and one `<step>` child per active step with
`note`, `gate`, `tie`, `rest` attributes (NO `accent` attribute — removed in v1).
On load, `stepCount` is clamped to `[0, 100]`; missing per-step attributes default
(gate=true, tie/rest=false, note=0); excess steps beyond 100 are dropped
[ADR-021 L8]. The `<seq>` element is the canonical home of the culturally
load-bearing stored pattern [ADR-008 §6; research/11 §4.7, §4.8] and MUST be
captured by every SeqArpRiff preset (§10.3).

## 6. On-disk preset format

### 6.1 Format and location

Factory presets are human-readable, git-diffable JSON, one file per preset, with
extension `.mw101preset`, organized by category folder under `presets/`
[ADR-008 C13; §10]. Each preset is a thin projection of the same canonical
ValueTree, round-tripped through the ONE (de)serializer [ADR-008 §6].

### 6.2 JSON schema

```jsonc
{
  "schemaVersion": 1,
  "meta": {
    "name": "Acid Squelch",
    "author": "Matt Woolly",
    "category": "AcidBassLead",          // §6.5 enum, exactly one
    "tags": ["acid", "resonant", "303-adjacent"],
    "description": "Inspired-by acid-house lead; not a track reconstruction.",
    "inspired_by": null,                  // nullable; inspired-by/disputed only
    "sound_ext": false                    // true iff a software-only feature used
  },
  "params": {                             // every registry ID present, in range
    "mw101.vcf.cutoff": 0.42,
    "mw101.vcf.resonance": 0.85,
    "mw101.vco.range": 1,                 // choice index (canonical or appended)
    "mw101.lfo.shape": 0,
    "mw101.fx.bypass": true,              // FX default OFF unless preset engages
    "mw101.quality": 1                    // structural params included
    // ... all live IDs from §3.0 ...
  },
  "seq": {                                // present for SeqArpRiff; else empty
    "stepCount": 16,
    "steps": [ { "note": 0, "gate": true, "tie": false, "rest": false } ]
  },
  "arp": {                                // arp config snapshot
    "latch": false
  }
}
```

Per-step objects carry `note`/`gate`/`tie`/`rest` ONLY; there is no `accent`
field (§5.4/§5.5) [ADR-025].

### 6.3 Projection interface

```cpp
// core/preset/PresetFormat.h
namespace mw101::preset {

struct PresetMeta {
    juce::String name, author, category, description;
    juce::StringArray tags;
    juce::String inspiredBy;   // empty => null
    bool soundExt = false;
};

// JSON file -> canonical ValueTree (then runs the §7 migration chain).
// Returns nullopt on malformed JSON or schema-validation failure (-> §8 L11).
std::optional<juce::ValueTree> loadPresetJson(const juce::File& file,
                                              PresetMeta& outMeta);

// canonical ValueTree (+meta) -> JSON text (authoring/export).
juce::String writePresetJson(const juce::ValueTree& canonical,
                             const PresetMeta& meta);

} // namespace mw101::preset
```

### 6.4 Validation rules (CI + load)

A preset MUST satisfy, else it is rejected (CI) or recovered (load §8 L11)
[ADR-008 C13, C15, C18]:

- `schemaVersion` present; `meta.name`, `meta.author`, `meta.category` present;
  `category` is one of the §6.5 enum [ADR-008 C14].
- Every registry ID present in `params`; every continuous value within its
  `NormalisableRange`; every choice index `< choiceCount` [ADR-008 C18].
- `sound_ext == true` iff any param uses a software-only feature: `mw101.vco.range`
  index `>= 4` (32'/64') or `mw101.lfo.shape` index `== 4` (Sine) [ADR-008 C6,
  C15; research/11 §6.1, §6.2].
- Attribution discipline: `inspired_by` is null or an inspired-by/disputed
  reference, never "as used on track X"; no preset name/description/tag ships a
  "TB-303 filter" descriptor [ADR-008 C16; research/11 §4.2, §7.3]. CI greps for
  the forbidden descriptor and for forbidden phrasings.
- No per-step `accent` field appears in any `seq.steps` object (v1 has no per-step
  accent) [ADR-025]; CI rejects a preset that carries one.
- CI MUST mirror `presets/` 1:1 into the BinaryData manifest before embedding
  [ADR-008 C18].

### 6.5 Category enum (research taxonomy)

`category` MUST be exactly one of [ADR-008 C14; research/11 §7.1]:

| Category | Idiom | Trace |
| --- | --- | --- |
| `AcidBassLead` | Self-osc resonant LPF, high resonance, fast-decay zero-sustain filter env, glide; sequencer/arp-driven, overdrive-friendly | [research/11 §4.3, §7.1] |
| `SubBass` | Independent-level sub-osc -1/-2 oct under a clipped VCA; voiced under a 303 line | [research/11 §4.4, §4.8, §7.1] |
| `Lead` | Bright saw/square with resonance; vibrato/PWM motion | [research/11 §4.6, §7.1] |
| `PWMStrings` | Mono PWM stylization + chorus; labelled NOT a true poly pad | [research/11 §4.5, §7.1] |
| `BlipsFX` | Very short A/D on a resonant filter + noise FX; general-practice character | [research/11 §4.6, §7.1] |
| `SeqArpRiff` | Authored with a stored sequencer pattern / arp settings; the riff IS the identity (Voodoo Ray) | [research/11 §4.7, §4.8, §7.1] |

## 7. Migration chain

### 7.1 Interface

```cpp
// core/state/Migration.h
namespace mw101::state {

// Pure transform: in-place upgrade of a canonical tree from version n to n+1.
// MUST NOT throw; MUST be a pure function of its input tree.
using MigrationStep = std::function<void(juce::ValueTree& canonical)>;

// Ordered, contiguous chain indexed by source version (v1->v2 at index 1, ...).
const std::vector<MigrationStep>& migrationChain();

// Runs steps for [schemaVersion, CURRENT); sets schemaVersion=CURRENT.
// Tolerant of schemaVersion > CURRENT (no-op down-bind; see §8 L3).
void migrateToCurrent(juce::ValueTree& canonical);

} // namespace mw101::state
```

### 7.2 Rules

- `migrateToCurrent` runs ordered pure `migrateV(n)->V(n+1)` transforms when
  `schemaVersion < CURRENT_SCHEMA_VERSION`, on the message thread only, then binds
  to APVTS [ADR-008 C10].
- Forward/backward tolerance: missing param => default; unknown future param =>
  preserved/round-tripped; unknown future `schemaVersion` => bind known IDs
  best-effort, never crash [ADR-008 C11; §8 L2-L3].
- Every migration step has a committed frozen before/after golden fixture test in
  CI [ADR-008 C12]. Recovery cases (§8) add fixtures per L1-L11 [ADR-021 L14].
- Presets run through the SAME chain; a v1 factory preset opens correctly at the
  current version with zero per-preset edits [ADR-008 C17].

### 7.3 Version table

| schemaVersion | Change | Migration |
| --- | --- | --- |
| 1 | Initial contract (all §3.0 live IDs, `<extras>` with note/rest/tie/gate seq steps, `renderVersion`, `engineVersion`) | none (baseline) |

`CURRENT_SCHEMA_VERSION` starts at 1. Every breaking change (new param, re-skew of
a shipped range, rename via alias, structural change) bumps `schemaVersion` and
adds one ordered step plus its fixture [ADR-008 Consequences]. A pure DSP re-tune
does NOT bump `schemaVersion` and MUST NOT add a no-op step — it bumps
`renderVersion` instead [ADR-023 V4]. If a per-step `accent` field is ever found
in a loaded v1 artifact authored by a pre-ADR-025 tool, the migration drops it
silently (it was never a contracted field).

### 7.4 Rename / alias mechanism

A rename never edits an ID in place. The new ID is added; a migration step copies
the old ID's value to the new ID and marks the old slot a hidden no-op; the old
ID stays in the registry as a deprecated alias [ADR-008 C2]. The
`mw101.os.factor` -> `mw101.quality` case (§3.7) is the canonical example: if
`mw101.os.factor` was ever live, it is retained ONLY as an alias migrated to
`mw101.quality` [ADR-018 Q8]. The single fine-tune ID is `mw101.vco.fine`; any
sibling-doc `tune.fine` reference is re-pointed to it, not minted as a second ID
(doc 09 §5).

## 8. Load-failure handling (ADR-021)

### 8.1 Principle

`setStateInformation` and preset load MUST NOT throw out of the call or abort the
process on any malformed/truncated/out-of-range/unrecognized input [ADR-021 L1,
L13]. Every parse/validate/migrate step is wrapped; failure becomes a classified
recovery on the message thread only. The audio thread never parses, allocates,
locks, or observes an in-progress recovery [ADR-021 L13].

### 8.2 Graded fallback ladder

Recovery prefers the most faithful valid state reachable; INIT (the §11 patch) is
the last resort, reachable only when no interpretable params survive [ADR-021 L5]:

```cpp
// core/state/LoadFailure.h
enum class RecoveryOutcome : uint8_t {
    CleanLoad,            // no deviation
    MigratedAndBound,     // schemaVersion <= CURRENT, ran chain
    NewerDownInterpreted, // schemaVersion > CURRENT, raw retained
    ClampedValues,        // out-of-range clamped / invalid choice reset
    InitFallback          // structurally unparseable -> INIT
};

struct RecoveryReport {
    RecoveryOutcome outcome = RecoveryOutcome::CleanLoad;
    juce::StringArray notes;   // coalesced into ONE warning (L12)
};

// Never throws; always returns a complete valid canonical tree + report.
juce::ValueTree recoverState(const void* blob, int sizeBytes,
                             RecoveryReport& outReport);
```

### 8.3 Failure-case contract (verbatim from ADR-021)

| # | Case | Behavior |
| --- | --- | --- |
| L1 | Truncated/garbage/unparseable blob | No throw/crash; fall back to INIT (§11) + empty `<extras>`; one non-modal warning. |
| L2 | Parses, `schemaVersion <= CURRENT` | Run chain (§7), bind, default missing/invalid; warn only if a value was missing/clamped. |
| L3 | Parses, `schemaVersion > CURRENT` | Bind every valid known ID, default the rest, retain raw blob (L6); one warning; never reset to INIT while interpretable params survive. |
| L4 | Param out of range / invalid choice index | Clamp continuous into range; reset invalid choice to default; no crash; coalesce into the single warning. |
| L5 | Fallback ranking | Prefer partial-load-with-defaults over full reset; INIT only when no interpretable params survive. |
| L6 | Round-trip preservation | Retain `rawNewerBlob`; next `getStateInformation` re-emits preserved newer data merged with this-session edits to known params. |
| L7 | Never-torn application | Assemble fully on the message thread before handoff; APVTS atomics + SPSC `<extras>`; on failure leave the running state untouched. |
| L8 | Wrong-length stored sequence | Pad/clamp into the fixed-capacity 100-step structure (§5.4); no audio-thread allocation; no crash. |
| L9 | Missing/undecodable embedded factory preset | That slot resolves to INIT and warns naming it; the rest of the bank still loads; construction never aborts/empties the bank. |
| L10 | Missing user preset file | Load INIT; one non-modal warning; no crash. |
| L11 | Malformed `.mw101preset` (bad JSON / failed validation) | No crash; load INIT (or partial per L2/L3 if the canonical projection is recoverable); warn. |
| L12 | Warning surfacing | Exactly one non-modal, dismissible, message-thread-only warning per deviating load (never modal/blocking); coalesce sub-failures; if the editor is closed, record + log + show when it next opens. |
| L13 | RT safety of recovery | All classification/parse/migrate/validate/fallback/raw-retention/warning on the message thread only. |
| L14 | Recovery fixtures | Each of L1-L11 has a committed fixture asserting no crash, correct fallback target, raw preservation where required (L6), one coalesced warning (L12). |

### 8.4 Warning affordance

The warning is a plugin-owned, non-modal, dismissible message-thread affordance,
decoupled from the reimagined UI look [ADR-021 L12; ADR-016 §UI]. A deferred-record
mechanism shows the warning when the editor next opens if it was closed at load
time, and every recovery is logged for field triage [ADR-021 §Consequences]. The
visual design is owned by the UI doc; this contract owns only the data
(`RecoveryReport`) and the non-modal/coalesce/deferred rules.

## 9. Engine and render versioning (ADR-023)

### 9.1 Constants

```cpp
// core/version/EngineVersion.h
namespace mw101::version {
    inline constexpr int          kCurrentSchemaVersion = 1;
    inline constexpr int          kCurrentRenderVersion = 1;   // bump on bless-change
    inline constexpr const char*  kEngineVersion        = "1.0.0"; // MAJOR.MINOR.PATCH
    inline constexpr const char*  kPluginVersion        = "1.0.0"; // marketing
}
```

### 9.2 renderVersion lifecycle in state

- The state root carries `renderVersion` (int), serialized on the message thread
  alongside `schemaVersion`/`pluginVersion`/`engineVersion` [ADR-023 V3].
- `renderVersion` is orthogonal to `schemaVersion`: a pure DSP re-tune bumps
  `renderVersion` only; a parameter-shape change bumps `schemaVersion` only; a
  DSP-only change MUST NOT add a migration step [ADR-023 V4].
- On load, if the session's `renderVersion < kCurrentRenderVersion`: keep
  rendering at the session's stored `renderVersion` (the legacy-render
  constant-set is selected at `prepareToPlay`, never at audio rate) and raise a
  non-modal message-thread opt-in offering "update engine (audio will change)"
  [ADR-023 V8-V10, V18]. Accepting writes `kCurrentRenderVersion` into state on
  next save (and sets the sticky `<extras>` `renderOptIn`); declining is sticky.
  A session never silently changes its audio [ADR-023 V8-V9].
- New/blank sessions and new factory presets author at `kCurrentRenderVersion`
  [ADR-023 V9].
- `engineVersion` is informational and MUST NOT trigger migration [ADR-023
  V1-V2].

This doc owns only the state-resident lifecycle above. The bless-tool governance
(bump-on-bless, MANIFEST recording, CI completeness, engine-tag carriage), the
blessed sample-rate set `{44100, 48000, 88200, 96000}` Hz, the high-host-SR clamp
and the FP-tolerance tiers are owned by the testing doc [ADR-023 V5-V7, V11-V17].

## 10. Preset bank and PresetManager

### 10.1 Construction

```cpp
// core/preset/PresetManager.h
class PresetManager {
public:
    PresetManager();                         // loads embedded bank (message thread)
    int           getNumPresets() const noexcept;
    juce::String  getName(int index) const;
    juce::String  getCategory(int index) const;
    // Applies a preset's canonical tree to APVTS + extras via the §5.3 handoff.
    void          loadPreset(int index, juce::AudioProcessorValueTreeState&,
                             Extras&, RecoveryReport& outReport);
    // category -> indices, for the browser owned by the UI doc.
    juce::Array<int> indicesForCategory(juce::StringRef category) const;
};
```

### 10.2 Bank rules

- The ~64 factory presets are embedded via CMake/BinaryData and loaded into the
  in-memory bank at construction [ADR-008 §6; research/11 §7.1]. CI mirrors
  `presets/` 1:1 [ADR-008 C18].
- A missing/undecodable embedded preset resolves that slot to INIT and warns
  naming it; construction never aborts or empties the bank [ADR-021 L9].
- Loading a preset runs it through the SAME migration chain (§7) and the SAME
  validation/recovery (§8) as session state [ADR-008 C17; ADR-021 L11].
- Preset application uses the §5.3 message-thread assembly + atomic/SPSC handoff;
  no audio-thread allocation [ADR-008 C19].

### 10.3 Authoring discipline

- Every preset declares exactly one `category` from §6.5 [ADR-008 C14].
- Factory presets are authored against the modern-default poles (modern control,
  velocity on, mono, subtle drift) per the INIT baseline (§11); a faithful preset
  bank is the counterbalance [ADR-016 §Consequences].
- SeqArpRiff presets MUST capture their stored `<seq>` pattern (note/rest/tie/gate
  steps; no accent) [ADR-008 §6; ADR-025; research/11 §4.7, §4.8].
- Any preset using a software-only feature sets `sound_ext: true` (§6.4)
  [ADR-008 C15].
- FX presets engage FX only where the research prescribes it (notably the
  "PWM / Strings" category = mono PWM + chorus); the default is `mw101.fx.bypass`
  ON and `*_enable` off [ADR-010 FX-13; research/11 §4.5].
- The "hardware-accurate" tuning preset sets `mw101.tune.a4 = 442`; 442 is never
  the engine default [ADR-012 C22].
- Attribution renders as inspired-by/disputed, never "as used on track X"; no
  "TB-303 filter" descriptor [ADR-008 C16; research/11 §7.3].

## 11. INIT patch (out-of-box defaults, ADR-016)

The INIT patch is the canonical fallback (§8) and the out-of-box state. It is a
PATCH built from `ParamDefs` defaults with the ADR-016 pole selections applied;
the per-parameter `defaultValue` in §3 is NOT changed by ADR-016 — INIT is a
patch overlay, and the `<extras>` sequence is empty by default [ADR-021 L1; ADR-016
Contract]. INIT values are `(PI)` and live in the calibration table (§3.10).

| Surface | INIT value | Param(s) | Faithful pole (one action) | Trace |
| --- | --- | --- | --- | --- |
| Control rate / pitch quant | MODERN-SMOOTH | `mw101.control.vintage` = Modern (0) | Vintage (1) | [ADR-016 R-1] |
| Velocity | ON (-> VCA + VCF amount) | `mw101.vel.enable` = true, `mw101.vel.depth` low-mid `(PI)` | `mw101.vel.enable` = false | [ADR-016 R-2] |
| Voice mode | MONO | `mw101.voice.mode` = Mono (0) | Mono is the faithful pole; Poly/Unison are the modern toggle | [ADR-016 R-3] |
| Analog drift | Subtle ON, Age LOW | `mw101.vintage.enable` = true, `mw101.vintage.age` = low `(PI)` | Age=0 / drift off (param default 0 unchanged) | [ADR-016 R-4] |
| Quality | Standard | `mw101.quality` = Standard (1) | n/a (bless default) | [ADR-018 Q4] |
| FX | engine OFF | `mw101.fx.bypass` = true, `mw101.fx.*_enable` = false, `mw101.fx.chorus_mode` = Off | bakeable into presets | [ADR-016 §Accepted; ADR-010 FX-13] |
| Tuning | A4 = 440 Hz | `mw101.tune.a4` = 440 | 442 via "hardware-accurate" preset | [ADR-012 C21-C22] |
| MPE | OFF | `mw101.mpe.enable` = false | opt-in lower-zone 1..15 | [ADR-012 C10] |
| Modern un-quantized pitch | OFF | `mw101.pitch.modern_unquantized` = false | n/a (escape hatch is the opt-in) | [ADR-012 C7] |
| renderVersion | CURRENT | root `renderVersion` = `kCurrentRenderVersion` | n/a | [ADR-023 V9] |

The `mw101.vintage.age` PARAMETER default stays 0 (in tune on load); the INIT
PATCH moves it low and enables drift — a patch choice, not a parameter-default
change [ADR-016 R-4]. Likewise `mw101.fx.bypass` PARAMETER default is already true
(FX off), so INIT inherits FX-off directly. Reaching the most-authentic experience
(6-bit stepping + no velocity + dead-clean) is a deliberate multi-action path
mitigated by a labeled "Vintage / Faithful" factory preset [ADR-016 §Consequences].

## 12. Real-time invariants (summary)

- All parsing, migration, validation, BinaryData decode, file IO, preset
  application, recovery and warning surfacing run on the message thread ONLY
  [ADR-008 C19; ADR-021 L13; ADR-023 V18].
- The audio thread reads only lock-free APVTS atomics and the pre-allocated SPSC
  `<extras>` double-buffer; no heap allocation and no locks on the audio thread
  [ADR-008 C19-C20].
- The 100-step sequence is a fixed-capacity pre-reserved structure; a stored
  pattern of any length is padded/clamped without allocation [ADR-008 C20;
  ADR-021 L8].
- Smoother state is POD, sized/reset at `prepareToPlay`; hot smoothing paths are
  `noexcept`; structural params reconfigure against pre-allocated max-size buffers
  only at `prepareToPlay` [ADR-008 C7; ADR-020 S14; ADR-018 Q5].
- renderVersion/constant-set selection and per-SR table regen happen at
  `prepareToPlay`, never at audio rate [ADR-023 V18].

## Acceptance hooks

Objectively-testable properties a backlog task's tests MUST verify:

- Every parameter ID is `mw101.`-prefixed, snake_case `mw101.<group>.<name>`,
  unique, and declared only in `kParamDefs`; the APVTS layout is a pure function
  of `kParamDefs` (no parameter constructed elsewhere); no dotted-camelCase
  variant is live [ADR-008 C1, §1; §3.0].
- The §3.0 index is complete: every live param the sibling docs (07/08/09) cite is
  present here with exactly one canonical snake_case ID; `fx.chorus_rate`,
  `var.env_time`, `vintage.detune_amt`, `vca.level` (CC7), `mod.lfo_mod_wheel`
  (CC1), `vco.fine` (TUNE) are the canonical targets and no `amp.volume` /
  `tune.fine` / `sub.shape` ID exists.
- VST3/AU/CLAP numeric IDs are deterministic across builds (hash of the string
  ID); no hand-maintained numeric table exists [ADR-008 C3].
- Choice enum indices match §3.4/§3.7/§3.8 exactly; a property test asserts no
  canonical index shifts when a software-ext option is appended, and that
  `mw101.lfo.shape` Sine and `mw101.vco.range` 32'/64' occupy only indices `>=`
  the canonical count [ADR-008 C5-C6].
- `mw101.quality` is the ONLY quality control, is `withAutomatable(false)`,
  defaults to Standard, maps Eco/Standard/HQ -> 1x/2x/4x and PolyBLEP/PolyBLEP/
  minBLEP per §3.7; no `mw101.os.factor` or oscillator-AA param is in the live
  parameter list (`mw101.os.factor` exists only as a migration alias) [ADR-018
  Q1-Q4, Q8].
- All structural params (§3.7, §3.8) — `mw101.quality`, `mw101.voice.mode`,
  `mw101.voice.count`, `mw101.unison.count`, `mw101.control.vintage` — are
  non-automatable and carry `SmoothingClass::NoSmooth`; smoothing them is rejected
  by static assert / CI [ADR-008 C7; ADR-020 S8].
- The FX surface is complete and FX-default-OFF: `mw101.fx.bypass` defaults to
  true (bypassed); `mw101.fx.delay_feedback` range ceiling is 0.95 (matching doc
  07 §5.2.7); `mw101.out.mono` exists; chorus mode/width and drive tone/output and
  delay sync/division/damp/width/pingpong are all present [ADR-010 FX-6..FX-9,
  FX-13; doc 07].
- A continuous param of class S1-S5 de-zippers a step input (one-pole, snaps at
  the fixed threshold); a stepped/choice param does NOT smear through intermediate
  values (paired positive/negative property test) [ADR-020 S2, S7, S10, S12].
- Host automation value of every continuous param is 0..1; changing a `unit`
  string does not change the stored/automation value [ADR-008 C4].
- Round-trip: `captureState` -> `writeToBlob` -> `readFromBlob` reproduces every
  param value and the full `<extras>` (including a 100-step note/rest/tie/gate
  sequence, drift seed + lock, and CC-learn bindings) bit-for-bit [ADR-008 §5].
- No SeqStep / `<seq>` / preset `seq.steps` carries an `accent` field; the
  sequencer step state is note/rest/tie/gate only [ADR-025; ADR-007].
- The state root carries `schemaVersion`, `pluginVersion`, `engineVersion`
  (informational, no migration), and `renderVersion`; `renderVersion` and
  `schemaVersion` move independently [ADR-008 C9; ADR-023 V1-V4].
- Each migration step has a committed before/after golden fixture; a v1 preset and
  a v1 session both open correctly at CURRENT with no per-file edits [ADR-008 C12,
  C17].
- Load-failure fixtures L1-L11 each assert: no throw/crash, the correct fallback
  target, raw preservation where required (L6), wrong-length sequence padded/
  clamped without allocation (L8), and exactly one coalesced non-modal warning
  (L12); recovery never runs on the audio thread [ADR-021 L1-L14].
- On `renderVersion < CURRENT` load, audio does not change without opt-in; the
  legacy constant-set is selected at `prepareToPlay`; declining is sticky; new
  sessions/presets author at CURRENT [ADR-023 V8-V10].
- Every `.mw101preset` validates against the registry (all IDs present, in range,
  valid choice index), declares one §6.5 category, sets `sound_ext` iff a
  software-only feature is used, carries no per-step `accent`, contains no "TB-303
  filter" descriptor and no "as used on track X" attribution; CI mirrors
  `presets/` 1:1 [ADR-008 C13-C18; ADR-025; research/11 §7.3].
- A missing/undecodable embedded preset resolves that slot to INIT and warns
  without aborting or emptying the bank [ADR-021 L9].
- No heap allocation and no locks occur on the audio thread during preset/state
  application (verified by an allocation/lock detector around `processBlock`)
  [ADR-008 C19-C20].

## References

ADRs (normative contracts):

- ADR-008 — Parameter / state / preset schema and versioning (the contract).
- ADR-009 — Vintage variance / analog-drift model (the `vintage.*` / `drift.*` /
  `var.*` / `warmup.*` / `tune.slop` surface).
- ADR-010 — Built-in FX section (Chorus / Delay / Drive) — the `fx.*` / `out.mono`
  surface; FX-default-OFF.
- ADR-012 — MIDI / MPE-lite mapping & tuning reference (the `key.*` / `tune.a4` /
  `vel.*` / `amp.expression` / `mpe.*` / `pitch.modern_unquantized` surface; CC
  bindings to existing IDs).
- ADR-016 — Owner ratifications: out-of-box defaults.
- ADR-018 — Quality-tier parameter registration (`mw101.quality` sole structural
  quality param; `mw101.os.factor` alias).
- ADR-020 — Parameter smoothing / de-zipper policy (the smoothing-class column).
- ADR-021 — State / preset load-failure handling.
- ADR-022 — MPE-lite & arp/seq cross-format behavior (pressure destination,
  capability ladder feeding the `mpe.*` params).
- ADR-023 — Engine versioning, bless communication & blessed sample-rate set.
- ADR-025 — Sequencer has NO per-step accent in v1 (aligns to ADR-007); the
  `<seq>`/SeqStep contract carries note/rest/tie/gate only.
- (referenced) ADR-004 oversampling, ADR-005/ADR-016 control rate, ADR-006 voice,
  ADR-007 modulation/arp/seq, ADR-013 testing/golden, ADR-017 FX latency.

Sibling design docs aligned to the §3.0 index:

- docs/design/07-fx-section.md — FX DSP; binds the `mw101.fx.*` / `mw101.out.mono`
  IDs (delay_feedback ceiling 0.95).
- docs/design/08-vintage-variance.md — drift DSP; binds `mw101.vintage.*`,
  `mw101.drift.*`, `mw101.var.*`, `mw101.warmup.time`, `mw101.tune.slop`.
- docs/design/09-formats-io-midi.md — MIDI/MPE/tuning front-end; binds
  `mw101.key.trigger_priority`, `mw101.tune.a4`, `mw101.vco.fine` (TUNE),
  `mw101.vel.*`, `mw101.amp.expression`, `mw101.mpe.*`,
  `mw101.pitch.modern_unquantized`, and CC bindings to existing IDs.

Research docs (cited factual ground truth):

- docs/research/11-cultural-influence.md — §4.2 (TB-303 filter correction),
  §4.3-§4.8 (idioms), §5 (key parameters), §6.1-§6.2 (software-only artifacts:
  Sine LFO, 32'/64'), §7.1 (preset taxonomy), §7.3 (naming/attribution
  discipline).
- docs/research/05-mixer-modulation-glide.md — §1 (glide 0-5 s, LFO 0.1-30 Hz,
  bend sensitivity clamp +/-1200 cents), referenced via ADR-008 §3.
- docs/research/09-vintage-variance-drift.md — drift bands / variance spreads
  feeding the `vintage`/`drift`/`var` ranges (via ADR-009).
