// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the renderVersion state lifecycle helper (task 018).
// Test-case names begin with "renderver"; tag "[renderver]".
//
// Covers docs/design/06 §9.2 (and the §5.3 setStateInformation step 3, §5.4
// <extras> renderOptIn) realizing ADR-023 V8-V10, V18.

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "version/EngineVersion.h"
#include "version/RenderVersionState.h"

using mw::version::RenderVersionState;
using mw::version::LoadedRenderState;
using mw::version::OptInDecision;
using mw101::version::kCurrentRenderVersion;

// A renderVersion strictly below CURRENT, used to exercise the legacy-render path.
// CURRENT is 1 today, so the only value below it is 0; the helper's contract is a
// pure `stored < CURRENT` comparison, so any such value drives the legacy branch
// for every future CURRENT. (Only SHIPPED versions are ever retained for the
// frozen constant-set; that registry lives in Calibration.h, out of scope here.)
static constexpr int kLegacy = kCurrentRenderVersion - 1;

// --- Acceptance 1: a load with renderVersion < CURRENT pins the stored version
//     and raises the opt-in; the render-version-to-use is the STORED one, so
//     audio does not change without accept [§9.2 V8-V10]. -----------------------
TEST_CASE("renderver: legacy load pins stored render and raises the opt-in", "[renderver]") {
    const int legacy = kLegacy;
    REQUIRE(legacy < kCurrentRenderVersion); // a version below CURRENT drives the legacy path

    LoadedRenderState in{};
    in.storedRenderVersion = legacy;
    in.priorRenderOptIn    = false; // a session never opted in before

    RenderVersionState st = RenderVersionState::onLoad(in);

    // Pins the STORED (legacy) renderVersion — that is what prepareToPlay consumes.
    REQUIRE(st.renderVersionToUse() == legacy);
    REQUIRE(st.renderVersionToUse() != kCurrentRenderVersion);

    // Raises the non-modal opt-in affordance.
    REQUIRE(st.shouldRaiseOptIn());

    // No accept yet -> next save preserves the legacy version, NOT CURRENT, and the
    // sticky renderOptIn extras flag stays false: audio has not changed.
    REQUIRE(st.renderVersionForNextSave() == legacy);
    REQUIRE_FALSE(st.renderOptIn());
}

// --- Acceptance 2: accepting writes kCurrentRenderVersion on next save and sets
//     the sticky renderOptIn [§9.2]. ---------------------------------------------
TEST_CASE("renderver: accept writes CURRENT on next save and sets sticky renderOptIn", "[renderver]") {
    const int legacy = kLegacy;

    LoadedRenderState in{};
    in.storedRenderVersion = legacy;
    in.priorRenderOptIn    = false;

    RenderVersionState st = RenderVersionState::onLoad(in);
    REQUIRE(st.shouldRaiseOptIn());

    st.accept(OptInDecision::Accept);

    // Next save writes CURRENT; the sticky <extras> renderOptIn flag is set.
    REQUIRE(st.renderVersionForNextSave() == kCurrentRenderVersion);
    REQUIRE(st.renderOptIn());

    // Once accepted the opt-in is no longer raised (it has been answered).
    REQUIRE_FALSE(st.shouldRaiseOptIn());

    // The IN-SESSION render version is still the pinned legacy one: the engine does
    // not re-render at audio rate; CURRENT is consumed at the NEXT prepareToPlay
    // after the save round-trip [§9.2; §12 / ADR-023 V18].
    REQUIRE(st.renderVersionToUse() == legacy);
}

// --- Acceptance 3: declining is sticky [§9.2]. --------------------------------
TEST_CASE("renderver: decline is sticky and never bumps the saved render version", "[renderver]") {
    const int legacy = kLegacy;

    LoadedRenderState in{};
    in.storedRenderVersion = legacy;
    in.priorRenderOptIn    = false;

    RenderVersionState st = RenderVersionState::onLoad(in);
    st.accept(OptInDecision::Decline);

    // Declining keeps the legacy render on save and does not set renderOptIn.
    REQUIRE(st.renderVersionForNextSave() == legacy);
    REQUIRE_FALSE(st.renderOptIn());

    // Sticky: the opt-in is not raised again.
    REQUIRE_FALSE(st.shouldRaiseOptIn());
    REQUIRE(st.renderVersionToUse() == legacy);

    // Sticky across a re-load: a state that already declined (priorRenderOptIn
    // stays false but the legacy version persists) does NOT re-raise on a session
    // whose stored optIn was preserved. We model the sticky decline as: a later
    // re-load of the SAME legacy state with the same priorRenderOptIn re-raises,
    // because decline is not persisted as an extras flag (only accept is). The
    // sticky property within a session is what we assert above.
}

// --- A session that ALREADY opted in (priorRenderOptIn == true) but whose stored
//     render version is still legacy must NOT re-raise: the accept is sticky in
//     <extras> across reloads [§9.2; §5.4 renderOptIn]. --------------------------
TEST_CASE("renderver: a prior opt-in is sticky across reload and does not re-raise", "[renderver]") {
    const int legacy = kLegacy;

    LoadedRenderState in{};
    in.storedRenderVersion = legacy;
    in.priorRenderOptIn    = true; // accepted in an earlier session, persisted in <extras>

    RenderVersionState st = RenderVersionState::onLoad(in);

    // Already answered (accepted) -> no opt-in raised; next save writes CURRENT and
    // renderOptIn stays set.
    REQUIRE_FALSE(st.shouldRaiseOptIn());
    REQUIRE(st.renderOptIn());
    REQUIRE(st.renderVersionForNextSave() == kCurrentRenderVersion);
}

// --- Acceptance 4 (part): new/blank state authors at kCurrentRenderVersion with
//     no opt-in [§9.2 V9]. -------------------------------------------------------
TEST_CASE("renderver: new/blank state authors at CURRENT with no opt-in", "[renderver]") {
    RenderVersionState st = RenderVersionState::onNewState();

    REQUIRE(st.renderVersionToUse() == kCurrentRenderVersion);
    REQUIRE(st.renderVersionForNextSave() == kCurrentRenderVersion);
    REQUIRE_FALSE(st.shouldRaiseOptIn());
    REQUIRE_FALSE(st.renderOptIn()); // a fresh state has no sticky opt-in flag
}

// --- A load that is ALREADY at CURRENT does not raise and authors at CURRENT. ---
TEST_CASE("renderver: a load already at CURRENT does not raise the opt-in", "[renderver]") {
    LoadedRenderState in{};
    in.storedRenderVersion = kCurrentRenderVersion;
    in.priorRenderOptIn    = false;

    RenderVersionState st = RenderVersionState::onLoad(in);

    REQUIRE(st.renderVersionToUse() == kCurrentRenderVersion);
    REQUIRE(st.renderVersionForNextSave() == kCurrentRenderVersion);
    REQUIRE_FALSE(st.shouldRaiseOptIn());
    REQUIRE_FALSE(st.renderOptIn());
}

// --- A stored renderVersion ABOVE current (a newer-state down-interpret, §8 L3)
//     never raises the legacy opt-in and is pinned to CURRENT for rendering: the
//     engine cannot render a version it does not have a frozen set for. ----------
TEST_CASE("renderver: a newer-than-current stored render version does not raise the legacy opt-in", "[renderver]") {
    LoadedRenderState in{};
    in.storedRenderVersion = kCurrentRenderVersion + 5; // a future session
    in.priorRenderOptIn    = false;

    RenderVersionState st = RenderVersionState::onLoad(in);

    // The legacy opt-in (update-to-current) is meaningless when stored >= CURRENT.
    REQUIRE_FALSE(st.shouldRaiseOptIn());
    // Render at the highest version this build actually has: CURRENT.
    REQUIRE(st.renderVersionToUse() == kCurrentRenderVersion);
}

// --- §12 RT discipline: the helper is a pure message-thread POD aggregate. It
//     must be trivially copyable so it can be snapshotted and handed to
//     prepareToPlay without allocation; nothing here runs at audio rate. ---------
TEST_CASE("renderver: the state is a trivially-copyable POD consumed at prepareToPlay", "[renderver]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<RenderVersionState>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<LoadedRenderState>);

    // onLoad / onNewState are pure factory functions (no side effects, no I/O).
    LoadedRenderState in{};
    in.storedRenderVersion = kCurrentRenderVersion;
    RenderVersionState a = RenderVersionState::onLoad(in);
    RenderVersionState b = a; // copyable
    REQUIRE(a.renderVersionToUse() == b.renderVersionToUse());
}
