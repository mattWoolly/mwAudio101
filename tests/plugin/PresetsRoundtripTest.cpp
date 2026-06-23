// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetsRoundtripTest.cpp — the presets_roundtrip ctest (task 025b).
// Test-case display names begin with the `presets` tag word and avoid the '[' char so
// `ctest -R presets_roundtrip --no-tests=error` selects exactly these and the
// silent-pass rule holds [AGENTS.md Tests; docs/design/11 §8.1].
//
// Realizes plan/backlog/025b 1:1 [docs/design/11 §9.1; ADR-014 C9; ADR-001 C9;
// ADR-013 C4]:
//
//   For EVERY patch in the bundled corpus this round-trips through BOTH projection
//   paths and asserts they agree end-to-end:
//
//     project  -> serialize -> reload -> re-project, asserting the schemaVersion and
//     the SHA-256 checksum (task 040, mw::golden::sha256) survive end-to-end, and that
//     the reloaded baked POD entry is BYTE-IDENTICAL to the original baked entry (the
//     POD round-trip is lossless) [§9.1; ADR-014 C9; ADR-001 C9].
//
//   THE TWO PROJECTION PATHS. There are two distinct readers of the .mw101preset shape
//   in the tree, and 025b is where their EQUIVALENCE is OWNED (the safety net the 144b
//   QA called for — 144b's JUCE-free reader assumes well-formed JSON and substitutes
//   kParamDefs defaults for omitted params, so a corpus param-drop or a reader
//   divergence surfaces HERE):
//
//     (A) plugin/preset/PresetFormat.* (task 025): the JUCE-bound .mw101preset JSON
//         <-> canonical MW101_STATE juce::ValueTree projection + the §6.4 validator.
//     (B) core/preset/PresetBake.h (task 144b): the JUCE-FREE flat-POD bake
//         (bake()/verify()/canonicalImage()), schemaVersion + SHA-256 per BakedPreset.
//
//   For each preset we assert the 025-projection param set (the 91 values in kParamDefs
//   order extracted from the canonical ValueTree) MATCHES the 144b-bake BakedPreset's
//   params, and likewise the seq pattern POD (stepCount + per-step note/gate/tie/rest +
//   arp latch). If they diverge for ANY preset, that is a REAL finding (the two readers
//   disagree) and the test FAILS.
//
//   AUDIO-THREAD-POD-ONLY (re-affirms the 144b contract). The bake (parse + projection)
//   runs OUTSIDE the measured window; the load/index of the finished POD table runs
//   inside it with ZERO heap allocation and ZERO parse. Because mw101_plugin_tests does
//   NOT link the global-new AudioThreadGuard sentinel (that TU lives only in the
//   JUCE-free mw101_tests core binary, where tests/unit/PresetBakeTest.cpp already arms
//   it over the bake), this plugin-side case uses the established sibling-test approach:
//   the macOS arm64 allocator's exact bytes_used delta via mstats() — an override-free,
//   collision-proof heap probe (mirrors tests/plugin/MpeReconstructorTest.cpp). A
//   positive control proves the probe is sensitive, so the no-alloc assertion is
//   non-vacuous [ADR-001 C3/C4; docs/design/11 §13.1; docs/design/09 §1.2].
//
//   NEGATIVE CONTROL (non-vacuity, ADR-013 C4). A deliberately corrupted checksum (a
//   flipped param byte whose stored checksum no longer matches) AND a deliberately
//   bumped schemaVersion on one patch BOTH make the round-trip assertions FAIL — proving
//   the gate is not a silent pass.
//
// WHY plugin-side: the round-trip goes through the JUCE PresetFormat projection (025),
// which references juce::ValueTree / juce::JSON / juce::File and so cannot live in
// mwcore [ADR-001 C1]. This test links BOTH the JUCE PresetFormat AND the JUCE-free
// core PresetBake, and is built under MW_BUILD_PLUGIN=ON (mirrors task 023/131/151).
// The 144b core bake is also covered JUCE-free in tests/unit/PresetBakeTest.cpp; this
// adds NO new core tag (the `presets` tag is plugin-side here) — the core
// ctest-labels.snapshot is untouched.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <malloc/malloc.h>              // mstats(): override-free heap-usage probe (macOS arm64)

#include <juce_audio_processors/juce_audio_processors.h>

#include "preset/PresetFormat.h"        // (A) JUCE projection (task 025)
#include "preset/PresetBake.h"          // (B) JUCE-free flat-POD bake (task 144b)
#include "params/ParamDefs.h"           // kParamDefs — canonical param order/count
#include "state/StateTree.h"            // canonical ValueTree keys
#include "state/Extras.h"               // mw::state::Extras / SeqStep — seq POD
#include "version/EngineVersion.h"      // kCurrentSchemaVersion

#include "../golden/Sha256.h"                // task-040 SHA-256 (the injected hasher)

namespace pb = mw::preset;

namespace {

using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::loadPresetJson;
using mw::plugin::preset::writePresetJson;

// The six §6.5 categories (folder names AND meta.category enum values), so the coverage
// assertion can prove >= 1 patch per category survived the round-trip.
const std::array<juce::String, 6> kCategories{
    juce::String{ "AcidBassLead" }, juce::String{ "SubBass" },  juce::String{ "Lead" },
    juce::String{ "PWMStrings" },   juce::String{ "BlipsFX" },  juce::String{ "SeqArpRiff" }
};

// Locate the repository's presets/ root by walking up from this test source file's
// compile-time path, with a cwd-walk fallback. Mirrors FactoryPresetCorpusTest /
// PresetBankCoverageTest so the corpus is resolved identically locally and in CI.
juce::File findPresetsRoot()
{
    const juce::File thisSource{ juce::String::fromUTF8(__FILE__) };
    if (thisSource.existsAsFile())
    {
        const auto repoRoot = thisSource.getParentDirectory()    // tests/plugin
                                        .getParentDirectory()    // tests
                                        .getParentDirectory();   // repo root
        const auto dir = repoRoot.getChildFile("presets");
        if (dir.isDirectory())
            return dir;
    }
    for (auto dir = juce::File::getCurrentWorkingDirectory();
         dir.exists() && dir != dir.getParentDirectory();
         dir = dir.getParentDirectory())
    {
        const auto candidate = dir.getChildFile("presets");
        if (candidate.isDirectory())
            return candidate;
    }
    return {};
}

// Every *.mw101preset file under presets/ (recursive), in a DETERMINISTIC order
// (lexicographic by full path) so the bake input order is reproducible — the same
// ordering tests/unit/PresetBakeTest.cpp uses for the core bake.
std::vector<juce::File> gatherCorpusFiles()
{
    std::vector<juce::File> files;
    const auto root = findPresetsRoot();
    if (root.isDirectory())
        for (const auto& f : root.findChildFiles(juce::File::findFiles, /*recursive=*/true,
                                                 "*.mw101preset"))
            files.push_back(f);
    std::sort(files.begin(), files.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getFullPathName() < b.getFullPathName(); });
    return files;
}

// The injected hasher: task-040 SHA-256 over a byte span, returned as the 32-byte digest
// the baker stores per entry (identical to tests/unit/PresetBakeTest.cpp).
pb::Checksum sha256Hasher(std::span<const std::byte> bytes)
{
    const mw::golden::Sha256 d = mw::golden::sha256(bytes);
    pb::Checksum out{};
    out.bytes = d.bytes;
    return out;
}

// Bake ONE .mw101preset JSON text into a single baked POD entry via the 144b path.
pb::BakedPreset bakeOneJson(const std::string& id, const std::string& json)
{
    std::vector<pb::PresetSource> src{ pb::PresetSource{ id, json } };
    const pb::BakedTable t = pb::bake(src, sha256Hasher);
    REQUIRE(t.size() == 1);
    return t.entry(0);
}

// Extract the 91 modeled param values (in kParamDefs order) from a canonical
// MW101_STATE ValueTree produced by the 025 PresetFormat projection. Continuous params
// store a float value; Choice/Bool store an integer index — both read back as a double.
// A missing node is the registry default (mirrors the bake's "complete params" rule), so
// a silent param drop on EITHER path surfaces as a value divergence in the cross-check.
std::array<float, pb::BakedPreset::kParamCount>
projectionParamValues(const juce::ValueTree& canonical)
{
    std::array<float, pb::BakedPreset::kParamCount> out{};
    const auto params =
        canonical.getChildWithName(juce::Identifier{ mw::state::kParamsId });
    for (std::size_t k = 0; k < pb::BakedPreset::kParamCount; ++k)
    {
        const auto& def = mw::params::kParamDefs[k];
        double v = static_cast<double>(def.defaultValue);
        if (params.isValid())
        {
            const auto node =
                params.getChildWithProperty("id", juce::String::fromUTF8(def.id));
            if (node.isValid())
                v = static_cast<double>(node.getProperty("value"));
        }
        out[k] = static_cast<float>(v);
    }
    return out;
}

// Extract the seq pattern POD (stepCount + per-step note/gate/tie/rest + arp latch) from
// a canonical ValueTree, in the SAME shape the 144b bake stores in BakedPreset::extras,
// so the two seq projections can be compared field-for-field.
mw::state::Extras projectionExtras(const juce::ValueTree& canonical)
{
    mw::state::Extras ex{};
    const auto extras =
        canonical.getChildWithName(juce::Identifier{ mw::state::kExtrasId });
    if (! extras.isValid())
        return ex;

    ex.arpLatch =
        static_cast<bool>(extras.getProperty(mw::state::kExtrasArpLatch, false));

    const auto seq = extras.getChildWithName(juce::Identifier{ mw::state::kSeqId });
    if (! seq.isValid())
        return ex;

    int stepCount = juce::jlimit(0, mw::state::kMaxSeqSteps,
                                 static_cast<int>(seq.getProperty("stepCount", 0)));
    const int n = juce::jmin(stepCount, seq.getNumChildren());
    for (int i = 0; i < n; ++i)
    {
        const auto stepTree = seq.getChild(i);
        mw::state::SeqStep s{};
        const int note = static_cast<int>(stepTree.getProperty("note", 0));
        s.noteSemitone = static_cast<std::int8_t>(juce::jlimit(-128, 127, note));
        s.gate = static_cast<bool>(stepTree.getProperty("gate", true));
        s.tie  = static_cast<bool>(stepTree.getProperty("tie", false));
        s.rest = static_cast<bool>(stepTree.getProperty("rest", false));
        ex.steps[static_cast<std::size_t>(i)] = s;
    }
    ex.stepCount = n;
    return ex;
}

// Compare two seq PODs field-for-field over the ACTIVE step range (the inactive tail of
// the fixed-capacity array is don't-care for equivalence). True iff identical.
bool seqEquivalent(const mw::state::Extras& a, const mw::state::Extras& b)
{
    if (a.stepCount != b.stepCount) return false;
    if (a.arpLatch != b.arpLatch) return false;
    for (int i = 0; i < a.stepCount; ++i)
    {
        const auto& sa = a.steps[static_cast<std::size_t>(i)];
        const auto& sb = b.steps[static_cast<std::size_t>(i)];
        if (sa.noteSemitone != sb.noteSemitone) return false;
        if (sa.gate != sb.gate) return false;
        if (sa.tie  != sb.tie)  return false;
        if (sa.rest != sb.rest) return false;
    }
    return true;
}

// Load a JSON text through the 025 projection by writing it to a temp file (loadPresetJson
// takes a juce::File). Returns nullopt exactly when the projection rejects it.
std::optional<juce::ValueTree> projectJson(const juce::String& json, PresetMeta& outMeta)
{
    const juce::TemporaryFile temp{ ".mw101preset" };
    if (! temp.getFile().replaceWithText(json))
        return std::nullopt;
    return loadPresetJson(temp.getFile(), outMeta);
}

} // namespace

// =====================================================================================
// (1) Every preset round-trips: project -> serialize -> reload -> re-project, with the
//     schemaVersion + SHA-256 checksum surviving end-to-end, and the reloaded baked POD
//     entry BYTE-IDENTICAL to the original baked entry.
// =====================================================================================
TEST_CASE("presets_roundtrip every patch survives project serialize reload re-project "
          "with matching schemaVersion and SHA-256 and a byte-identical POD entry",
          "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = gatherCorpusFiles();
    INFO("resolved presets/ to: " << findPresetsRoot().getFullPathName());
    REQUIRE(files.size() >= 60);   // ~64 factory patches + INIT [ADR-014 C9]

    bool sawInit = false;
    for (const auto& file : files)
    {
        const std::string id = file.getFileName().toStdString();
        INFO("preset: " << file.getFullPathName());

        // --- project (025): the on-disk JSON -> canonical ValueTree ---------------
        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());   // a faithful factory preset always projects

        if (meta.name == "INIT")
            sawInit = true;

        // --- bake the ORIGINAL JSON (144b): the reference baked POD entry ---------
        const std::string originalJson = file.loadFileAsString().toStdString();
        const pb::BakedPreset baked = bakeOneJson(id, originalJson);

        // The baked entry verifies against its own stored checksum without re-parsing.
        REQUIRE(pb::verify(baked, sha256Hasher));
        REQUIRE(baked.schemaVersion == mw101::version::kCurrentSchemaVersion);
        REQUIRE(baked.paramCount == pb::BakedPreset::kParamCount);

        // --- serialize (025): canonical ValueTree -> §6.2-shaped JSON text --------
        const juce::String reserializedJson = writePresetJson(*canonical, meta);

        // --- reload (025): the re-serialized JSON projects again -------------------
        PresetMeta meta2;
        const auto canonical2 = projectJson(reserializedJson, meta2);
        REQUIRE(canonical2.has_value());   // the round-tripped JSON re-validates

        // --- re-project (144b): bake the RE-SERIALIZED JSON -----------------------
        const pb::BakedPreset rebaked =
            bakeOneJson(id, reserializedJson.toStdString());

        // schemaVersion survives end-to-end [§9.1; ADR-014 C9].
        REQUIRE(rebaked.schemaVersion == baked.schemaVersion);

        // SHA-256 checksum survives end-to-end: the rebaked entry's stored checksum
        // equals the original's, AND verifies against its own content [§9.1; ADR-014 C9].
        REQUIRE(pb::verify(rebaked, sha256Hasher));
        REQUIRE(rebaked.checksum == baked.checksum);

        // POD byte-identical: the reloaded baked entry's canonical payload image is
        // byte-for-byte the original's (the POD round-trip is lossless) [ADR-001 C9].
        // The byte comparison is over the CANONICAL image, not raw struct memory: the
        // POD carries indeterminate padding (mw::state::Extras is 424 B but its fields
        // sum to 414 B — 10 padding bytes the C++ object model leaves unspecified), so a
        // raw memcmp of two independently-baked entries is NOT a valid identity test.
        // The canonical little-endian image is exactly the platform-stable byte form the
        // checksum is computed over and the design's "byte-identical across boxes"
        // contract uses [core/preset/PresetBake.h DETERMINISM; docs/design/11 §9.1].
        const auto imgA = pb::entryPayloadImage(baked);
        const auto imgB = pb::entryPayloadImage(rebaked);
        REQUIRE(imgA.size() == imgB.size());
        REQUIRE(std::equal(imgA.begin(), imgA.end(), imgB.begin()));

        // The same identity restated as a hash: SHA-256 over each entry's canonical image
        // is equal (a single payload byte difference would flip the digest).
        REQUIRE(mw::golden::sha256(imgA) == mw::golden::sha256(imgB));
    }

    REQUIRE(sawInit);   // INIT / baseline (task 144) is in the round-tripped set
}

// =====================================================================================
// (2) TWO-PATH EQUIVALENCE — the 144b QA safety net. For each preset the 025-projection
//     param set (91 values in kParamDefs order) MATCHES the 144b-bake's BakedPreset
//     params, and likewise the seq pattern POD (the two readers must agree).
// =====================================================================================
TEST_CASE("presets_roundtrip the 025 ValueTree projection and the 144b POD bake agree on "
          "every param value and the seq pattern for every preset",
          "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = gatherCorpusFiles();
    REQUIRE(files.size() >= 60);

    int comparisons = 0;
    for (const auto& file : files)
    {
        const std::string id = file.getFileName().toStdString();
        INFO("preset: " << file.getFullPathName());

        // (A) 025 projection -> canonical ValueTree -> 91 param values + seq POD.
        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());
        const auto projParams = projectionParamValues(*canonical);
        const auto projExtras = projectionExtras(*canonical);

        // (B) 144b bake -> BakedPreset params + extras.
        const std::string originalJson = file.loadFileAsString().toStdString();
        const pb::BakedPreset baked = bakeOneJson(id, originalJson);

        // schemaVersion agreement between the two readers.
        REQUIRE(static_cast<int>(baked.schemaVersion)
                == static_cast<int>(canonical->getProperty(mw::state::kAttrSchemaVersion, -1)));

        // Param-by-param equivalence in kParamDefs order. A divergence here is a REAL
        // finding (the two readers disagree on a param value) — fail + name the param.
        for (std::size_t k = 0; k < pb::BakedPreset::kParamCount; ++k)
        {
            INFO("param[" << k << "] id=" << mw::params::kParamDefs[k].id
                 << " projection=" << projParams[k]
                 << " bake=" << baked.params[k]);
            REQUIRE(projParams[k] == baked.params[k]);
            ++comparisons;
        }

        // Seq pattern equivalence (stepCount + per-step note/gate/tie/rest + arp latch).
        INFO("seq divergence: projection.stepCount=" << projExtras.stepCount
             << " bake.stepCount=" << baked.extras.stepCount
             << " projection.arpLatch=" << projExtras.arpLatch
             << " bake.arpLatch=" << baked.extras.arpLatch);
        REQUIRE(seqEquivalent(projExtras, baked.extras));
    }

    // Non-vacuity: we actually compared 91 params for >= 60 presets (>= 5460 checks).
    REQUIRE(comparisons >= 60 * static_cast<int>(pb::BakedPreset::kParamCount));
}

// =====================================================================================
// (3) COVERAGE — INIT/baseline + >= 1 patch per §6.5 category round-trips, and the
//     corpus is the authored bank (>= 60 patches). The gate fails if any category breaks
//     the schema/checksum contract.
// =====================================================================================
TEST_CASE("presets_roundtrip covers INIT plus at least one patch per category and asserts "
          "the corpus count",
          "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = gatherCorpusFiles();
    REQUIRE(files.size() >= 60);   // assert the corpus count [ADR-014 C9]

    std::array<int, 6> perCategoryRoundTripped{};
    bool initRoundTripped = false;

    for (const auto& file : files)
    {
        const std::string id = file.getFileName().toStdString();
        INFO("preset: " << file.getFullPathName());

        // Full schema+checksum round-trip for this preset (the same gate as case 1).
        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        const std::string originalJson = file.loadFileAsString().toStdString();
        const pb::BakedPreset baked = bakeOneJson(id, originalJson);
        const juce::String reJson = writePresetJson(*canonical, meta);
        const pb::BakedPreset rebaked = bakeOneJson(id, reJson.toStdString());

        const bool roundTrips = pb::verify(rebaked, sha256Hasher)
                             && rebaked.checksum == baked.checksum
                             && rebaked.schemaVersion == baked.schemaVersion;
        REQUIRE(roundTrips);

        if (meta.name == "INIT")
            initRoundTripped = true;

        for (std::size_t c = 0; c < kCategories.size(); ++c)
            if (meta.category == kCategories[c])
                ++perCategoryRoundTripped[c];
    }

    // INIT / baseline (task 144) round-trips.
    REQUIRE(initRoundTripped);

    // Every §6.5 category has >= 1 patch that round-tripped (the gate fails if any
    // category breaks the schema/checksum contract) [025b Scope; §6.5].
    for (std::size_t c = 0; c < kCategories.size(); ++c)
    {
        INFO("category with no round-tripped patch: " << kCategories[c]);
        REQUIRE(perCategoryRoundTripped[c] >= 1);
    }
}

// =====================================================================================
// (4) AUDIO-THREAD-POD-ONLY — re-affirms the 144b contract through the round-trip's own
//     baked table: the project/serialize/reload/re-bake (all parse + JUCE projection)
//     runs OUTSIDE the measured window; the load/index of the finished POD runs inside
//     it with ZERO heap allocation and ZERO parse, proven by a zero mstats() bytes_used
//     delta. mw101_plugin_tests does NOT link the global-new AudioThreadGuard sentinel
//     (that TU lives only in the JUCE-free core mw101_tests, where PresetBakeTest already
//     arms it over the bake), so this uses the established mstats() heap probe — the same
//     override-free approach as tests/plugin/MpeReconstructorTest.cpp. macOS arm64 is the
//     documented bless platform [ADR-001 C3/C4; docs/design/11 §13.1; docs/design/09 §1.2].
// =====================================================================================
TEST_CASE("presets_roundtrip the round-tripped POD table is read with no parse and no "
          "heap allocation in the hot path",
          "[presets][rt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = gatherCorpusFiles();
    REQUIRE(files.size() >= 60);

    // Build the round-tripped POD table ENTIRELY before the measured window: for each
    // preset, project (025) -> serialize (025) -> bake the re-serialized JSON (144b),
    // accumulating the finished POD entries. All parse / JUCE / heap work happens here.
    pb::BakedTable table;
    table.reserve(files.size());
    for (const auto& file : files)
    {
        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());
        const juce::String reJson = writePresetJson(*canonical, meta);
        std::vector<pb::PresetSource> src{
            pb::PresetSource{ file.getFileName().toStdString(), reJson.toStdString() } };
        table.push(pb::bake(src, sha256Hasher).entry(0));
    }
    REQUIRE(table.size() == files.size());

    // The hot-path read body: index the contiguous POD table and consume each entry's
    // param values + the seq POD + schemaVersion. No JSON parse, no projection, no heap.
    float acc = 0.0f;
    std::int64_t seqAcc = 0;
    const auto readPod = [&]()
    {
        for (std::size_t i = 0; i < table.size(); ++i)
        {
            const pb::BakedPreset& e = table.entry(i);   // pure POD index, no alloc
            for (const float v : e.params) acc += v;
            seqAcc += e.extras.stepCount;
            for (int s = 0; s < e.extras.stepCount; ++s)
                seqAcc += e.extras.steps[static_cast<std::size_t>(s)].noteSemitone;
            seqAcc += static_cast<std::int64_t>(e.schemaVersion);
        }
    };

    // Touch mstats() once before the measured window so any lazy first-call allocator
    // bookkeeping is not counted against the POD read (mirrors MpeReconstructorTest).
    (void) mstats();

    // POSITIVE CONTROL: a deliberate heap allocation INSIDE the measured window must move
    // bytes_used, proving the probe is sensitive (so the zero-delta assertion below is
    // non-vacuous). The vector is sized from a volatile so the allocation is not elided.
    {
        const std::size_t before = mstats().bytes_used;
        volatile std::size_t n = 4096;
        std::vector<std::byte> sentinel(static_cast<std::size_t>(n));
        sentinel[0] = std::byte{ 1 };   // touch it so the buffer is not optimized away
        const std::size_t after = mstats().bytes_used;
        REQUIRE(after > before);        // the probe DOES observe a heap allocation
    }

    // THE INVARIANT: reading the finished POD table allocates ZERO bytes (no parse, no
    // projection, no heap) — net bytes_used is unchanged across the hot-path read. This
    // net-delta probe catches a RETAINED allocation (the realistic shape of a load that
    // parses/keeps heap state — verified by mutation: pushing into a vector that outlives
    // the loop trips this); a transient alloc-then-free that the allocator recycles from
    // its free-list nets to zero and would NOT be caught, which is the documented limit
    // of a net-delta probe shared with tests/plugin/MpeReconstructorTest.cpp. The exact
    // per-instruction no-alloc/no-lock guarantee over the bake/POD path is the armed
    // AudioThreadGuard's job in the JUCE-free core mw101_tests (PresetBakeTest) [§13.1].
    const std::size_t before = mstats().bytes_used;
    readPod();
    const std::size_t after = mstats().bytes_used;

    REQUIRE(after == before);   // ZERO net heap allocation reading the round-tripped POD
    REQUIRE(acc != 0.0f);       // the read did real work (non-vacuous)
    REQUIRE(seqAcc >= 0);
}

// =====================================================================================
// (5) NEGATIVE CONTROL (non-vacuity, ADR-013 C4). A deliberately corrupted checksum AND
//     a deliberately bumped schemaVersion on one patch BOTH break the round-trip
//     assertions — proving the gate is not a silent pass.
// =====================================================================================
TEST_CASE("presets_roundtrip a corrupted checksum or a bumped schema FAILS the round-trip "
          "gate",
          "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = gatherCorpusFiles();
    REQUIRE(files.size() >= 60);

    // Bake one real preset to a clean baseline entry (the control's positive half).
    const auto& file = files.front();
    const std::string id = file.getFileName().toStdString();
    const std::string originalJson = file.loadFileAsString().toStdString();
    const pb::BakedPreset baked = bakeOneJson(id, originalJson);
    REQUIRE(pb::verify(baked, sha256Hasher));            // the clean entry verifies

    SECTION("a corrupted payload byte breaks verify() and the checksum-equality assertion")
    {
        // Flip one bit of a param value WITHOUT updating the stored checksum: the entry
        // now lies about its content, exactly the silent-corruption the gate must catch.
        pb::BakedPreset corrupted = baked;
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(corrupted.params[0]);
        corrupted.params[0] = std::bit_cast<float>(bits ^ 0x1u);

        // The corruption is detected: verify() FAILS, the checksum no longer matches the
        // clean entry's, and the POD is no longer byte-identical. (If ANY of these passed
        // the round-trip gate would be vacuous.)
        REQUIRE_FALSE(pb::verify(corrupted, sha256Hasher));
        REQUIRE(corrupted.checksum == baked.checksum);   // stored checksum untouched ...
        const auto imgClean = pb::entryPayloadImage(baked);
        const auto imgCorr  = pb::entryPayloadImage(corrupted);
        // ... but the PAYLOAD image diverges, so a checksum recomputed over content
        // differs — the end-to-end checksum survival assertion would FAIL.
        REQUIRE_FALSE(std::equal(imgClean.begin(), imgClean.end(), imgCorr.begin()));
        REQUIRE_FALSE(mw::golden::sha256(imgClean) == mw::golden::sha256(imgCorr));
    }

    SECTION("a bumped schemaVersion breaks the end-to-end schemaVersion assertion")
    {
        // Re-author the preset's JSON with schemaVersion bumped beyond the baked one and
        // re-bake: the rebaked schemaVersion no longer equals the original baked one, so
        // the case-1 `rebaked.schemaVersion == baked.schemaVersion` assertion would FAIL.
        std::string bumped = originalJson;
        const auto pos = bumped.find("\"schemaVersion\":");
        REQUIRE(pos != std::string::npos);
        const auto colon = bumped.find(':', pos);
        const auto end = bumped.find_first_of(",}\n", colon + 1);
        bumped.replace(colon + 1, end - (colon + 1), " 999");

        const pb::BakedPreset rebumped = bakeOneJson(id, bumped);
        REQUIRE(rebumped.schemaVersion == 999);
        REQUIRE_FALSE(rebumped.schemaVersion == baked.schemaVersion);
        // The bumped schema also changes the canonical payload, so the checksum diverges.
        REQUIRE_FALSE(rebumped.checksum == baked.checksum);
    }
}
