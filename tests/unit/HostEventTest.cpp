// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for HostEvent + NormalizedEventBuffer (task 099). Names begin
// with "hostevent". Uses the AudioThreadGuard to prove push() never allocates.

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "../invariants/AudioThreadGuard.h"
#include "host/HostEvent.h"

using namespace mw::plugin;

TEST_CASE("hostevent: HostEvent is a trivially copyable POD with the sec 3.2 fields", "[hostevent]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<HostEvent>);
    HostEvent e{HostEventType::NoteOn, /*channel=*/1, /*sampleOffset=*/0,
                /*data0=*/60, /*value=*/1.0f, /*noteId=*/-1};
    REQUIRE(e.type == HostEventType::NoteOn);
    REQUIRE(e.channel == 1);
    REQUIRE(e.data0 == 60);
    REQUIRE(e.noteId == -1);
}

TEST_CASE("hostevent: capacity is 4*maxBlockSize + 256 (PI, from Calibration.h)", "[hostevent]") {
    REQUIRE(eventBufferCapacityFor(512) == 4 * 512 + 256);
    NormalizedEventBuffer buf;
    buf.prepare(512);
    REQUIRE(buf.capacity() == 4 * 512 + 256);
    REQUIRE(buf.size() == 0);
}

TEST_CASE("hostevent: prepare(N) then N pushes succeed and size()==N, order preserved", "[hostevent]") {
    NormalizedEventBuffer buf;
    buf.prepare(8);                                   // capacity = 4*8+256 = 288
    const int n = 100;
    for (int i = 0; i < n; ++i) {
        REQUIRE(buf.push(HostEvent{HostEventType::ParamValue, 1, i, i, 0.0f, -1}));
    }
    REQUIRE(buf.size() == n);
    // Iteration order matches insertion.
    int idx = 0;
    for (const HostEvent* it = buf.begin(); it != buf.end(); ++it, ++idx) {
        REQUIRE(it->sampleOffset == idx);
    }
}

TEST_CASE("hostevent: push beyond capacity drops, returns false, never allocates", "[hostevent]") {
    NormalizedEventBuffer buf;
    buf.prepare(0);                                   // capacity = 4*0+256 = 256
    const int cap = buf.capacity();

    // Fill to capacity outside the armed scope.
    for (int i = 0; i < cap; ++i) {
        REQUIRE(buf.push(HostEvent{HostEventType::ParamValue, 1, i, i, 0.0f, -1}));
    }

    mw::test::AudioThreadGuard g;
    g.arm();
    // An over-capacity push must drop (return false) WITHOUT growing/allocating.
    const bool ok = buf.push(HostEvent{HostEventType::NoteOn, 1, 0, 60, 1.0f, -1});
    g.disarm();

    REQUIRE_FALSE(ok);                // dropped, never grew
    REQUIRE(buf.size() == cap);       // size unchanged
    REQUIRE_FALSE(g.violated());      // push did not allocate
}
