// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-2 golden support: the offline render-input POD types — Stimulus, the
// canonical deterministic stimulus builders, the harness-side PatchSnapshot view, and
// the stable byte serialization for renderGraphHash (task 042). Test-case names begin
// with "golden" so `ctest -R golden` selects them (silent-pass rule, AGENTS.md /
// docs/design/11 §8.3). Realizes docs/design/11 §5.4 (render-input shape) and §5.3
// (renderGraphHash keying).
//
// Acceptance coverage (plan/backlog/042):
//  - each canonical stimulus builder produces IDENTICAL event sequences for a fixed
//    seed (determinism / purity) [docs/design/11 §5.4]
//  - NEGATIVE CONTROL: two different seeds produce DIFFERENT stimulus streams for the
//    randomized builder [docs/design/11 §5.4]
//  - Stimulus serializes to a STABLE byte form for inclusion in renderGraphHash
//    [docs/design/11 §5.3]
//
// Tagged [golden] (an existing snapshot tag, tests/golden/corpus/ctest-labels.snapshot
// line "[golden]"); selection is by the `golden` NAME prefix. This is OFFLINE harness
// code; RT invariants are not in play [docs/design/11 §2.2].

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../golden/Stimulus.h"

namespace {

using mw::golden::makeGateBurst;
using mw::golden::makeRandomCcSweep;
using mw::golden::makeSustainedNote;
using mw::golden::PatchParam;
using mw::golden::PatchSnapshot;
using mw::golden::renderGraphHashContribution;
using mw::golden::serialize;
using mw::golden::StimEvent;
using mw::golden::StimEventKind;
using mw::golden::Stimulus;

// True iff `events` is sorted ascending by sampleOffset (the seam's MidiEventView
// ordering contract, docs/design/00 §5.3).
bool offsetsAscending(const std::vector<StimEvent>& events) noexcept {
    for (std::size_t i = 1; i < events.size(); ++i)
        if (events[i].sampleOffset < events[i - 1].sampleOffset) return false;
    return true;
}

} // namespace

// === Determinism: every builder is a pure function of its inputs ===================

TEST_CASE("golden: sustained-note builder produces identical event sequences for "
          "fixed args [docs/design/11 5.4]",
          "[golden]") {
    const Stimulus a = makeSustainedNote();
    const Stimulus b = makeSustainedNote();
    REQUIRE(a == b);                       // identical inputs -> identical Stimulus

    // Shape sanity (paired positive control — a stub returning {} would fail these):
    REQUIRE(a.events.size() == 2);
    REQUIRE(a.events[0].kind == StimEventKind::NoteOn);
    REQUIRE(a.events[1].kind == StimEventKind::NoteOff);
    REQUIRE(a.events[0].sampleOffset < a.events[1].sampleOffset);  // off after on
    REQUIRE(a.events[0].value > 0.0f);                             // a real velocity
    REQUIRE(offsetsAscending(a.events));
    REQUIRE(a.durationFrames > 0);
}

TEST_CASE("golden: gate-burst builder produces identical event sequences for fixed "
          "args [docs/design/11 5.4]",
          "[golden]") {
    const Stimulus a = makeGateBurst();
    const Stimulus b = makeGateBurst();
    REQUIRE(a == b);

    // One on/off pair per pulse, strictly increasing offsets, gate toggles correctly.
    REQUIRE(a.events.size() % 2 == 0);
    REQUIRE(a.events.size() >= 4);                 // a real burst, not a single note
    for (std::size_t i = 0; i < a.events.size(); i += 2) {
        REQUIRE(a.events[i].kind == StimEventKind::NoteOn);
        REQUIRE(a.events[i + 1].kind == StimEventKind::NoteOff);
        REQUIRE(a.events[i].sampleOffset < a.events[i + 1].sampleOffset);
    }
    REQUIRE(offsetsAscending(a.events));
    REQUIRE(a.durationFrames > 0);
}

TEST_CASE("golden: randomized sweep builder is deterministic for a FIXED seed "
          "[docs/design/11 5.4]",
          "[golden]") {
    const std::uint64_t seed = 0xC0FFEEull;
    const Stimulus a = makeRandomCcSweep(seed);
    const Stimulus b = makeRandomCcSweep(seed);
    REQUIRE(a == b);                       // same seed -> byte-identical stream

    // Bracketed by a gate, with CC events carrying real (randomized) values in [0,1).
    REQUIRE(a.events.front().kind == StimEventKind::NoteOn);
    REQUIRE(a.events.back().kind == StimEventKind::NoteOff);
    int ccCount = 0;
    for (const auto& e : a.events) {
        if (e.kind == StimEventKind::ControlChange) {
            ++ccCount;
            REQUIRE(e.value >= 0.0f);
            REQUIRE(e.value < 1.0f);
        }
    }
    REQUIRE(ccCount > 0);
    REQUIRE(offsetsAscending(a.events));
    REQUIRE(a.seed == seed);
}

// === Negative control: different seeds -> different randomized streams =============

TEST_CASE("golden: randomized sweep produces DIFFERENT streams for DIFFERENT seeds "
          "(negative control) [docs/design/11 5.4]",
          "[golden]") {
    const Stimulus a = makeRandomCcSweep(/*seed=*/1ull);
    const Stimulus b = makeRandomCcSweep(/*seed=*/2ull);

    // Same shape (same event count / offsets), but the CC value axis must diverge.
    REQUIRE(a.events.size() == b.events.size());
    REQUIRE_FALSE(a == b);

    bool anyCcDiffers = false;
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        if (a.events[i].kind == StimEventKind::ControlChange
            && a.events[i].value != b.events[i].value) {
            anyCcDiffers = true;
            break;
        }
    }
    REQUIRE(anyCcDiffers);

    // And the serialized byte form / hash diverge too (the property the harness keys on).
    REQUIRE(serialize(a) != serialize(b));
    REQUIRE(renderGraphHashContribution(a) != renderGraphHashContribution(b));
}

// === Stable byte serialization for renderGraphHash (docs/design/11 §5.3) ===========

TEST_CASE("golden: Stimulus serializes to a STABLE byte form across runs "
          "[docs/design/11 5.3]",
          "[golden]") {
    const Stimulus s = makeSustainedNote();
    const std::vector<std::byte> b1 = serialize(s);
    const std::vector<std::byte> b2 = serialize(s);
    REQUIRE(b1 == b2);                                   // deterministic
    REQUIRE_FALSE(b1.empty());                           // not a no-op stub

    // The hash contribution is likewise stable run-to-run.
    REQUIRE(renderGraphHashContribution(s) == renderGraphHashContribution(s));

    // A freshly built equivalent serializes identically (no hidden/address state).
    const Stimulus sCopy = makeSustainedNote();
    REQUIRE(serialize(sCopy) == b1);
}

TEST_CASE("golden: Stimulus serialization CHANGES when ANY field changes (negative "
          "controls) [docs/design/11 5.3]",
          "[golden]") {
    const Stimulus base = makeSustainedNote();
    const std::uint64_t h0 = renderGraphHashContribution(base);

    SECTION("durationFrames") {
        Stimulus s = base;
        s.durationFrames += 1;
        REQUIRE(serialize(s) != serialize(base));
        REQUIRE(renderGraphHashContribution(s) != h0);
    }
    SECTION("seed") {
        Stimulus s = base;
        s.seed ^= 0x1ull;
        REQUIRE(serialize(s) != serialize(base));
        REQUIRE(renderGraphHashContribution(s) != h0);
    }
    SECTION("event note") {
        Stimulus s = base;
        s.events[0].note = static_cast<std::int16_t>(s.events[0].note + 1);
        REQUIRE(serialize(s) != serialize(base));
        REQUIRE(renderGraphHashContribution(s) != h0);
    }
    SECTION("event value") {
        Stimulus s = base;
        s.events[0].value += 0.01f;
        REQUIRE(serialize(s) != serialize(base));
        REQUIRE(renderGraphHashContribution(s) != h0);
    }
    SECTION("event kind") {
        Stimulus s = base;
        s.events[0].kind = StimEventKind::ControlChange;
        REQUIRE(serialize(s) != serialize(base));
        REQUIRE(renderGraphHashContribution(s) != h0);
    }
    SECTION("event sampleOffset") {
        Stimulus s = base;
        s.events[1].sampleOffset += 1;
        REQUIRE(serialize(s) != serialize(base));
        REQUIRE(renderGraphHashContribution(s) != h0);
    }
    SECTION("adding an event") {
        Stimulus s = base;
        s.events.push_back(StimEvent{});
        REQUIRE(serialize(s) != serialize(base));
        REQUIRE(renderGraphHashContribution(s) != h0);
    }
}

TEST_CASE("golden: distinct canonical builders yield distinct renderGraphHash "
          "contributions [docs/design/11 5.3/5.4]",
          "[golden]") {
    const std::uint64_t hSustain = renderGraphHashContribution(makeSustainedNote());
    const std::uint64_t hBurst   = renderGraphHashContribution(makeGateBurst());
    const std::uint64_t hSweep   = renderGraphHashContribution(makeRandomCcSweep(7ull));
    REQUIRE(hSustain != hBurst);
    REQUIRE(hSustain != hSweep);
    REQUIRE(hBurst   != hSweep);
}

// === Harness-side PatchSnapshot view (references core ParamIDs) ====================

TEST_CASE("golden: PatchSnapshot is a plain-data overlay of core mw101.* param IDs "
          "[docs/design/11 5.4]",
          "[golden]") {
    PatchSnapshot patch{};
    patch.renderVersion = 1;
    patch.params.push_back(PatchParam{ mw::params::ids::kVcfCutoff, 0.5f });
    patch.params.push_back(PatchParam{ mw::params::ids::kVcfResonance, 0.9f });
    patch.params.push_back(PatchParam{ mw::params::ids::kEnvAttack, 0.0f });

    // It references the canonical IDs verbatim (never mints its own), and is value-
    // comparable (a POD overlay, mirroring InitPatch's PatchValue shape).
    REQUIRE(patch.params.size() == 3);
    REQUIRE(patch.params[0].id == std::string_view{ "mw101.vcf.cutoff" });
    REQUIRE(patch.params[1].value == 0.9f);

    PatchSnapshot same = patch;
    REQUIRE(same == patch);                 // value equality (positive control)

    PatchSnapshot diff = patch;
    diff.params[1].value = 0.1f;
    REQUIRE_FALSE(diff == patch);           // a value change is observable

    PatchSnapshot diffVer = patch;
    diffVer.renderVersion = 2;
    REQUIRE_FALSE(diffVer == patch);        // renderVersion participates in identity
}
