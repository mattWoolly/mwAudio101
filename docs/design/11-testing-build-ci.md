<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Testing, Golden Harness, Build & Toolchain

This document is the single source of truth for the mwAudio101 test
architecture, the golden/regression harness, the calibration self-tests, the
cross-cutting `ctest` invariants, and the CMake/CPM build + toolchain. Backlog
tasks cite this doc by section number (e.g. "implement per docs/design/11 §5.3").

It is the design realization of three normative contracts: ADR-013 (testing /
golden / calibration harness), ADR-014 (build system / dependency management /
toolchain), and ADR-023 (engine versioning, bless communication, blessed
sample-rate set). The cited research ground truth is
docs/research/13-validation-gaps-and-disputes.md (the honesty ledger). Where a
value is an engineering invention not present in research it is tagged **(PI)**
(pragmatic invention) and centralizes in the single calibration table
`core/calibration/Calibration.h` (created by the backlog; owned by the
calibration/variance design doc — this doc REFERENCES it, never re-mints it).

Parameter IDs, ranges, units, defaults and skews are owned by the parameter
schema design doc (docs/design/06 §2) per ADR-008; this doc REFERENCES them and
never re-defines them. Engine DSP constants (`tanh` approximation, decimator
coefficients, per-SR compensation table, `fc <= 0.45*fs_os` guard) are owned by
the filter/oversampling design docs and ADR-003; this doc treats them as opaque
versioned frozen constants selected by `renderVersion`.

## 1. Scope and non-goals

### 1.1 In scope

- The single `ctest` graph: Layer 1 (Catch2 silent-pass-hardened units), Layer 2
  (two-stage golden comparer, corpus partitioned by determinism class), Layer 3
  (guarded `bless` + provenance `MANIFEST.toml`), Layer 4 (calibration-tool
  self-tests).
- Cross-cutting invariant gates: license headers, no-audio-thread alloc/lock
  (`AudioThreadGuard`), FP-discipline flag gate, CPU-budget regression golden,
  label-snapshot diff, per-prefix discovery assertions.
- `renderVersion` / `engineVersion` governance hooks in the bless tool and CI
  (ADR-023).
- The blessed sample-rate set and its corpus keying (ADR-023).
- CMake structure, `CMakePresets.json`, CPM-pinned dependencies,
  `mw_fp_discipline` INTERFACE target, sanitizer presets, and the local==CI
  command map in `docs/BUILDING.md`.

### 1.2 Non-goals (owned elsewhere — reference, do not redefine)

- Parameter IDs / ranges / defaults / skews — docs/design/06 §2 (ADR-008).
- The DSP engine internals (Huovilainen ladder, PolyBLEP, oversampler,
  decimator, per-SR compensation table, `tanh` constants) — ADR-003 / filter +
  oversampling design docs. This doc only renders, hashes, and compares their
  output and pins their FP build flags.
- The variance/drift/k-mapping/tempco physics being calibrated — the
  calibration/variance design doc and `core/calibration/Calibration.h`. This doc
  only specifies the calibrator's SELF-TESTS, not its model.
- State/preset schema, `schemaVersion`, migration chain — ADR-008 /
  docs/design/06. This doc consumes `renderVersion` (ADR-023) which sits beside
  `schemaVersion` but is orthogonal to it.

### 1.3 The one truth this harness must encode

Because mwAudio101 holds no physical SH-101 and takes no bench measurements as
the oracle [research/13 §1.1, §5], a golden file can only prove "the DSP still
does what it did when blessed" — it can NEVER prove "this matches a real
SH-101." Every test in this project validates **self-consistency and
topology-faithfulness, NOT measured fidelity** [ADR-013 Consequences;
research/13 §5.6]. The provenance MANIFEST (§7) is the single control that keeps
a guess from silently hardening into a "regression-protected fact." No test in
this project may assert measured SH-101 fidelity [ADR-013 owner ratification].

## 2. Repository tree and module responsibilities

The CMake structure realizes the ADR-001/ADR-014 tree split. Files the backlog
creates for THIS doc's subsystem are listed; DSP/core files owned by other docs
are shown only where the harness depends on them.

### 2.1 Directory layout

```text
CMakeLists.txt                      top-level project, options, subdirs
CMakePresets.json                   schema v6 — sole build entrypoint (§9)
cmake/
  CPM.cmake                         committed CPM bootstrap, CPM_DOWNLOAD_HASH-checked
  Dependencies.cmake                single full-SHA pin manifest (§10)
  CompilerFlags.cmake               mw_fp_discipline INTERFACE target (§11)
core/                               pure C++20 static lib mwcore, NO JUCE
  dsp/                              engine (other docs); harness renders from it
  calibration/Calibration.h         single (PI) constant table (other doc)
plugin/                             JUCE shell mwplugin (links mwcore)
ui/                                 editor (links mwplugin)
presets/                            ~64 patches as data + deterministic loader
tests/
  CMakeLists.txt                    Catch2 console binary; ctest registration (§8)
  unit/                             Layer-1 unit tests, name-prefixed (§4)
  golden/
    Comparer.h/.cpp                 two-stage comparer (§6)
    RenderHarness.h/.cpp            deterministic offline render (§5)
    GoldenStore.h/.cpp              corpus load/key/lookup (§5.4)
    Manifest.h/.cpp                 MANIFEST.toml read/validate (§7)
    corpus/                         versioned golden blobs + sidecar JSON
      MANIFEST.toml                 provenance ledger (§7)
      ctest-labels.snapshot         checked-in label snapshot (§8.4)
  cal/CalibrationSelfTests.cpp      Layer-4 planted/disjoint/negative (§12)
  invariants/
    AudioThreadGuard.h/.cpp         alloc/lock sentinel (§13)
    LicenseHeaderCheck.cpp          SPDX scan (§13.2)
    FpDisciplineCheck.cpp           runtime FP-flag assertion (§13.4)
    CpuBudget.cpp                   worst-case wall-time golden (§13.5)
tools/
  bless/bless.cpp                   guarded bless tool (§7.2), arm64-only
  bless/Provenance.h/.cpp           honesty-label + renderVersion governance
scripts/
  check.sh                          host one-command configure→build→test (§9.4)
docs/BUILDING.md                    local↔CI 2-column command map (§9.4)
```

### 2.2 Module responsibilities

| Module | Responsibility | RT-relevant? |
| --- | --- | --- |
| `RenderHarness` | Deterministic offline render of a patch+stimulus to an f32 buffer; pins SR, block size, seed, engine tag, `renderVersion` | No (offline) |
| `GoldenStore` | Key, locate, load golden blobs + sidecars by the §5.4 key | No (offline) |
| `Comparer` | Two-stage compare (Stage 1 scalar fingerprint, Stage 2 FFT/NMSE + alias floor) per determinism class | No (offline) |
| `Manifest` | Parse/validate `MANIFEST.toml`; completeness + orphan + honesty-label + renderVersion checks | No (CI) |
| `bless` | Arm64-only, `BLESS_REASON`-gated golden writer + MANIFEST appender + `renderVersion` governor | No (offline, message-thread) |
| `AudioThreadGuard` | `processBlock`-scope alloc/lock sentinel | Audio-thread observer |
| `CalibrationSelfTests` | Planted-answer / disjoint cal-val / negative-control | No (offline) |

## 3. The `ctest` graph (overview)

A single `ctest` graph with four layers plus cross-cutting invariant gates
[ADR-013 Decision]. Every test is registered via `catch_discover_tests` (Catch2)
or `add_test` (tool/invariant tests) under the silent-pass rules of §4.

### 3.1 Labels and name-prefix selectors

Every test carries a subsystem tag and a name-prefix selector [ADR-013 Layer 1].

| Layer | Name-prefix selector | Subsystem tags (`-L`) |
| --- | --- | --- |
| Layer 1 units | `mw101.unit.*` | `[vco] [vcf] [vca] [env] [seq] [prng] [arp] [rt] [cal]` |
| Layer 2 golden | `mw101.golden.*` | `[golden] [class-exact] [class-fp]` |
| Layer 3 bless/manifest | `mw101.manifest.*` | `[manifest] [provenance]` |
| Layer 4 calibration | `mw101.cal.*` | `[cal]` |
| Cross-cutting invariants | `mw101.inv.*` | `[license] [rt] [fp] [cpu]` |

CI asserts each required prefix (`vco`, `vcf`, `vca`, `env`, `seq`, `prng`,
`arp`, `cal`) has >= 1 discovered test [ADR-013 C2]; a deleted/renamed suite is
caught by the label-snapshot diff (§8.4) [ADR-013 C3].

### 3.2 Real-time invariants (statement)

The harness itself is offline and not RT-constrained. The properties it
verifies about the engine are: no heap allocation and no lock on the audio
thread (`processBlock`) outside the documented one-time warm-up carve-out
[ADR-013 C19; ADR-001]; all per-voice PRNG state, oversampling buffers,
Newton-iteration scratch, and `tanh`/compensation tables pre-allocated at
`prepareToPlay` [research/10 §3.7, §5.1]; `renderVersion` read/write,
constant-set selection, table regen and the opt-in flag run on the message
thread / at `prepareToPlay` only [ADR-023 V18]. These are enforced by §13.1 and
§13.3.

## 4. Layer 1 — Catch2 units, silent-pass-hardened

### 4.1 Silent-pass prevention

- Register every test executable with `catch_discover_tests` and run it under
  `--no-tests=error`; an empty / mis-linked / mis-filtered binary FAILS instead
  of passing green [ADR-013 Layer 1, C1; ADR-014 C7]. The `ctest`
  `FAIL_REGULAR_EXPRESSION` is set on `"No tests ran"` as a belt-and-braces
  backstop.
- `testPresets` carry `--output-on-failure` and `--no-tests=error` [ADR-014 C7].
- A clang-query/grep lint bans bare `REQUIRE(true)` and empty-body `TEST_CASE`
  bodies; lint FAILS on a hit [ADR-013 Layer 1, C4].

### 4.2 Paired positive/negative property asserts (mandatory)

Every numeric DSP assert MUST be paired with a negative/property control so a
stubbed-to-constant implementation fails [ADR-013 Layer 1, C4]. Concretely, the
canonical example: the ladder self-oscillates at `k = 4` but NOT at `k = 3.9`
[research/10 §3.4; research/13 §5.6 — note `k=4` is a dimensionless
normalized-model threshold, NOT the SH-101's physical resonance-pot value].

```cpp
// tests/unit/VcfSelfOscTest.cpp  (illustrative shape)
TEST_CASE("mw101.unit.vcf self-oscillates at k>=4", "[vcf]") {
    REQUIRE(rmsAfterSettle(renderLadder(/*k=*/4.0f)) > kSelfOscRmsFloor);   // positive
    REQUIRE(rmsAfterSettle(renderLadder(/*k=*/3.9f)) < kSelfOscRmsFloor);   // negative control
}
```

`kSelfOscRmsFloor` is a test fixture constant (PI), declared in the test
translation unit; if it ever needs to be shared by the engine it migrates to
`core/calibration/Calibration.h` rather than being duplicated.

### 4.3 Signatures (test support)

```cpp
namespace mw::test {
// Renders n samples of a single isolated subsystem for a unit assert.
struct UnitRenderSpec {
    double   sampleRate;     // one of the blessed set (§5.2) for unit comparability
    int      blockSize;
    uint64_t seed;
    int      numSamples;
};
std::vector<float> renderLadder(float k, const UnitRenderSpec& = kDefaultUnitSpec);
float rmsAfterSettle(const std::vector<float>& x, int settleSamples = 4096) noexcept;
}
```

## 5. Layer 2 — Render harness and golden corpus

### 5.1 Determinism-class partition (the spine)

The corpus is partitioned by **determinism class**, with platform behavior a
property of the class [ADR-013 Decision; resolution of the split]:

- **CLASS-EXACT** — integer or integer-derived paths. SHA-256 hash-compared.
  MUST be IDENTICAL on macOS arm64 AND Linux x64; any diff FAILS [ADR-013 C5].
  Members: phase-accumulator wrap; 4013 divider diode-OR sub-osc edge-sample
  indices [research/10 §7; research/13 §4.3]; sequencer per-step byte layout and
  note-priority decode [research/13 §2.9, §4.6]; fixed-seed integer PRNG stream
  [research/10 §6 — prefer 64-bit LCG/PCG; xorshift only with the §9.1
  contested-endorsement caveat]; arp step ordering; parameter-smoothing block
  boundaries; CC/automation param mapping (IDs per docs/design/06 §2).
- **CLASS-FP** — the nonlinear audio path: TPT/Huovilainen `tanh` ladder,
  PolyBLEP residuals, oversampled VCA, Drive [research/10 §3.2, §3.6, §4]. Two-
  stage compare (§6). Bit-exact on arm64 (the bless platform); FP-tolerance-
  banded on Linux x64 (hard gate) and Windows x64 (goal). Tolerance is
  **per-corpus in the manifest**, never a global `#define` [ADR-013 C6–C8].

Correctly classifying every new golden as CLASS-EXACT or CLASS-FP at authoring
time is mandatory: a mis-classified FP path placed in CLASS-EXACT is permanently
red on Linux [ADR-013 Consequences].

### 5.2 Blessed sample-rate set

Golden corpora (both classes) are generated at each of the blessed set
`{44100, 48000, 88200, 96000}` Hz, each keyed by sample rate [ADR-023 V12; B3].
These are the two base production rates and their 2x relatives implied by the
mandatory 2x oversampling [ADR-003 §F-09]. The per-SR compensation table is
regenerated for each at `prepareToPlay` [ADR-003 §F-11].

Behavior above the set (e.g. 176.4/192 kHz): supported but unblessed, validated
under the FP-tolerance tier ONLY, never asserted bit-exact, never blessed
[ADR-023 V14]. When 2x oversampling would push the internal rate above
`OS_CEILING_HZ = 192000` Hz (2x the top blessed rate), oversampling is clamped
to 1x and the `fc <= 0.45*fs_os` guard MUST still hold [ADR-023 V15; ADR-003
§F-08]. A clamped or unblessed-rate render records the clamp/rate in the
engine-tag/MANIFEST provenance and surfaces "running unblessed at this host
rate" in the UI [ADR-023 V16]. Rates below 44.1 kHz are out of scope — resampled
by the host, neither blessed nor clamped [ADR-023 V17].

### 5.3 Engine-tag and renderVersion keying

Goldens are engine-tagged so the open ADR-003 (ladder engine) and ADR-004
(decimator / oversampling) A/Bs run on identical stimuli without
cross-contaminating blesses [ADR-013 Layer 2; research/10 §9.4]. The engine tag
MUST include `renderVersion` [ADR-023 V11]. A golden compared across a different
engine tag, oversample factor, or `renderVersion` than it was blessed under is
REFUSED [ADR-013 C22; ADR-023 V11].

```cpp
namespace mw::golden {
enum class LadderEngine { ZDF, Huovilainen };          // ADR-003 open A/B
enum class DeterminismClass { Exact, Fp };

struct EngineTag {
    LadderEngine ladder;
    int          oversampleFactor;   // 1 or 2 (ADR-003 §F-09; clamp ADR-023 V15)
    int          renderVersion;      // ADR-023 V11 — bless-affecting contract
};

struct GoldenKey {
    uint64_t        renderGraphHash; // SHA-256-derived hash of the render graph
    EngineTag       engine;
    double          sampleRate;      // must be in the blessed set (§5.2)
    int             blockSize;
    uint64_t        seed;
    DeterminismClass cls;
};
uint64_t hash(const GoldenKey&) noexcept;
bool sameEngineContext(const EngineTag& a, const EngineTag& b) noexcept; // refuse if false
}
```

### 5.4 Render harness signatures

```cpp
namespace mw::golden {
struct RenderResult {
    std::vector<float> samples;   // mono f32 (or interleaved if stereo path)
    double             sampleRate;
    EngineTag          engine;
};

class RenderHarness {
public:
    // Deterministic: identical (patch, stimulus, key) -> identical bytes on the
    // same platform; CLASS-EXACT identical on arm64 AND Linux.
    RenderResult render(const PatchSnapshot& patch,
                        const Stimulus&      stim,
                        const GoldenKey&     key) const;
};

// Golden blobs: raw f32 / WAV + sidecar JSON keyed by GoldenKey (§7.1).
class GoldenStore {
public:
    bool                 has(const GoldenKey&) const;
    RenderResult         load(const GoldenKey&) const;        // throws if absent
    std::filesystem::path blobPath(const GoldenKey&) const;
    std::filesystem::path sidecarPath(const GoldenKey&) const;
};
}
```

Goldens are stored as binary blobs in the repo (raw f32/WAV + sidecar JSON);
they grow history weight, so Git LFS or a render-on-demand-from-seed strategy is
required to bound clone size [ADR-013 Consequences; ADR-023 Consequences — the
rate axis multiplies the footprint by four].

## 6. Layer 2 — Two-stage comparer

### 6.1 Stage gating

Stage 1 is a cheap scalar fingerprint; Stage 2 (full vector + FFT/NMSE + alias
floor) runs ONLY on a Stage-1 flag or when `--full` is passed [ADR-013 C9]. This
keeps the common-case compare fast while preserving spectral rigor on
divergence.

### 6.2 CLASS-EXACT compare

SHA-256 over the canonical byte serialization of the integer/control output;
equality is bit-for-bit on arm64 AND Linux x64 [ADR-013 C5].

```cpp
namespace mw::golden {
struct Sha256 { std::array<uint8_t, 32> bytes; bool operator==(const Sha256&) const; };
Sha256 sha256(std::span<const std::byte>) noexcept;

struct ExactResult { bool match; Sha256 got; Sha256 expected; };
ExactResult compareExact(const RenderResult& got, const RenderResult& blessed);
}
```

### 6.3 CLASS-FP compare

```cpp
namespace mw::golden {
struct Stage1Fingerprint {        // cheap scalar pass
    double rms;
    double peak;
    double maxAbsErr;             // vs blessed, sample-aligned
    double envelopeErr;           // windowed envelope max error
};

struct Stage2Metrics {            // only on Stage-1 flag or --full
    double nmseDb;                // windowed-FFT NMSE in dB
    double aliasFloorDb;          // energy above the perceptual alias limit
};

struct FpTolerance {              // per-corpus, FROM the manifest (§7) — never a global #define
    double maxAbsErr;             // arm64 = 0 (bit-exact); Linux/Windows = manifest band
    double rmsErr;
    double nmseDbCeiling;
    double aliasFloorDbCeiling;
};

struct FpResult { bool pass; bool ranStage2; Stage1Fingerprint s1; std::optional<Stage2Metrics> s2; };

// arm64: tol.maxAbsErr == 0 => bit-exact gate. Linux/Windows: banded.
FpResult compareFp(const RenderResult& got, const RenderResult& blessed,
                   const FpTolerance& tol, bool full = false);
}
```

- Compare tier is bit-exact on arm64 (`maxAbsErr == 0`, the bless platform)
  [ADR-013 C6; ADR-023 V13]; FP-tolerance-banded on Linux x64 (hard gate, outside
  band FAILS) [ADR-013 C7] and Windows x64 (same tier, reported, goal-gated)
  [ADR-013 C8; ADR-014 C12].
- The alias floor is asserted below the perceptual limit (~2135 Hz for NI=2 /
  ~7.8 kHz B-spline) [research/10 §8].
- A naive bit-exact gate on Linux would emit constant false regressions because
  the DSP stack is wall-to-wall transcendentals (`tanh`, `tan`, `exp`) whose
  last-ULP results, SIMD reduction order and FMA contraction differ between
  arm64 and x64 libm [research/10 §3.2, §3.6, §4; ADR-013 Context] — hence the
  CLASS-FP band, never a single cross-platform bit-exact gate.

### 6.4 Numeric defaults

The Linux/Windows tolerance band defaults derive from the design-spec
documented noise floor; the project floor is `max abs <= 1e-6` [ADR-014 Context,
C-design-§5]. This is the DEFAULT seed value only — the operative tolerance for
each corpus lives per-corpus in the manifest (§7) and may be tightened or
loosened there with a `BLESS_REASON`.

| Metric | arm64 (bless) | Linux/Windows default | Owner |
| --- | --- | --- | --- |
| `maxAbsErr` | 0 (bit-exact) | `1e-6` (PI seed) | manifest per-corpus |
| `rmsErr` | 0 | `1e-7` (PI seed) | manifest per-corpus |
| `nmseDbCeiling` | n/a (exact) | `-120 dB` (PI seed) | manifest per-corpus |
| `aliasFloorDbCeiling` | per §8 alias limit | per §8 alias limit | manifest per-corpus |

The `1e-6` / `1e-7` / `-120 dB` seeds are **(PI)** starting bands subject to
per-corpus re-derivation; they are NOT global compile constants and live in the
manifest the moment a corpus is blessed. Too tight => Linux flaps red on
legitimate libm differences; too loose => real small regressions slip through
[ADR-013 Consequences].

## 7. Layer 3 — Guarded bless and provenance MANIFEST

### 7.1 Golden artifact + sidecar

Each golden is a binary blob plus a sidecar JSON recording its `GoldenKey` and a
human-readable render-graph description. The authoritative provenance ledger is
`tests/golden/corpus/MANIFEST.toml`.

### 7.2 The bless tool

`bless` is a separate tool, NEVER a test side-effect, and runs ONLY on macOS
arm64 (Linux/Windows blesses are REFUSED) [ADR-013 Layer 3, C10]. It refuses
unless `BLESS_REASON` is set non-empty [ADR-013 C11].

```cpp
namespace mw::bless {
struct BlessRequest {
    GoldenKey                key;
    std::string              blessReason;     // env BLESS_REASON, must be non-empty
    std::vector<HonestyLabel> honestyLabels;  // ledger §2-§8 provenance (§7.4)
    std::optional<FpTolerance> tolerance;     // required for CLASS-FP
};

enum class BlessRefusal { NotArm64, EmptyReason, MissingTolerance, MissingHonestyLabel };

// Returns the appended MANIFEST entry, or a refusal reason. Pure tool, off audio thread.
std::expected<ManifestEntry, BlessRefusal> bless(const BlessRequest&);
}
```

### 7.3 MANIFEST entry contents

Each blessed artifact appends to `MANIFEST.toml` recording [ADR-013 Layer 3]:

| Field | Meaning |
| --- | --- |
| `artifactSha256` | SHA-256 of the golden blob |
| `blesser` | blesser identity |
| `isoDate` | ISO-8601 bless date |
| `commitSha` | repository commit SHA at bless time |
| `blessReason` | non-empty `BLESS_REASON` |
| `engine` | `{ZDF\|Huov}` ladder tag |
| `oversampleFactor` | 1 or 2 (clamp recorded per ADR-023 V16) |
| `sampleRate` | one of the blessed set (§5.2) |
| `seed` | fixed render seed |
| `blockSize` | render block size |
| `corpusClass` | `EXACT` or `FP` |
| `tolerance` | per-corpus band (CLASS-FP only) |
| `compiler` | compiler + version |
| `fpFlagProof` | `-ffast-math` off, `-ffp-contract=off` proof (§13.4) |
| `arm64HostId` | reference host identifier |
| `renderVersion` | the `renderVersion` this artifact was blessed under (ADR-023) |
| `honestyLabels` | ledger §2-§8 provenance labels (§7.4) |

### 7.4 Honesty-label provenance binding

Every blessed artifact whose claim derives from a ledger §2-§8 labelled fact
MUST carry that label in its MANIFEST entry [ADR-013 C14]. The controlled
vocabulary is the ledger's [research/13 §1.2]:

| Label | Example artifact claim |
| --- | --- |
| `clone-derived` | self-osc amplitude: AMSynths AM8101 @ ±12V, NOT measured [research/13 §4.1] |
| `reverse-engineered` | BA662 VCA internals: Open Music Labs chip probing [research/13 §4.2] |
| `theory/inference` | ADSR curve law: topology inference, unmeasured [research/13 §5.1] |
| `community-disassembly` | sequencer byte format: joebritt, partly inferred [research/13 §4.6] |
| `service-manual` | gate ON = 12V per 1982 Service Notes [research/13 §2.2] |
| `disputed` | gate ON 10V vs 12V; ship as range [research/13 §3.1] |
| `software-emulation-artifact` | no sine LFO / no 32'-64' on 1982 hardware [research/13 §7] |

```cpp
namespace mw::bless {
enum class LabelKind { CloneDerived, ReverseEngineered, TheoryInference,
                       CommunityDisassembly, ServiceManual, Disputed,
                       SoftwareEmulationArtifact };
struct HonestyLabel { LabelKind kind; std::string ledgerRef; /* e.g. "research/13 §4.1" */ };
}
```

### 7.5 CI completeness, orphan, and renderVersion checks

- A golden file whose hash is absent from `MANIFEST.toml` => CI FAILS [ADR-013
  C12].
- A `MANIFEST.toml` entry with no corresponding test => CI FAILS [ADR-013 C13].
- A blessed artifact whose claim derives from a ledger §2-§8 fact lacks its
  label => CI FAILS [ADR-013 C14].
- Blessed artifacts changed without a `renderVersion` bump => CI FAILS [ADR-023
  V7]. `renderVersion` bumped with no corresponding MANIFEST/artifact change =>
  CI FAILS [ADR-023 V7].

### 7.6 renderVersion / engineVersion governance

`renderVersion` increments **iff** a bless changes any CLASS-EXACT artifact hash
or moves a CLASS-FP artifact outside its manifest tolerance band [ADR-023 V5];
the bless tool is where it is governed and recorded next to the affected
artifacts and `BLESS_REASON` [ADR-023 V6]. `renderVersion` is orthogonal to
`schemaVersion` (a pure DSP re-tune bumps `renderVersion` with `schemaVersion`
unchanged; a DSP-only change MUST NOT add a no-op migration step) [ADR-023 V4].
`engineVersion` is a `MAJOR.MINOR.PATCH` human-facing string: MAJOR = intentional
sonic redesign, MINOR = audio-altering change that bumps `renderVersion`, PATCH =
proven not to alter any blessed artifact [ADR-023 V2]. Legacy reproducibility:
a pinned old `renderVersion` reproduces by selecting the frozen constant-set
tagged with that `renderVersion` at `prepareToPlay`, never at audio rate
[ADR-023 V10; ADR-003 §F-11, §F-14]. The state-side fields, opt-in flag and
release-notes wiring are owned by ADR-023 / docs/design/06; this doc only
governs the bless/MANIFEST/CI side.

## 8. ctest registration, discovery and label snapshot

### 8.1 Discovery under no-tests=error

```cmake
include(Catch)
catch_discover_tests(mw101_tests
  TEST_PREFIX "mw101."
  PROPERTIES FAIL_REGULAR_EXPRESSION "No tests ran")
# testPresets add: --no-tests=error --output-on-failure   (§9, ADR-014 C7)
```

### 8.2 Per-prefix discovery assertion

A CI step enumerates discovered tests and asserts each required prefix has >= 1
test [ADR-013 C2]:

```text
required prefixes: vco vcf vca env seq prng arp cal
=> ctest FAILS if any prefix has 0 discovered tests
```

### 8.3 Selector hygiene

Any `ctest -R`/`-L` selector carries `--no-tests=error`, and test names begin
with the selector word so a typo'd selector cannot silently match nothing
[ADR-014 C7].

### 8.4 Label-snapshot diff

A checked-in `tests/golden/corpus/ctest-labels.snapshot` (from
`ctest --print-labels`) is diffed in CI; a deleted/renamed suite shows as a
failing diff, not silence [ADR-013 C3]. Updating the snapshot is a reviewed diff.

## 9. Build — CMake structure and presets

### 9.1 Tree / CMake structure

Realizes ADR-001 / ADR-014 [ADR-014 Decision]:

- Top-level `CMakeLists.txt`: `project(mwAudio101 CXX)`, C++20,
  `CMAKE_CXX_EXTENSIONS OFF`, GPL SPDX headers, options `MW101_TESTS`,
  `MW_BUILD_CLAP`, `MW_BUILD_LV2`, sanitizer toggles wired through presets (not
  bare cache edits).
- `add_subdirectory(core)` => `mwcore`, pure C++20 static lib with ZERO JUCE
  include/link dependency (ADR-001 C1 guard, §13.6); links `mw_fp_discipline`.
- `add_subdirectory(plugin)` => `mwplugin` via `juce_add_plugin`, links
  `mwcore`; links `mw_fp_discipline` on DSP-bearing TUs.
- `add_subdirectory(ui)` => editor, links `mwplugin`.
- `add_subdirectory(tests)` => Catch2 console binary linking `mwcore` only
  (gated on `-DMW101_TESTS=ON`); links `mw_fp_discipline` so golden-compare math
  matches the shipped core exactly.
- `add_subdirectory(presets)` => deterministic loader baking the ~64 patches into
  a flat POD table at build/load time, never parsing on the audio thread
  (ADR-001 C3/C4); a `presets_roundtrip` ctest round-trips every patch (schema +
  checksum) [ADR-014 C9].
- Helpers: `cmake/CPM.cmake`, `cmake/Dependencies.cmake`,
  `cmake/CompilerFlags.cmake`.

CMake floor is `>= 3.25` (schema v6); configure FAILS with a clear floor message
below it [ADR-014 C3]; the floor is documented in `docs/BUILDING.md`.

### 9.2 Presets

`CMakePresets.json` schema v6 is the SOLE build entrypoint [ADR-014 C1].
Hidden base presets + inheritance avoid duplication [ADR-014 C8].

| `configurePresets` | Purpose |
| --- | --- |
| `default` | RelWithDebInfo, FP-disciplined |
| `debug` | Debug |
| `release` | Release |
| `asan` | AddressSanitizer |
| `ubsan` | UndefinedBehaviorSanitizer |
| `tsan` | ThreadSanitizer |
| `macos-arm64` | inherits base; toolchain/generator + format scope |
| `linux-x64` | inherits base; toolchain/generator + format scope |
| `windows-x64` | inherits base; toolchain/generator + format scope |

`buildPresets` and `testPresets` share the SAME names 1:1 [ADR-014 C8].
`testPresets` carry `--output-on-failure` and `--no-tests=error` [ADR-014 C7].
`ci-*` presets inherit a base and ONLY add toolchain/generator — no build/test
logic in CI YAML [ADR-014 Decision].

### 9.3 Per-platform format scoping

No format is built where no validator is wired [ADR-014 C6]:

| Platform | Formats built | Gate |
| --- | --- | --- |
| macOS arm64 | VST3 + AU + CLAP + Standalone | reference/bless, bit-exact hard gate |
| Linux x64 | VST3 + CLAP + Standalone (+ LV2 goal) | co-required hard gate |
| Windows x64 | VST3 + CLAP + Standalone | goal (`continue-on-error: true`) [ADR-014 C12] |

CLAP is gated behind `MW_BUILD_CLAP`; LV2 behind `MW_BUILD_LV2` (goal).

### 9.4 local==CI mapping

`docs/BUILDING.md` is the contract: a 2-column table mapping each dev command to
the identical CI step [ADR-014 C1; Decision]. The developer runs
`cmake --preset linux-x64 && cmake --build --preset linux-x64 &&
ctest --preset linux-x64` (or `scripts/check.sh` for the host); CI runs the SAME
lines / the SAME `scripts/check.sh`. The CI workflow is a dumb dispatcher of
preset names — NO build/test logic lives only in CI YAML [ADR-014 Decision].
Windows runs `continue-on-error: true` [ADR-014 C12].

```text
# docs/BUILDING.md (shape)
| Dev command                                   | CI step                       |
| cmake --preset linux-x64                       | configure (linux-x64)         |
| cmake --build --preset linux-x64               | build (linux-x64)             |
| ctest --preset linux-x64                       | test (linux-x64)              |
| scripts/check.sh                               | host gate (all platforms)     |
```

## 10. Dependency management (CPM, full-SHA pins)

CPM.cmake (a thin, auditable FetchContent wrapper) is the SINGLE vendoring
mechanism — no submodules, no vcpkg/Conan [ADR-014 Decision, forecloses]. Every
dependency is pinned in `cmake/Dependencies.cmake` to a FULL 40-char commit SHA —
never a branch, floating tag, or `main` [ADR-014 C2]; each pin carries an inline
comment recording version + SHA + date + why + SPDX license, doubling as the
GPLv3 license-provenance audit trail [ADR-014 Decision; research/12 §2, §7.3].

| Dependency | Pin | Gate | SPDX |
| --- | --- | --- | --- |
| JUCE | known-good 8.0.x release commit SHA (not "8.x") | `CPMAddPackage(GIT_TAG <40-char-SHA>)` | GPL-3.0 |
| Catch2 | vendored v3.x release SHA (not system) | bit-reproducible test results | BSL-1.0 |
| clap-juce-extensions | SHA pin (no semver releases exist) | gated behind `MW_BUILD_CLAP` | (project) |
| CLAP | transitively SHA-pinned | via clap-juce-extensions | MIT |

- `CPM_SOURCE_CACHE` (shared local + CI checkout dir) + an offline
  `FETCHCONTENT_FULLY_DISCONNECTED` path make builds reproducible and network-
  light; a no-network build MUST succeed [ADR-014 C10].
- `cmake/CPM.cmake` bootstrap is integrity-checked via `CPM_DOWNLOAD_HASH`
  [ADR-014 C2].
- A dependency bump is one reviewed PR re-pinning SHA + hash + date together
  [ADR-014 Consequences].

## 11. FP discipline (the `mw_fp_discipline` INTERFACE target)

`cmake/CompilerFlags.cmake` defines ONE INTERFACE target `mw_fp_discipline`,
linked by `mwcore`, the DSP-bearing `plugin` TUs, and the golden tests so
golden-compare math matches the shipped core exactly [ADR-014 Decision, C4]. The
flags are FROZEN (design spec §5) — this doc enforces, never relaxes.

| Toolchain | Flags |
| --- | --- |
| GCC / Clang / AppleClang | `-fno-fast-math -ffp-contract=off -fno-finite-math-only -fno-associative-math -fno-reciprocal-math -fexcess-precision=standard -fdenormal-fp-math=ieee`; NEVER `-ffast-math` / `-Ofast` |
| MSVC | `/fp:precise /fp:contract-`; NEVER `/fp:fast` |

Runtime FTZ/DAZ flush is set in `process` per ADR-001 C11, so denormal flushing
is never left to a build flag that could silently change golden output [ADR-014
Decision]. Without `-ffp-contract=off` / fast-math off, CLASS-EXACT is a lie and
CLASS-FP tolerances drift silently between toolchains [ADR-013 cross-cutting].
Pinning these slightly raises per-sample cost vs an FMA-fused build but is
mandatory for determinism and verified, not assumed [ADR-013 Consequences].

## 12. Layer 4 — Calibration-tool self-tests

The calibration harness (variance/drift fit, k-mapping, tempco — physics owned
by the calibration design doc and `core/calibration/Calibration.h`) is tested
offline, off the audio thread, with three fixture kinds [ADR-013 Layer 4]. Since
there is no physical oracle [research/13 §1.1, §5.4, §6.1], planted answers are
the only oracle we can manufacture.

```cpp
namespace mw::cal::test {
struct PlantedFixture {
    CalibrationParams knownParams;     // params used to synthesize the signal
    std::vector<float> signal;         // synthesized from knownParams
    uint64_t           seed;
};
struct CalValSplit {
    std::vector<PlantedFixture> fitSet;   // set A — used to FIT
    std::vector<PlantedFixture> valSet;   // set B — disjoint, used to VALIDATE
};
}
```

| Self-test | Method | Required outcome | Trace |
| --- | --- | --- | --- |
| Planted-answer | Synthesize a signal from known circuit-model params; run the calibrator; assert recovery within tolerance | Recovered params within tolerance, else FAILS — catches a calibrator that "succeeds" by echoing input | [ADR-013 C15] |
| Disjoint cal/val | Fit on parameter/seed set A; validate held-out error on disjoint set B | Held-out error within tolerance, else FAILS (overfit detected) | [ADR-013 C16] |
| Negative control | Run the calibrator on a deliberately-WRONG planted fixture | Calibrator MUST reject it; acceptance FAILS if it accepts | [ADR-013 C17] |

The recovery tolerance is a calibration-fixture constant **(PI)** that
centralizes in `core/calibration/Calibration.h`, never duplicated in the test
TU. The fit/validate parameter and seed sets MUST be disjoint by construction
[ADR-013 C16].

## 13. Cross-cutting invariant gates

All of these are `ctest` GATES (not advisories) because they are owner-locked
[ADR-013 cross-cutting].

### 13.1 No-audio-thread allocation / lock (`AudioThreadGuard`)

A fixture installs a `processBlock`-scope sentinel — override global
`new`/`malloc`/`free` and pthread mutex hooks (or run under an RT-safety
harness) — and FAILS on any heap alloc or lock taken during `processBlock`
[ADR-013 C19].

```cpp
namespace mw::test {
class AudioThreadGuard {
public:
    void arm() noexcept;          // begin processBlock-scope sentinel
    void disarm() noexcept;
    bool violated() const noexcept;
    struct Violation { enum class Kind { Alloc, Lock } kind; const void* addr; };
    std::span<const Violation> violations() const noexcept;
    // One-time warm-up carve-out: documented, code-reviewed, never blanket.
    void allowWarmUpOnce() noexcept;
};
}
```

Exercised under stress: block-size sweep, sample-rate change, voice-steal,
mid-block preset recall, automation storm [ADR-013 C19]. This pins the
no-heap/no-lock lock against the per-voice PRNG state, oversampling buffers,
Newton-iteration scratch, and `tanh`/compensation tables — all must be
preallocated at `prepareToPlay` [research/10 §3.7, §5.1]. There is a documented,
code-reviewed ONE-TIME warm-up carve-out for lazy init that must never become a
blanket exemption [ADR-013 C19; Decision].

### 13.2 License-header check

Every source file MUST carry `SPDX-License-Identifier: GPL-3.0-or-later`; a
missing header => ctest FAILS [ADR-013 C18; ADR-014 §1 GPLv3 lock]. The scan
matches the headers used throughout docs/research/*.md and the ADRs.

### 13.3 RT-safety of versioning state (statement)

`renderVersion` read/write, constant-set selection, table regen and the opt-in
flag run on the message thread / at `prepareToPlay` only — no heap allocation
and no locks on the audio thread [ADR-023 V18; ADR-003 §F-11; ADR-008 C19].
Verified by the same `AudioThreadGuard` stress fixture exercising a mid-session
`renderVersion` legacy-path selection at `prepareToPlay`.

### 13.4 FP-discipline flag gate

Compile-time + runtime assertion that `-ffast-math`,
`-funsafe-math-optimizations`, `-freciprocal-math` are OFF and `-ffp-contract`
is `off` for the DSP TU [ADR-013 C20; ADR-014 C4/C5]. The mechanical CI gate
`fp_discipline_guard` greps `compile_commands.json` for forbidden flags
(`-ffast-math`, `-Ofast`, `/fp:fast`, `-ffp-contract=fast`) on every golden/DSP
target and FAILS the build if any appears — fast-math poisoning is caught
mechanically, not by review [ADR-014 C5]. The proof string is recorded in the
bless MANIFEST (`fpFlagProof`, §7.3) [ADR-013 Layer 3].

### 13.5 CPU-budget regression golden

Render a worst-case patch (full poly + unison + 2x oversampling + Newton-iterated
ladder) and assert per-block wall-time stays under a committed ceiling, with
engine and oversample factor pinned in the MANIFEST [ADR-013 C21; research/10
§3.6, §3.7]. A fidelity change (e.g. Huovilainen-Euler -> TPT+Newton) that blows
the RT budget is caught as a regression, not discovered as a user underrun.

```cpp
namespace mw::test {
struct CpuBudgetSpec {
    int      numVoices;          // full poly
    int      unison;             // full unison
    int      oversampleFactor;   // 2x (worst case)
    LadderEngine engine;         // Newton-iterated ladder
    double   sampleRate;
    int      blockSize;
    double   ceilingMicrosPerBlock;  // committed ceiling (PI) — pinned in MANIFEST
};
double measureWorstCaseBlockMicros(const CpuBudgetSpec&);   // median of N runs
}
```

`ceilingMicrosPerBlock` is **(PI)** — a committed wall-time ceiling whose value
is pinned in the MANIFEST alongside engine + oversample factor; it is host-
relative and re-derived per reference host id.

### 13.6 No-JUCE-in-core build guard

The `core` (`mwcore`) target's link/include closure MUST contain zero JUCE
target/header (re-affirms ADR-001 C1) [ADR-014 C11]. Enforced as a build guard
on the `mwcore` target.

## Acceptance hooks

Objectively-testable properties a backlog task's tests must verify (each maps to
a normative contract case):

- An empty / mis-linked / mis-filtered test binary FAILS (`--no-tests=error` /
  "No tests ran" regex) — never green [ADR-013 C1].
- Each required name-prefix (`vco vcf vca env seq prng arp cal`) has >= 1
  discovered test; 0 => ctest FAILS [ADR-013 C2].
- The committed `ctest --print-labels` snapshot differing from current => ctest
  FAILS [ADR-013 C3].
- A numeric DSP test lacking its paired negative/property control, or whose
  control does not fail on a constant stub, => lint/test FAILS (e.g. `k=4`
  self-oscillates, `k=3.9` does not) [ADR-013 C4].
- A CLASS-EXACT golden (seq bytes, divider OR edges, PRNG stream, arp order,
  param-smooth boundaries, CC map) on arm64 OR Linux x64 must SHA-256 match
  bit-for-bit; any diff FAILS [ADR-013 C5].
- A CLASS-FP golden on arm64 must be bit-exact vs the blessed artifact; any diff
  FAILS [ADR-013 C6].
- A CLASS-FP golden on Linux x64 must fall inside its per-corpus manifest
  tolerance band; outside band FAILS [ADR-013 C7]. Windows is the same tier,
  reported, goal-gated [ADR-013 C8].
- Stage 2 (full vector + FFT/NMSE + alias floor) is SKIPPED unless Stage 1 flags
  or `--full` is passed [ADR-013 C9].
- `bless` invoked on Linux/Windows REFUSES (arm64-only) [ADR-013 C10]; invoked
  without non-empty `BLESS_REASON` REFUSES [ADR-013 C11].
- A golden hash absent from `MANIFEST.toml` => CI FAILS [ADR-013 C12]; a MANIFEST
  entry with no corresponding test => CI FAILS [ADR-013 C13].
- A blessed artifact deriving from a ledger §2-§8 fact without its honesty label
  in the MANIFEST entry => CI FAILS [ADR-013 C14].
- Calibration planted-answer recovers params within tolerance [ADR-013 C15];
  disjoint cal/val held-out error within tolerance [ADR-013 C16]; negative
  control is REJECTED [ADR-013 C17].
- Any source file missing `SPDX-License-Identifier: GPL-3.0-or-later` => ctest
  FAILS [ADR-013 C18].
- Any heap alloc or lock taken during `processBlock` (outside the documented
  one-time warm-up carve-out), under the stress fixture, => ctest FAILS [ADR-013
  C19].
- `-ffast-math`/`-funsafe-math-optimizations`/`-freciprocal-math` not OFF, or
  `-ffp-contract` not `off`, in the DSP TU => compile-time + runtime assertion
  FAILS; `fp_discipline_guard` greps `compile_commands.json` [ADR-013 C20;
  ADR-014 C4/C5].
- Worst-case patch per-block wall-time exceeding the committed ceiling => ctest
  FAILS [ADR-013 C21].
- A CLASS-FP golden compared across a different engine tag, oversample factor, or
  `renderVersion` than blessed => compare REFUSES [ADR-013 C22; ADR-023 V11].
- `cmake --preset X` is the only entrypoint; CI runs identical preset names /
  `scripts/check.sh`; no build-test logic in YAML [ADR-014 C1].
- Every dependency pinned to a full 40-char commit SHA with version + SHA + date
  + why + SPDX inline; CPM bootstrap `CPM_DOWNLOAD_HASH`-checked [ADR-014 C2].
- CMake < 3.25 (or non-v6 schema) => configure FAILS with a floor message
  [ADR-014 C3].
- A forbidden FP flag reaching any golden/DSP target => build FAILS [ADR-014 C5].
- Formats built only where a validator is wired (macOS VST3+AU+CLAP+Standalone;
  Linux VST3+CLAP+Standalone+LV2-goal; Windows VST3+CLAP+Standalone) [ADR-014
  C6].
- `configurePresets`/`buildPresets`/`testPresets` names pair 1:1; per-platform
  presets inherit a hidden base and add only toolchain/generator + format scope
  [ADR-014 C8].
- The ~64 presets round-trip (schema + checksum) via `presets_roundtrip`, baked
  to a flat POD table, never parsed on the audio thread [ADR-014 C9].
- A no-network build succeeds via `CPM_SOURCE_CACHE` +
  `FETCHCONTENT_FULLY_DISCONNECTED` [ADR-014 C10].
- The `mwcore` link/include closure contains zero JUCE [ADR-014 C11].
- Windows x64 CI runs `continue-on-error: true`; macOS arm64 + Linux x64 are
  hard gates [ADR-014 C12].
- Golden corpora (CLASS-EXACT and CLASS-FP) exist at each of
  {44100, 48000, 88200, 96000} Hz, keyed by sample rate [ADR-023 V12].
- Blessed artifacts changed without a `renderVersion` bump => CI FAILS; a bump
  with no MANIFEST/artifact change => CI FAILS [ADR-023 V7].
- A host SR strictly above the blessed set runs the FP-tolerance tier only,
  never bit-exact, never blessed [ADR-023 V14]; 2x oversampling above
  `OS_CEILING_HZ = 192000` Hz is clamped to 1x with `fc <= 0.45*fs_os` still
  holding [ADR-023 V15], and the clamp/rate is recorded in provenance + surfaced
  in UI [ADR-023 V16].

## References

- ADR-013 — Testing strategy: golden/regression and calibration harness
  (plan/decisions/013-testing-golden-calibration-harness.md).
- ADR-014 — Build system, dependency management & toolchain
  (plan/decisions/014-build-toolchain-deps.md).
- ADR-023 — Engine versioning, bless communication & blessed sample-rate set
  (plan/decisions/023-engine-versioning-and-blessed-sample-rates.md).
- ADR-001 — Core/plugin boundary (referenced: pure-C++20 core, no-JUCE-in-core
  guard, RT no-alloc/no-lock lock).
- ADR-003 — Filter modeling: Huovilainen engine, 2x oversampling, per-SR
  compensation table, `fc <= 0.45*fs_os` guard, versioned frozen constants
  (referenced).
- ADR-004 — Oversampling / decimator A/B (referenced: engine-tagged goldens).
- ADR-008 — Parameter/state/preset schema; `schemaVersion`; parameter ID
  ownership (referenced — IDs owned by docs/design/06 §2).
- docs/design/06 — Parameter schema (owns parameter IDs/ranges/defaults/skews).
- docs/research/13 — Validation gaps, open disputes & honesty ledger
  (docs/research/13-validation-gaps-and-disputes.md) — cited factual ground
  truth.
- docs/research/10 — DSP modeling techniques (referenced via ADR citations:
  transcendental divergence §3.2/§3.6/§4, alias limit §8, PRNG §6, divider OR
  §7, RT pre-alloc §3.7/§5.1).
- docs/research/12 — Market/legal landscape (referenced: GPLv3 provenance §2,
  §7.3).
- `core/calibration/Calibration.h` — single calibration table for all (PI)
  constants (created by the backlog; owned by the calibration/variance design
  doc).
