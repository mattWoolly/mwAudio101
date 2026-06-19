// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/version/RenderVersionState.cpp — translation-unit anchor + compile-time
// self-checks for the renderVersion state lifecycle helper (task 018).
//
// The lifecycle logic is constexpr and lives in the header so prepareToPlay can
// fold it at compile time; this TU exists so the module has a real object file in
// mwcore and so the §9.2 invariants are asserted at build time, not only in tests.
// Realizes docs/design/06 §9.2 and ADR-023 V8-V10, V18.

#include "RenderVersionState.h"

namespace mw::version {
namespace {

// A new/blank session authors at CURRENT with no opt-in [§9.2 V9].
static_assert(RenderVersionState::onNewState().renderVersionToUse()
                  == mw101::version::kCurrentRenderVersion,
              "RenderVersionState: new state must author at kCurrentRenderVersion [§9.2 V9].");
static_assert(!RenderVersionState::onNewState().shouldRaiseOptIn(),
              "RenderVersionState: new state must not raise the opt-in [§9.2 V9].");
static_assert(!RenderVersionState::onNewState().renderOptIn(),
              "RenderVersionState: new state has no sticky renderOptIn [§9.2 V9].");

// A load already at CURRENT renders at CURRENT and never raises [§9.2].
static_assert(RenderVersionState::onLoad(LoadedRenderState{
                  /*storedRenderVersion=*/mw101::version::kCurrentRenderVersion,
                  /*priorRenderOptIn=*/false})
                      .renderVersionToUse()
                  == mw101::version::kCurrentRenderVersion,
              "RenderVersionState: a CURRENT load renders at CURRENT [§9.2].");
static_assert(!RenderVersionState::onLoad(LoadedRenderState{
                  mw101::version::kCurrentRenderVersion, false})
                   .shouldRaiseOptIn(),
              "RenderVersionState: a CURRENT load must not raise the opt-in [§9.2].");

} // namespace
} // namespace mw::version
