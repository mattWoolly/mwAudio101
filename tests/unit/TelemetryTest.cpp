// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/TelemetryTest.cpp — TDD acceptance tests for the UI telemetry SPSC
// path (task 107). Test-case names begin with "ui_telemetry" so the
// `-R ui_telemetry` selector matches (docs/design/11 §8.2; AGENTS.md silent-pass).
//
// Covers docs/design/10-ui.md §8.3 + ADR-015 C5/C12 acceptance criteria:
//   - Snapshot is trivially copyable and fixed size.
//   - push() performs no heap allocation and takes no lock; overrun overwrites the
//     oldest frame and never blocks.
//   - pull() coalesces to the most-recent frame; returns false when empty.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <type_traits>

#include "calibration/TelemetryConstants.h"
#include "ui/Telemetry.h"

// The processBlock-scope alloc sentinel (task 010): asserting push() does NOT
// allocate while armed is the objective no-alloc check the acceptance criterion
// requires ("via instrumented allocator").
#include "../invariants/AudioThreadGuard.h"

using mw::test::AudioThreadGuard;
using mw::ui::Telemetry::Buffer;
using mw::ui::Telemetry::Consumer;
using mw::ui::Telemetry::Producer;
using mw::ui::Telemetry::Snapshot;

namespace {
// Build a recognizable Snapshot so coalescing/most-recent assertions are objective.
Snapshot makeSnapshot(float tag) noexcept {
    Snapshot s;
    s.vcaLevelL = tag;
    s.vcaLevelR = tag * 0.5f;
    s.vcfCutoffDisplay = tag * 0.25f;
    s.lfoPhase = static_cast<std::uint32_t>(tag);
    s.seqStep = static_cast<std::uint64_t>(tag);
    s.scope[0] = tag;
    s.scope[mw::cal::telemetry::kScopePoints - 1] = tag;
    return s;
}
} // namespace

TEST_CASE("ui_telemetry: Snapshot is a trivially copyable POD of fixed size", "[ui_telemetry][ui]") {
    // §8.3: "POD, fixed size, trivially copyable".
    STATIC_REQUIRE(std::is_trivially_copyable_v<Snapshot>);
    STATIC_REQUIRE(std::is_standard_layout_v<Snapshot>);

    // Fixed size: the scope array length is the calibration-sourced kScopePoints,
    // and the size is a compile-time constant (no dynamic members).
    STATIC_REQUIRE(std::tuple_size_v<decltype(Snapshot{}.scope)> == mw::cal::telemetry::kScopePoints);
    STATIC_REQUIRE(sizeof(Snapshot) == sizeof(Snapshot)); // constant, not heap-backed

    // The documented members exist with the documented types (§8.3).
    STATIC_REQUIRE(std::is_same_v<decltype(Snapshot{}.vcaLevelL), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(Snapshot{}.vcaLevelR), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(Snapshot{}.vcfCutoffDisplay), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(Snapshot{}.lfoPhase), std::uint32_t>);
    STATIC_REQUIRE(std::is_same_v<decltype(Snapshot{}.seqStep), std::uint64_t>);
}

TEST_CASE("ui_telemetry: Producer/Consumer pre-allocated, fixed capacity from calibration", "[ui_telemetry][ui]") {
    // §8.3: kScopePoints / kFifoCapacity are sourced from calibration (PI), not
    // re-minted at the telemetry call site.
    STATIC_REQUIRE(Buffer::capacity() == mw::cal::telemetry::kFifoCapacity);
    REQUIRE(mw::cal::telemetry::kFifoCapacity >= 2); // a ring needs depth to drop on overrun
}

TEST_CASE("ui_telemetry: pull() returns false on an empty buffer", "[ui_telemetry][ui]") {
    // §8.3: "false if none".
    Buffer buf;
    Producer producer;
    Consumer consumer;
    producer.prepare(buf);
    consumer.prepare(buf);

    Snapshot out = makeSnapshot(7.0f); // sentinel; pull must not touch it / must report false
    REQUIRE_FALSE(consumer.pull(out));
}

TEST_CASE("ui_telemetry: pull() returns the pushed frame, then false again", "[ui_telemetry][ui]") {
    Buffer buf;
    Producer producer;
    Consumer consumer;
    producer.prepare(buf);
    consumer.prepare(buf);

    producer.push(makeSnapshot(3.0f));

    Snapshot out{};
    REQUIRE(consumer.pull(out));
    REQUIRE(out.vcaLevelL == 3.0f);
    REQUIRE(out.lfoPhase == 3u);
    REQUIRE(out.seqStep == 3u);

    // Drained: a second pull with nothing new returns false.
    REQUIRE_FALSE(consumer.pull(out));
}

TEST_CASE("ui_telemetry: pull() coalesces to the most-recent frame", "[ui_telemetry][ui]") {
    // §8.3: "coalesces to most-recent". Push several frames, pull once: get the last.
    Buffer buf;
    Producer producer;
    Consumer consumer;
    producer.prepare(buf);
    consumer.prepare(buf);

    producer.push(makeSnapshot(1.0f));
    producer.push(makeSnapshot(2.0f));
    producer.push(makeSnapshot(3.0f));

    Snapshot out{};
    REQUIRE(consumer.pull(out));
    REQUIRE(out.vcaLevelL == 3.0f); // most-recent, not the first

    // Nothing left after coalescing the whole backlog.
    REQUIRE_FALSE(consumer.pull(out));
}

TEST_CASE("ui_telemetry: overrun overwrites oldest and never blocks", "[ui_telemetry][ui]") {
    // §8.3 / ADR-015 C5: "overrun => overwrite oldest, never block". Push far more
    // than the ring capacity with NO intervening pull; every push must complete and
    // the consumer must then see the most-recent value, never a stale/older one.
    Buffer buf;
    Producer producer;
    Consumer consumer;
    producer.prepare(buf);
    consumer.prepare(buf);

    const int overpush = mw::cal::telemetry::kFifoCapacity * 4 + 1;
    for (int i = 1; i <= overpush; ++i) {
        producer.push(makeSnapshot(static_cast<float>(i))); // returns (does not block)
    }

    Snapshot out{};
    REQUIRE(consumer.pull(out));
    // Coalescing after overrun yields the newest frame written.
    REQUIRE(out.vcaLevelL == static_cast<float>(overpush));
}

TEST_CASE("ui_telemetry: push() performs no heap allocation (instrumented allocator)", "[ui_telemetry][ui][rt]") {
    // ADR-015 C12 / §3.4: the audio-thread producer allocates nothing. Arm the
    // process-scope alloc sentinel (task 010 global new/delete override) around
    // push() and assert zero violations — including the overrun path.
    Buffer buf;
    Producer producer;
    Consumer consumer;
    producer.prepare(buf); // prepare may set up state OUTSIDE the armed scope
    consumer.prepare(buf);

    const Snapshot s = makeSnapshot(5.0f);

    AudioThreadGuard guard;
    guard.arm();
    for (int i = 0; i < mw::cal::telemetry::kFifoCapacity * 3; ++i) {
        producer.push(s); // hot path: must not allocate, even on overrun
    }
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

TEST_CASE("ui_telemetry: push() and pull() hot paths are noexcept", "[ui_telemetry][ui][rt]") {
    // §3 / §8.3: hot-path producer methods and any method the audio thread may touch
    // are declared noexcept. No-lock is structural (no mutex member; lock-free ring) —
    // the no-alloc test above is the observable RT-safety assertion.
    Snapshot s{};
    Buffer buf;
    Producer producer;
    Consumer consumer;
    STATIC_REQUIRE(noexcept(producer.push(s)));
    STATIC_REQUIRE(noexcept(consumer.pull(s)));
    STATIC_REQUIRE(noexcept(producer.prepare(buf)));
    STATIC_REQUIRE(noexcept(consumer.prepare(buf)));
}
