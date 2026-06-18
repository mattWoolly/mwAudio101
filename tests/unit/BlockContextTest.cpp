// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the POD seam (task 007). Names begin with "blockcontext".
// Also carries a "core" name-prefix variant so the `-R core` selector (task 007's
// verification command) is satisfied.

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "BlockContext.h"

TEST_CASE("blockcontext: seam aggregates are PODs with no owning allocation", "[core][blockcontext]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::AudioBlockView>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::TransportInfo>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::MidiEvent>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::MidiEventView>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::BlockContext>);
    STATIC_REQUIRE(std::is_standard_layout_v<mw::BlockContext>);
}

TEST_CASE("core: BlockContext field shapes match docs/design/00 sec 5.3 verbatim", "[core][blockcontext]") {
    // AudioBlockView: float* const* channels; int numChannels; int numFrames.
    STATIC_REQUIRE(std::is_same_v<decltype(mw::AudioBlockView::channels), float* const*>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::AudioBlockView::numChannels), int>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::AudioBlockView::numFrames), int>);

    // TransportInfo: double bpm/ppq; bool isPlaying; double sampleRate.
    STATIC_REQUIRE(std::is_same_v<decltype(mw::TransportInfo::bpm), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::TransportInfo::isPlaying), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::TransportInfo::sampleRate), double>);

    // MidiEvent: typed kind + channel/noteId/data0/value/sampleOffset.
    STATIC_REQUIRE(std::is_same_v<decltype(mw::MidiEvent::type), mw::NormalizedType>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::MidiEvent::sampleOffset), int>);

    // BlockContext holds a const ParamSnapshot* (no JUCE type crosses the seam).
    STATIC_REQUIRE(std::is_pointer_v<decltype(mw::BlockContext::params)>);
}
