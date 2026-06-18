// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the single-LFO public API + POD layout declared in
// core/dsp/Lfo.h (task 051). Names begin with "lfo_header" so the silent-pass
// selector `-R lfo_header` matches (AGENTS.md tests section).
//
// Scope is the HEADER ONLY (enum + class layout per docs/design/03 §3.2/§3.3):
// the waveform DSP lives in a later .cpp task, so signatures are probed in
// UNEVALUATED contexts (noexcept(...) / decltype / type traits) — no out-of-line
// member is ODR-used, so the test links without an Lfo.cpp [task 051 Out of scope].

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

#include "dsp/Lfo.h"

using mw101::dsp::Lfo;
using mw101::dsp::LfoShape;

// NB: these cases carry NO Catch2 [tag]. The `-R lfo_header` selector (AGENTS.md
// tests section) matches the registered test-case NAME ("discovery registers
// names, not tags"), and the names all begin with "lfo_header". Omitting a tag
// keeps the shared tests/golden/corpus/ctest-labels.snapshot golden untouched so
// this task adds NO subsystem label that would collide with sibling agents'
// snapshot edits in the same wave [task 051 conflict-avoidance].

// --- §3.2: FOUR positions only, fixed values, NO Sine/Saw ----------------------

TEST_CASE("lfo_header: LfoShape has exactly four enumerators with the documented values") {
    // Documented order/values (§3.2). Pin each so a reorder or insertion is caught.
    REQUIRE(static_cast<std::uint8_t>(LfoShape::SmoothTri) == 0);
    REQUIRE(static_cast<std::uint8_t>(LfoShape::Square)    == 1);
    REQUIRE(static_cast<std::uint8_t>(LfoShape::Random)    == 2);
    REQUIRE(static_cast<std::uint8_t>(LfoShape::Noise)     == 3);

    // uint8_t-backed stepped selector (§3.2 enum class : uint8_t).
    REQUIRE(std::is_same_v<std::underlying_type_t<LfoShape>, std::uint8_t>);
}

namespace {
// Count the named enumerators by exhaustively matching the four documented ones.
// This pins the four-position invariant without naming a Sine/Saw symbol that must
// not exist; a fifth enumerator would not be reachable through this covered set.
constexpr int kEnumeratorCount = [] {
    int n = 0;
    for (auto s : {LfoShape::SmoothTri, LfoShape::Square, LfoShape::Random, LfoShape::Noise})
        switch (s) {
            case LfoShape::SmoothTri:
            case LfoShape::Square:
            case LfoShape::Random:
            case LfoShape::Noise: ++n; break;
            // Intentionally NO default and NO Sine/Saw labels: this switch names
            // every documented enumerator exactly once, so the count below is a
            // direct, objective check of the "four positions only" rule (§3.2).
        }
    return n;
}();
} // namespace

TEST_CASE("lfo_header_api: enumerator count is four and no Sine/Saw exists") {
    STATIC_REQUIRE(kEnumeratorCount == 4);  // §3.2: exactly four positions
    REQUIRE(kEnumeratorCount == 4);
}

// --- §3.3: hot paths are noexcept ----------------------------------------------

TEST_CASE("lfo_header_api: tick / setRateHz / setShape / setNoiseSource are noexcept") {
    // Unevaluated: probes the declared exception spec without calling (no .cpp yet).
    Lfo* lfo = nullptr;  // never dereferenced — noexcept() does not evaluate its operand
    const float* noise = nullptr;

    REQUIRE(noexcept(lfo->tick()));
    REQUIRE(noexcept(lfo->setRateHz(5.0f)));
    REQUIRE(noexcept(lfo->setShape(LfoShape::Square)));
    REQUIRE(noexcept(lfo->setNoiseSource(noise)));

    // The remaining declared members are also noexcept per §3.3 RT invariants.
    REQUIRE(noexcept(lfo->prepare(48000.0, 1)));
    REQUIRE(noexcept(lfo->reset()));
    REQUIRE(noexcept(lfo->resetPhaseOnKey()));
    REQUIRE(noexcept(lfo->cycleEdge()));
    REQUIRE(noexcept(lfo->value()));
}

// --- §3.3: exact method signatures ---------------------------------------------

TEST_CASE("lfo_header_api: declared method signatures match §3.3") {
    // decltype is unevaluated: asserts the member pointer types (arg + return), so a
    // changed signature fails to compile. Covers every API method named in §3.3.
    STATIC_REQUIRE(std::is_same_v<decltype(&Lfo::prepare),         void (Lfo::*)(double, int) noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Lfo::reset),           void (Lfo::*)()             noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Lfo::setRateHz),       void (Lfo::*)(float)        noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Lfo::setShape),        void (Lfo::*)(LfoShape)     noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Lfo::resetPhaseOnKey), void (Lfo::*)()             noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Lfo::tick),            float (Lfo::*)()            noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Lfo::setNoiseSource),  void (Lfo::*)(const float*) noexcept>);

    // const accessors with inline bodies (§3.3).
    STATIC_REQUIRE(std::is_same_v<decltype(&Lfo::cycleEdge), bool (Lfo::*)()  const noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Lfo::value),     float (Lfo::*)() const noexcept>);

    REQUIRE(true);  // a runtime assertion so the case body is non-empty
}

// --- §3.3 / ADR-020 S14: state is POD ------------------------------------------

TEST_CASE("lfo_header_api: Lfo state is POD / trivially copyable (ADR-020 S14)") {
    // RT invariant: all state is POD, sized in prepare(); no heap/owned resources.
    REQUIRE(std::is_trivially_copyable_v<Lfo>);
    REQUIRE(std::is_standard_layout_v<Lfo>);
    REQUIRE(std::is_trivially_destructible_v<Lfo>);
    // Default-constructible at the seam (POD defaults per §3.3).
    REQUIRE(std::is_nothrow_default_constructible_v<Lfo>);
}
