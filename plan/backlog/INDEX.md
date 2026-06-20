<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Backlog index — execution order

All tasks start `status: todo`. Rows are grouped into **execution waves**: every task in a wave
depends only on tasks in *earlier* waves, so a whole wave runs in parallel — one agent + one git
worktree each. The task file's `status` frontmatter is the source of truth; this table is the
dependency map + dashboard. Generated from `plan/backlog/dag.json` (the machine-readable DAG).

**164 tasks across waves 0–18.** Streams: infra, core(osc/filter/env-lfo-vca), voice-control,
mod-arp-seq, fx, vintage, calibration, params, plugin, ui, golden, presets, integration, qa, ci.

| Wave | id | Title | Component | Size | Depends on | Status |
|---|---|---|---|---|---|---|
| 0 | 001 | Top-level CMakeLists + project skeleton + options | infra | S | — | done |
| 1 | 002 | CMakePresets.json schema-v6 base + sanitizer + per-platform presets | infra | M | 001 | done |
| 1 | 003 | CPM bootstrap + full-SHA dependency pin manifest | infra | M | 001 | done |
| 1 | 004 | mw_fp_discipline INTERFACE target (CompilerFlags.cmake) | infra | S | 001 | done |
| 1 | 095 | Validator target locator cmake/Validators.cmake | infra | S | 001 | done |
| 1 | 098 | Capability rung enums + ResolvedCapabilities POD (plugin/host/Capabilities.h) | app | S | 001 | done |
| 2 | 005 | mwcore static-lib target + no-JUCE-in-core build guard | infra | S | 001, 004 | done |
| 2 | 013 | docs/BUILDING.md local==CI command map + scripts/check.sh | docs | S | 002 | done |
| 2 | 096 | Configure-time format gate cmake/Formats.cmake | infra | M | 001, 095 | done |
| 3 | 005b | core/calibration/Calibration.h — single cross-module (PI) constants table + per-renderVersion frozen constant-set registry | core | M | 005 | done |
| 3 | 006 | tests/CMakeLists.txt — Catch2 binary, catch_discover_tests, silent-pass gates | qa | M | 003, 005 | done |
| 3 | 007 | core/BlockContext.h — POD seam aggregate + views | core | S | 005 | done |
| 3 | 097 | Per-platform format sets in CMakePresets.json | infra | S | 001, 096 | done |
| 4 | 008 | core/params/Smoother.h — OnePoleSmoother + ctest | core | S | 006 | done |
| 4 | 009 | core/util/Prng.h — seeded integer PRNG + CLASS-EXACT stream ctest | core | S | 006 | done |
| 4 | 010 | AudioThreadGuard alloc/lock sentinel + ctest | qa | M | 006 | done |
| 4 | 011 | License-header SPDX check + ctest-labels snapshot diff | qa | S | 006 | done |
| 4 | 012 | fp_discipline_guard — compile_commands.json forbidden-flag grep ctest | qa | S | 004, 006 | done |
| 4 | 014 | Compile-time parameter string-ID constants (ParamIDs.h) | core | S | 001, 006 | done |
| 4 | 015 | SmoothingClass enum + per-class time-constant accessor (SmoothingClass.h) | core | S | 001, 006 | done |
| 4 | 016 | Engine/render/schema version constants (EngineVersion.h) | core | S | 001, 006 | done |
| 4 | 017 | State-tree identifiers + Extras POD payload (StateTree.h, Extras.h) | core | S | 001, 006 | done |
| 4 | 026 | PolyBLEP closed-form residual (header-only) | core | S | 001, 006 | done |
| 4 | 027 | minBLEP residual table + per-voice applicator | core | M | 001, 006, 007 | done |
| 4 | 028 | White-noise source (xorshift32) | core | S | 001, 006, 007 | done |
| 4 | 033 | FastTanh.h: shared frozen tanh approximation + OTA-knee transconductor | core | S | 001, 006, 007 | done |
| 4 | 034 | LadderReferenceTPT: offline TPT/ZDF linear 4-pole oracle | core | M | 001, 006 | done |
| 4 | 035 | FilterTables: per-sample-rate CV->g and tuning-comp tables built in prepare | core | M | 001, 006, 007 | done |
| 4 | 036 | Oversampler: polyphase IIR halfband up/downsampler (realtime path) | core | M | 001, 006, 007 | done |
| 4 | 040 | SHA-256 byte hasher for golden serialization | qa | S | 001, 006 | done |
| 4 | 049 | Envelope/LFO/VCA (PI) calibration constants block | core | S | 007, 006, 001 | done |
| 4 | 050 | Envelope.h header: EnvStage/EnvTrigMode/EnvParams + Envelope class layout | core | S | 007, 006, 001 | done |
| 4 | 051 | Lfo.h header: LfoShape enum + Lfo class layout | core | S | 007, 006, 001 | done |
| 4 | 052 | Vca.h header: VcaMode enum + Vca class layout | core | S | 007, 006, 001 | done |
| 4 | 053 | ModRouting.h header: ModDepths/VelocityRouting/ModBus PODs | core | S | 007, 006, 001 | done |
| 4 | 063 | Xorshift128+ PRNG with Gaussian/cubic helpers and seed derivation | engine | S | 001, 006, 007 | done |
| 4 | 067 | VoiceTypes.h — shared voice/control PODs, enums, and pool constants | core | S | 001, 006, 007 | done |
| 4 | 081 | Control-core POD types: events, enums, ModInputs/Outputs, ControlSnapshot | core | S | 001, 006, 007 | done |
| 4 | 088 | FxParams POD snapshot struct | core | S | 001, 006 | done |
| 4 | 089 | FractionalDelayLine header-only ring buffer | core | S | 001, 006, 007 | done |
| 4 | 099 | HostEvent POD + NormalizedEventBuffer (plugin/host/HostEvent.h) | app | S | 001, 006, 007 | done |
| 4 | 106 | DesignTokens table (palette/stroke/radius/typography, single reskin knob) | ui | S | 006 | done |
| 4 | 107 | Telemetry SPSC types (Snapshot POD + Producer/Consumer, lock-free, pre-allocated) | ui | M | 006, 006 | done |
| 4 | 110 | SVG assets + BinaryData embedding (logo + static decoration) | ui | S | 001, 006 | done |
| 5 | 018 | renderVersion state lifecycle + opt-in flag (RenderVersionState.h/.cpp) | core | S | 016, 017 | done |
| 5 | 019 | Declarative parameter registry (ParamDefs.h) | core | M | 014, 015, 007 | done |
| 5 | 022 | Migration chain (Migration.h/.cpp) | core | S | 016, 017, 007 | done |
| 5 | 023 | Canonical state (de)serializer (StateSerializer.h/.cpp) | core | M | 016, 017, 007 | done |
| 5 | 029 | VCO: phase core, exp pitch, footage, drift | core | M | 001, 006, 007, 027 | done |
| 5 | 031 | Sub-oscillator: 4013 divider + diode-OR 25% pulse | core | M | 001, 006, 007, 026, 027 | done |
| 5 | 037 | Oversampler: offline linear-phase FIR halfband + reported latency (render tier) | core | S | 001, 006, 007, 036 | done |
| 5 | 038 | LadderFilter linear core: 4-stage Huovilainen cascade + cutoff mapping (no resonance) | core | M | 001, 006, 007, 033, 035 | done |
| 5 | 041 | GoldenKey / EngineTag types, hashing, and engine-context refusal | qa | S | 001, 006, 007, 040 | done |
| 5 | 046 | Manifest — parse/validate MANIFEST.toml with completeness, orphan, honesty-label and renderVersion checks | qa | M | 001, 006, 040 | done |
| 5 | 054 | Envelope.cpp: ADSR one-pole segment curve + stage machine | core | M | 007, 006, 049, 050 | done |
| 5 | 055 | Lfo rate/phase + SmoothTri and Square cores | core | M | 007, 006, 049, 051 | done |
| 5 | 056 | Vca.cpp: OTA control-law taper + tanh drive | core | M | 007, 006, 049, 052 | done |
| 5 | 057 | ModRouting.cpp: depth scaling, velocity routing, mod-bus LPF | core | M | 007, 006, 049, 053 | done |
| 5 | 064 | ThermalState OU/pink/warm-up shared thermal integrator | engine | M | 001, 006, 007, 063 | done |
| 5 | 065 | DriftState POD struct and Tier-1/Tier-3/variance draw helpers | engine | M | 001, 006, 007, 063 | done |
| 5 | 068 | Glide.h/.cpp — per-voice portamento slew | core | S | 001, 006, 007, 067 | done |
| 5 | 069 | KeyAssigner.h/.cpp — bit-faithful note-priority/retrigger state machine | core | M | 001, 006, 007, 067 | done |
| 5 | 070 | ControlCore pitch assembly — 6-bit integer DAC-count pitch (VINTAGE quantization) | core | S | 001, 006, 007, 067 | done |
| 5 | 082 | ModRouter: fixed LFO/ADSR modulation routing | core | S | 001, 006, 007, 081 | done |
| 5 | 083 | TriggerSource (S7): coupled note-priority + retrigger | core | S | 001, 006, 007, 081 | done |
| 5 | 084 | Arpeggiator: UP/U&D/DOWN over 32-key bitmap | core | S | 001, 006, 007, 081 | done |
| 5 | 085 | StepSequencer: 100-slot note/rest/tie record & play | core | M | 001, 006, 007, 081 | done |
| 5 | 086 | Clock: single H->L edge node, 3 sources, swing, keypress reset | core | M | 001, 006, 007, 081 | done |
| 5 | 090 | FxOversampler2x dedicated post-voice 2x halfband pair | core | M | 001, 006, 007, 036 | done |
| 5 | 092 | Chorus stage: Juno-style anti-phase BBD widener | core | M | 001, 006, 007, 088, 089 | done |
| 5 | 093 | Delay stage: tempo-synced mono-core stereo delay with damped feedback | core | M | 001, 006, 007, 088, 089 | done |
| 5 | 108 | MwAudioLookAndFeel vector drawing parameterized by DesignTokens | ui | M | 006, 106 | done |
| 6 | 020 | APVTS ParameterLayout generator (ParameterLayout.cpp) | core | S | 019, 007 | done |
| 6 | 021 | INIT patch builder (out-of-box defaults, ADR-016) | core | S | 019, 017, 007 | done |
| 6 | 025 | .mw101preset JSON projection + validator (PresetFormat.h/.cpp) | core | M | 019, 023, 022, 007 | done |
| 6 | 030 | VCO band-limited saw + variable PWM pulse | core | M | 001, 006, 007, 026, 027, 029 | done |
| 6 | 039 | LadderFilter resonance: inverting feedback + phase comp + diode clamp + self-osc + make-up Q | core | M | 001, 006, 007, 033, 034, 035, 038 | done |
| 6 | 043 | CLASS-FP two-stage comparer (scalar fingerprint + FFT/NMSE + alias floor) | qa | M | 001, 006, 007, 041 | done |
| 6 | 049c | core/calibration: prepareToPlay constant-set SELECTOR keyed by renderVersion (legacy-render path) | core | M | 005b, 033, 035, 029 | done |
| 6 | 058 | Envelope trigger state machine: GateTrig/Gate/Lfo retrigger | core | S | 006, 054 | done |
| 6 | 059 | Lfo Random S/H, Noise source, and cycleEdge flag | core | M | 007, 006, 055 | done |
| 6 | 060 | Vca anti-thump gate fade + ENV/GATE mode handling | core | S | 007, 006, 056 | done |
| 6 | 071 | ControlCore driver — control-tick advance, VINTAGE/MODERN poles, jitter, auto-engage, crossfade | core | M | 001, 006, 007, 067, 069, 070 | done |
| 6 | 087 | SequencerEngine: fixed-order tick + RT-safe snapshot swap | core | M | 001, 006, 007, 081, 082, 083, 084, 085, 086 | done |
| 6 | 091 | Drive stage: oversampled asymmetric waveshaper + tilt + DC block | core | M | 001, 006, 007, 088, 090 | done |
| 6 | 109 | Custom control subclasses (Rotary/Linear sliders, ToggleSwitch, ChoiceSelector) | ui | M | 006, 106, 108 | done |
| 7 | 024 | Load-failure recovery ladder (LoadFailure.h/.cpp) | core | M | 019, 023, 021, 022, 007 | done |
| 7 | 032 | OscillatorSection owner + per-voice HQ escalation | core | M | 001, 006, 007, 026, 027, 029, 030, 031, 028 | done |
| 7 | 042 | Stimulus and PatchSnapshot render-input types | qa | S | 001, 006, 007, 041, 020 | done |
| 7 | 044 | Provenance — honesty-label vocabulary and renderVersion governor | qa | S | 001, 006, 043 | done |
| 7 | 047 | Oversampler zone wrapper: factor selection, OS_CEILING clamp, and CI alias-floor harness | core | M | 001, 006, 007, 039, 036 | done |
| 7 | 061 | Env/LFO param de-zipper class verification (S2/S4 paired test) | qa | S | 007, 006, 020, 054, 055 | done |
| 7 | 062 | Env/LFO/VCA real-time safety and control-rate determinism suite | qa | S | 007, 006, 058, 059, 060, 057 | done |
| 7 | 066 | VintageMacro host-thread Age-to-target mapping | engine | S | 001, 006, 007, 020 | done |
| 7 | 094 | FxChain orchestration: bypass, dry-pad, mono collapse, latency | core | M | 001, 006, 007, 088, 089, 091, 092, 093 | done |
| 7 | 100 | RT-safe CC/learn map (plugin/midi/CcLearnMap.h/.cpp) | app | S | 001, 006, 020 | done |
| 7 | 102 | APVTS <-> ParamSnapshot marshalling (plugin/ParamBridge.h/.cpp) | app | M | 001, 006, 007, 020 | done |
| 7 | 105 | Constant PDC LatencyReporter (plugin/latency/LatencyReporter.h/.cpp) | app | M | 001, 006, 036, 091 | done |
| 7 | 144b | presets/ flat-POD bake loader contract — deterministic build/load-time bake, never parsed on the audio thread | core | M | 025, 040 | todo |
| 8 | 045 | bless tool — arm64-only, BLESS_REASON-gated guarded writer | qa | M | 001, 006, 047, 044 | done |
| 8 | 073 | Voice.h/.cpp — circuit-accurate signal-path assembly + drift seed | core | M | 001, 006, 007, 067, 068, 032, 047, 062 | done |
| 8 | 076 | RenderHarness — deterministic offline render | qa | M | 001, 006, 007, 042, 086 | done |
| 8 | 101 | HostEvent -> mw::core::MidiEvent translator (plugin/midi/EventTranslator.h/.cpp) | app | S | 001, 006, 007, 020, 099, 100 | done |
| 9 | 048 | FILTER golden corpus (EARLY freeze gate) — bless + compare across blessed rates | qa | M | 001, 006, 047, 043, 045 | done |
| 9 | 072 | DriftModel orchestration engine (Tier1/2/3 + smoothing + reroll) | engine | M | 001, 006, 007, 020, 063, 064, 065, 073 | done |
| 9 | 074 | VoiceManager — pool, MONO/UNISON dispatch, control-tick propagation, fixed-order render | core | M | 001, 006, 007, 067, 069, 073 | done |
| 9 | 077 | GoldenStore — blob/sidecar keying, lookup and load | qa | M | 001, 006, 041, 076 | done |
| 9 | 078 | CLASS-EXACT comparer (SHA-256 hash compare) | qa | S | 001, 006, 040, 076 | done |
| 9 | 079 | Calibration-tool self-tests — planted-answer, disjoint cal/val, negative control | qa | M | 001, 006, 007, 076 | done |
| 9 | 103 | MPE-over-MIDI reconstruction parser (plugin/midi/MpeReconstructor.h/.cpp) | app | M | 001, 006, 020, 073 | done |
| 9 | 104 | MidiFrontEnd note/gate/bend/pressure/CC translation (plugin/midi/MidiFrontEnd.h/.cpp) | app | M | 001, 006, 007, 020, 073, 099, 100, 098 | done |
| 10 | 075 | VoiceManager POLY allocator + deterministic voice stealing | core | M | 001, 006, 007, 073, 074 | done |
| 10 | 080 | Per-module CLASS-EXACT golden corpora — seq/divider/PRNG/arp/param-smooth/CC | qa | M | 001, 006, 007, 078, 045, 032, 086, 071 | done |
| 10 | 104b | Tuning + bend-range wiring: A4 440/442 duality, TUNE cents, per-channel + MPE bend ranges, optional MTS-ESP | app | M | 104, 103, 102 | done |
| 10 | 112 | CapabilityShim resolve + per-block recheck + UI publish (plugin/host/CapabilityShim.h/.cpp) | app | M | 001, 006, 098, 103, 087 | done |
| 10 | 152 | KeyAssignerReference.{h,cpp} — disassembly-semantics golden reference | qa | M | 001, 006, 007, 067, 077 | done |
| 11 | 118 | Wire all engine modules into Engine::prepare/process/reset assembly | engine | M | 006, 073, 075, 071, 091, 092, 093, 006 | done |
| 11 | 153 | KeyAssigner golden-trace conformance (K17) test battery | qa | S | 001, 006, 077, 069, 152 | done |
| 12 | 076b | CPU-budget regression golden ctest — measureWorstCaseBlockMicros HARD gate at max poly+unison @2x | qa | M | 118, 076, 046 | done |
| 12 | 119 | PresetManager in-memory bank + per-slot INIT fallback (PresetManager.h/.cpp) | core | M | 001, 006, 021, 022, 024, 025, 118 | done |
| 12 | 132 | Engine no-alloc / no-lock / noexcept hot-path guard tests | qa | S | 006, 118 | done |
| 12 | 133 | End-to-end audio smoke test (note-on to non-silent output) | qa | S | 006, 118 | done |
| 12 | 134 | Lifecycle/fuzz test: prepare/process/reset over random valid blocks and params | qa | S | 006, 118 | done |
| 12 | 135 | End-to-end determinism test (same seed + same BlockContext sequence) | qa | S | 006, 118, 077 | done |
| 12 | 144 | INIT/baseline preset + authoring conventions for the ~64-preset bank | docs | S | 118, 025 | done |
| 13 | 025b | presets_roundtrip ctest — every preset round-trips schema + checksum | qa | S | 025, 119, 040, 144b | todo |
| 13 | 111 | MwAudioProcessor shell: prepare/process/reset + block-split + setLatencySamples (plugin/PluginProcessor.h/.cpp) | app | M | 001, 006, 007, 118, 020, 119, 099, 104, 101, 102, 112, 105 | done |
| 13 | 131 | Factory preset corpus + CI registry/mirror validator | core | M | 001, 006, 025, 119 | todo |
| 13 | 143 | Legacy-render path + blessed sample-rate set integration test | qa | S | 006, 119, 036, 077, 118 | done |
| 13 | 145 | AcidBassLead category presets (squelchy resonant acid bass/lead) | docs | M | 118, 025, 144 | done |
| 13 | 146 | SubBass category presets (independent sub-osc, 303-underpinning) | docs | M | 118, 025, 144 | done |
| 13 | 147 | Lead category presets (bright saw/square leads, vibrato) | docs | M | 118, 025, 144 | done |
| 13 | 148 | PWMStrings category presets (mono PWM stylization + chorus) | docs | M | 118, 025, 144 | done |
| 13 | 149 | BlipsFX category presets (percussive blips, noise FX) | docs | M | 118, 025, 144 | done |
| 13 | 150 | SeqArpRiff category presets (stored 100-step patterns + arp settings) | docs | M | 118, 025, 144 | done |
| 14 | 113 | Single juce_add_plugin target over the shared processor (plugin/CMakeLists.txt) | infra | S | 001, 096, 111 | done |
| 14 | 114 | MwAudioEditor root: AffineTransform scaling, constrainer, resize/DPI | ui | M | 111, 020, 006, 106, 108 | done |
| 14 | 117 | ModuleBase + ModulatorModule (LFO/S&H + mod depth) with APVTS attachments | ui | M | 020, 111, 006, 106, 108, 109 | done |
| 14 | 151 | Bank coverage manifest + full-bank CI validation (~64 presets) | qa | S | 118, 025, 145, 146, 147, 148, 149, 150 | todo |
| 15 | 115 | Coalescing telemetry Timer + reduce-motion toggle in editor | ui | S | 020, 006, 107, 114 | todo |
| 15 | 116 | BackgroundLayer cached static chrome + patch lines + labels | ui | M | 006, 106, 114 | todo |
| 15 | 120 | VcoModule (range, waveform mix, PWM, pitch, sub, noise) | ui | M | 020, 111, 006, 109, 117 | done |
| 15 | 121 | SourceMixerModule (saw/pulse/sub/noise levels) | ui | S | 020, 111, 006, 109, 117 | done |
| 15 | 122 | VcfModule (cutoff, resonance, env amount, kbd track, mod) | ui | S | 020, 111, 006, 109, 117 | done |
| 15 | 123 | VcaModule (env/gate select, level, env A/D/S/R) | ui | S | 020, 111, 006, 109, 117 | done |
| 15 | 124 | ControllerStrip (glide, bend, mod-wheel routing, transpose) | ui | S | 020, 111, 006, 109, 117 | done |
| 15 | 125 | TransportModeBar (arp/seq mode, tempo-sync, run/hold, scale + reduce-motion toggles) | ui | M | 020, 111, 087, 006, 109, 117 | done |
| 15 | 128 | PresetBrowser thin view over processor PresetManager | ui | M | 119, 020, 111, 006, 106, 109, 114 | todo |
| 15 | 129 | StatusBanner (non-modal load-failure + disclaimer surface) | ui | S | 111, 119, 006, 106, 114 | todo |
| 15 | 130 | OpenGL opt-in escape hatch (OFF by default) | ui | S | 020, 006, 114 | todo |
| 15 | 136 | Wire MwAudioProcessor: engine + frontend + capability shim + latency reporter | app | M | 111, 104, 113, 020, 118 | done |
| 15 | 137 | Per-platform format resolution + configure-time validator gate (Formats.cmake) | infra | M | 001, 113 | todo |
| 15 | 138 | Locate/declare validator targets (Validators.cmake) | infra | M | 001, 113 | todo |
| 15 | 156 | GitHub Actions cross-platform build+test matrix workflow (preset dispatcher) | infra | M | 001, 006, 113, 077 | todo |
| 16 | 126 | SequencerGrid (100-step pattern editor view) | ui | M | 020, 111, 087, 006, 109, 107, 125 | todo |
| 16 | 127 | ScopeMeterOverlay (telemetry-driven, reduce-motion gated) | ui | S | 006, 106, 107, 115 | todo |
| 16 | 129b | UI signposts: 440-vs-442 tuning-duality banner + 'running unblessed at this host rate' banner | ui | S | 129, 112, 115 | todo |
| 16 | 139 | Cross-format bit-exactness test (VST3/AU/CLAP/Standalone identical DSP output) | qa | M | 113, 104, 119, 077, 136 | todo |
| 16 | 140 | Host smoke matrix: headless Standalone launch + per-format validator invocation | qa | M | 113, 001, 006, 136, 137, 138 | todo |
| 16 | 141 | Constant-PDC invariance + FX-off bit-exact integration test | qa | S | 113, 036, 091, 077, 136 | todo |
| 16 | 142 | Cross-format capability ladder integration test (note-expression + transport rungs) | qa | M | 113, 104, 087, 114, 136 | todo |
| 16 | 157 | CI verification gate steps: discovery, label-snapshot, MANIFEST, no-network | infra | M | 001, 006, 077, 156 | todo |
| 17 | 154 | Adversarial multi-dimension QA audit report (docs/QA-REPORT.md) | qa | M | 118, 006, 077, 006, 139, 140, 141, 142, 143 | todo |
| 18 | 155 | (PI) pragmatic-invention ledger sweep section in docs/QA-REPORT.md | qa | S | 118, 077, 006, 154 | todo |

| 15 | 118b | Reconcile KeyAssigner ownership — GateTrigMode via Engine | core | S | 118, 069, 071, 074 | done |

| 16 | 134b | Complete Engine::reset() to a deterministic fixed point | core | S | 118, 118b, 074, 071 | done |

| 14 | 110b | JUCE plugin skeleton bootstrap (Standalone + min processor) | app | M | 001, 096, 118 | done |

| 13 | 102b | Define core/params/ParamSnapshot.h POD (closes seam gap) | core | S | 019, 007 | done |

| 13 | 075b | Wire PolyAllocator into VoiceManager/Engine POLY path | core | S | 075, 074, 118, 069 | done |

| 13 | 023b | Persist CC-learn bindings in plugin state | app | S | 023, 100, 017 | todo |

## Notes (standing rationale ledger — why the DAG is shaped this way)

- **Waves 0–3 = infra bootstrap.** `001` (top-level CMake) is the universal root; presets/CPM/
  compiler-flags fan out at wave 1; the `mwcore` lib at wave 2; the test harness + `core/BlockContext.h`
  (core-types) + the single `core/calibration/Calibration.h` (PI) table at wave 3. The core-types↔
  param-schema cycle is broken: `core-types` depends only on the mwcore lib, never on the param schema.
- **Waves 4–7 run all five core DSP streams + fx + params + leaf golden/plugin/ui pieces fully
  decoupled** — each only needs build-skeleton + test-harness + core-types + its own leaf modules.
  This is the maximum-parallelism band (~30 tasks/wave).
- **The FILTER golden corpus is an EARLY freeze gate** — re-pathed to a filter-only render so it
  depends ONLY on the filter module + the CLASS-FP comparer + the bless tool, NOT the full engine.
- **`integration` (full-engine) is the single late convergence node**; the rest of the golden infra
  that needs the assembled engine sits after it.
- **plugin & UI streams converge on the AudioProcessor shell + param schema**; format wrappers and
  the cross-format suite follow.
- **QA is near-last and explicitly sequenced AFTER the integration cross-format suite.**
- **CI is intentionally LAST** (owner-locked). CI mirrors local presets 1:1; macOS+Linux hard-gate,
  Windows goal.
- **Never-CI / local-only gates:** per-format host smoke + any payware cross-check are local/QA-phase
  gates, never CI steps (mark in the relevant task's Out-of-scope).
- Waves were recomputed by topological longest-path layering after breaking 3 bad integrator edges
  (Voice↔DriftModel cycle; SVG-assets←BackgroundLayer and CapabilityShim←editor-telemetry reversed).
- **072 warm-up follow-up:** DriftModel's warm-up chassis is per-voice; ADR-009 D6/§5.3 require it GLOBAL. Dormant (warm-up OFF by default, §13 open gap) — reconcile when the warm-up path is exercised.
- **Calibration-model ownership (079 MEDIUM):** design 11 §12 says core/calibration owns a Calibrator/synthesize model, but no task delivered it; 079 authored a JUCE-free stand-in for the self-tests. With the no-physical-oracle decision (1.7) there is no real fitting to do, so the stand-in is acceptable harness infra — revisit only if a measured oracle is ever introduced.
