// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/CcLearnCodecTest.cpp — JUCE-linked tests for the CC-learn state round-trip
// (task 023b). Closes the 023 QA MEDIUM (PR #106): user MIDI-learn (CC-learn) bindings must
// survive save/reload through the canonical <extras> tree [docs/design/06 §5.4; ADR-012 C16;
// ADR-021]. Asserts every acceptance criterion:
//
//   1. A NON-DEFAULT CC->param binding round-trips through capture -> blob -> read
//      bit-for-bit; the DEFAULT rows restore to the §6.2 seed table.
//   2. Corrupt / absent <ccLearn> falls back to the default seed map WITHOUT failing the
//      whole load (the rest of the blob still parses; the map stays at its seed).
//   3. Only NON-DEFAULT rows are persisted (compact): a default map writes no <ccLearn>,
//      so a pre-023b blob (no <ccLearn>) stays loadable.
//
// Test names begin with the `serializer` tag so `ctest -R serializer` selects them and the
// silent-pass / --no-tests=error rule holds. No '[' in the display-name text.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/StateSerializer.h"   // captureState / writeToBlob / readFromBlob
#include "state/CcLearnCodec.h"      // writeCcLearn / readCcLearn (unit under test)
#include "state/StateTree.h"         // mw::state canonical keys
#include "state/Extras.h"            // mw::state::Extras
#include "midi/CcLearnMap.h"         // mw::plugin::CcLearnMap (the live map, task 100)
#include "params/ParameterLayout.h"  // mw::plugin::buildParameterLayout
#include "calibration/CcLearnMapConstants.h"  // mw::cal::cclearn::kHoldParamIndex

namespace {

// Minimal headless AudioProcessor hosting the FULL APVTS layout so captureState runs
// against the real tree, exactly like StateSerializerTest's host.
class CcHostProcessor final : public juce::AudioProcessor
{
public:
    CcHostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override        { return "CcHost"; }
    void prepareToPlay(double, int) override            {}
    void releaseResources() override                    {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override        { return 0.0; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override                     { return false; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram(int) override                {}
    const juce::String getProgramName(int) override     { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

constexpr int      kSchema  = 1;
constexpr int      kRender  = 1;
const juce::String kPlugin  = "1.2.3";
const juce::String kEngine  = "4.5.6";

// Learn CC#20 (a CC with NO §6.2 default) to param index 3 on a live map. CC20 is unmapped
// in the seed table, so this is unambiguously a NON-DEFAULT user binding.
constexpr int kLearnedCc    = 20;
constexpr int kLearnedParam = 3;

void learnBinding(mw::plugin::CcLearnMap& map, int cc, std::int32_t paramIndex)
{
    auto* draft = map.editableCopy();
    draft[cc].ccNumber   = static_cast<std::uint8_t>(cc);
    draft[cc].paramIndex = paramIndex;
    draft[cc].enabled    = true;
    map.publish();
}

// Capture a canonical tree with the map's non-default bindings written into <extras>, push
// it through writeToBlob -> readFromBlob, and return the restored canonical tree.
std::optional<juce::ValueTree> roundTrip(juce::AudioProcessorValueTreeState& apvts,
                                         const mw::plugin::CcLearnMap& map)
{
    auto canonical = mw::plugin::state::captureState(
        apvts, mw::state::Extras{}, kSchema, kPlugin, kEngine, kRender);

    auto extrasNode = canonical.getChildWithName(juce::Identifier{ mw::state::kExtrasId });
    mw::plugin::state::writeCcLearn(extrasNode, map);

    juce::MemoryBlock blob;
    mw::plugin::state::writeToBlob(canonical, blob);
    return mw::plugin::state::readFromBlob(blob.getData(), static_cast<int>(blob.getSize()));
}

} // namespace

TEST_CASE("serializer cc-learn non-default binding round-trips through capture blob read",
          "[serializer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    CcHostProcessor host;

    // A live map with one NON-DEFAULT learned binding (CC20 -> param 3).
    mw::plugin::CcLearnMap learnedMap;
    learnBinding(learnedMap, kLearnedCc, kLearnedParam);
    REQUIRE(learnedMap.lookup(static_cast<std::uint8_t>(kLearnedCc)) == kLearnedParam);

    const auto restored = roundTrip(host.apvts, learnedMap);
    REQUIRE(restored.has_value());

    // The restored tree carries a <ccLearn> child of <extras> with the learned row.
    const auto extras = restored->getChildWithName(juce::Identifier{ mw::state::kExtrasId });
    REQUIRE(extras.isValid());
    const auto ccLearn = extras.getChildWithName(juce::Identifier{ mw::state::kCcLearnId });
    REQUIRE(ccLearn.isValid());
    REQUIRE(ccLearn.getNumChildren() >= 1);

    // Restore into a FRESH (seed) map and assert the learned binding came back bit-for-bit
    // while a DEFAULT row (CC74 -> cutoff) still resolves to its §6.2 seed value.
    mw::plugin::CcLearnMap target;   // fresh -> §6.2 seed
    const int seedCutoff = target.lookup(74);   // default cutoff binding before restore
    REQUIRE(seedCutoff >= 0);

    const int applied = mw::plugin::state::readCcLearn(*restored, target);
    CHECK(applied == 1);

    // Learned binding restored exactly.
    CHECK(target.lookup(static_cast<std::uint8_t>(kLearnedCc)) == kLearnedParam);
    // Default rows untouched: CC74 still resolves to the seed cutoff index.
    CHECK(target.lookup(74) == seedCutoff);
    // A CC that was never learned and has no default stays unmapped.
    CHECK(target.lookup(20 + 1) == mw::plugin::CcLearnMap::kUnmapped);
}

TEST_CASE("serializer cc-learn default map writes no node and restores to the seed table",
          "[serializer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    CcHostProcessor host;

    // A DEFAULT (untouched) map: nothing learned. captureState + writeCcLearn must write NO
    // <ccLearn> node, so the blob stays byte-compatible with pre-023b sessions.
    const mw::plugin::CcLearnMap defaultMap;
    const auto restored = roundTrip(host.apvts, defaultMap);
    REQUIRE(restored.has_value());

    const auto extras = restored->getChildWithName(juce::Identifier{ mw::state::kExtrasId });
    REQUIRE(extras.isValid());
    CHECK_FALSE(extras.getChildWithName(juce::Identifier{ mw::state::kCcLearnId }).isValid());

    // readCcLearn over a tree with no <ccLearn> applies nothing and leaves the seed intact.
    mw::plugin::CcLearnMap target;
    const int defaultCutoff = target.lookup(74);
    const int applied = mw::plugin::state::readCcLearn(*restored, target);
    CHECK(applied == 0);
    CHECK(target.lookup(74) == defaultCutoff);                 // CC74 -> cutoff (seed)
    CHECK(target.lookup(64) == mw::cal::cclearn::kHoldParamIndex);  // CC64 -> HOLD (seed)
}

TEST_CASE("serializer cc-learn absent or garbage node falls back to seed without load failure",
          "[serializer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    CcHostProcessor host;

    SECTION("absent ccLearn node leaves the seed map")
    {
        // A canonical tree with NO <ccLearn> at all still parses; the map stays at the seed.
        const auto canonical = mw::plugin::state::captureState(
            host.apvts, mw::state::Extras{}, kSchema, kPlugin, kEngine, kRender);
        juce::MemoryBlock blob;
        mw::plugin::state::writeToBlob(canonical, blob);
        const auto restored = mw::plugin::state::readFromBlob(
            blob.getData(), static_cast<int>(blob.getSize()));
        REQUIRE(restored.has_value());   // the WHOLE load still succeeds.

        mw::plugin::CcLearnMap target;
        const int seedCutoff = target.lookup(74);
        const int applied = mw::plugin::state::readCcLearn(*restored, target);
        CHECK(applied == 0);
        CHECK(target.lookup(74) == seedCutoff);
    }

    SECTION("garbage binding rows are dropped, the seed and valid rows survive")
    {
        // Build a canonical tree, then hand-author a <ccLearn> with both a GARBAGE row
        // (out-of-range cc + bogus param) and one VALID learned row. The garbage is rejected
        // without failing the load; the valid row is applied; the seed otherwise survives.
        auto canonical = mw::plugin::state::captureState(
            host.apvts, mw::state::Extras{}, kSchema, kPlugin, kEngine, kRender);
        auto extras = canonical.getChildWithName(juce::Identifier{ mw::state::kExtrasId });
        REQUIRE(extras.isValid());

        juce::ValueTree ccLearn{ juce::Identifier{ mw::state::kCcLearnId } };

        // Garbage: cc out of range, param index nonsense.
        juce::ValueTree bad{ juce::Identifier{ mw::plugin::state::kCcBindingId } };
        bad.setProperty(juce::Identifier{ mw::plugin::state::kCcBindingAttrCc }, 999, nullptr);
        bad.setProperty(juce::Identifier{ mw::plugin::state::kCcBindingAttrParam }, 999999, nullptr);
        bad.setProperty(juce::Identifier{ mw::plugin::state::kCcBindingAttrOn }, true, nullptr);
        ccLearn.appendChild(bad, nullptr);

        // Valid learned row: CC20 -> param 3.
        juce::ValueTree good{ juce::Identifier{ mw::plugin::state::kCcBindingId } };
        good.setProperty(juce::Identifier{ mw::plugin::state::kCcBindingAttrCc }, kLearnedCc, nullptr);
        good.setProperty(juce::Identifier{ mw::plugin::state::kCcBindingAttrParam }, kLearnedParam, nullptr);
        good.setProperty(juce::Identifier{ mw::plugin::state::kCcBindingAttrOn }, true, nullptr);
        ccLearn.appendChild(good, nullptr);

        extras.appendChild(ccLearn, nullptr);

        juce::MemoryBlock blob;
        mw::plugin::state::writeToBlob(canonical, blob);
        const auto restored = mw::plugin::state::readFromBlob(
            blob.getData(), static_cast<int>(blob.getSize()));
        REQUIRE(restored.has_value());   // garbage in <ccLearn> does NOT fail the whole load.

        mw::plugin::CcLearnMap target;
        const int seedCutoff = target.lookup(74);
        const int applied = mw::plugin::state::readCcLearn(*restored, target);
        CHECK(applied == 1);                                            // only the valid row
        CHECK(target.lookup(static_cast<std::uint8_t>(kLearnedCc)) == kLearnedParam);
        CHECK(target.lookup(74) == seedCutoff);                         // seed survives
    }
}
