// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/version/RenderVersionState.h — the state-resident renderVersion lifecycle
// helper (task 018).
//
// A JUCE-free, message-thread POD helper that decides, from a loaded state's stored
// renderVersion vs kCurrentRenderVersion, (a) which renderVersion the engine should
// render at (consumed once at prepareToPlay, NEVER at audio rate), (b) whether to
// raise the non-modal "update engine (audio will change)" opt-in, and (c) how the
// sticky <extras> renderOptIn flag and the on-save renderVersion are written back.
//
// Realizes docs/design/06 §9.2 (and the §5.3 setStateInformation step 3 / §5.4
// <extras> renderOptIn), ADR-023 V8-V10, V18; threading per docs/design/00 §6.4
// (renderVersion read/write + frozen-constant-set selection on the message thread /
// at prepareToPlay only — never the audio thread).
//
// This module owns ONLY the state-resident lifecycle. The actual legacy DSP
// constant-set selection (the frozen-constant-set registry lives in
// core/calibration/Calibration.h), the UI opt-in dialog, and the bless-tool /
// MANIFEST governance are out of scope and owned by their own streams.

#pragma once

#include "EngineVersion.h"

namespace mw::version {

// The user's answer to the non-modal "update engine (audio will change)" opt-in.
// Pending == not yet answered (the affordance is still raised) [§9.2; ADR-023 V8].
enum class OptInDecision : unsigned char {
    Pending = 0,
    Accept,   // -> write kCurrentRenderVersion on next save + set sticky renderOptIn
    Decline,  // -> sticky: keep the legacy render, do not bump on save
};

// The inputs read from a just-loaded canonical state on the message thread
// [docs/design/06 §5.3 step 3 / §5.4]. POD aggregate — no allocation, no JUCE.
struct LoadedRenderState {
    // The renderVersion attribute serialized in the loaded state root [§5.1].
    int  storedRenderVersion = mw101::version::kCurrentRenderVersion;
    // The sticky <extras> renderOptIn flag persisted from an earlier "accept"
    // [§5.4]. True means a prior session already opted in to CURRENT.
    bool priorRenderOptIn = false;
};

// The decided lifecycle state, snapshotted on the message thread and handed to
// prepareToPlay. Trivially copyable POD so it can be passed by value without
// allocation; nothing here runs at audio rate [docs/design/00 §6.4; §12].
class RenderVersionState {
public:
    // New/blank session (and new factory presets) author at kCurrentRenderVersion
    // with no opt-in raised [§9.2 V9].
    static constexpr RenderVersionState onNewState() noexcept {
        RenderVersionState s{};
        s.pinnedRenderVersion_ = mw101::version::kCurrentRenderVersion;
        s.optedIn_             = false;
        s.decision_            = OptInDecision::Pending; // never raised: stored == CURRENT
        return s;
    }

    // Decide the lifecycle from a loaded state [§9.2 / §5.3 step 3; ADR-023 V8-V10].
    //
    //   stored <  CURRENT  &&  !priorOptIn  -> pin STORED, raise the opt-in.
    //   stored <  CURRENT  &&   priorOptIn  -> already accepted in a past session:
    //                                          sticky, no re-raise, save at CURRENT.
    //   stored >= CURRENT                    -> render at CURRENT (this build cannot
    //                                          render a version it lacks a frozen set
    //                                          for), never raise the legacy opt-in.
    static constexpr RenderVersionState onLoad(const LoadedRenderState& in) noexcept {
        RenderVersionState s{};
        s.optedIn_ = in.priorRenderOptIn;

        if (in.storedRenderVersion < mw101::version::kCurrentRenderVersion) {
            // Legacy session: keep rendering at the STORED version (legacy-render
            // path; the frozen constant-set is selected at prepareToPlay, never at
            // audio rate) [§9.2; ADR-023 V10, V18].
            s.pinnedRenderVersion_ = in.storedRenderVersion;
            // A prior accept is sticky and pre-answers the opt-in; otherwise it is
            // raised and Pending until the user answers this session.
            s.decision_ = in.priorRenderOptIn ? OptInDecision::Accept
                                              : OptInDecision::Pending;
        } else {
            // At (or beyond) CURRENT: render at CURRENT; no legacy opt-in is
            // meaningful. clamp() folds a future stored version down to CURRENT.
            s.pinnedRenderVersion_ = mw101::version::kCurrentRenderVersion;
            s.decision_            = OptInDecision::Pending;
        }
        return s;
    }

    // Record the user's answer to a raised opt-in (message thread). Accepting sets
    // the sticky renderOptIn and arms the on-save write of kCurrentRenderVersion;
    // declining is sticky and leaves the legacy render in place [§9.2].
    constexpr void accept(OptInDecision decision) noexcept {
        // Only meaningful while the opt-in was actually raised (pending on a legacy
        // load). A no-op otherwise keeps the helper idempotent.
        if (decision == OptInDecision::Accept) {
            decision_ = OptInDecision::Accept;
            optedIn_  = true;
        } else if (decision == OptInDecision::Decline) {
            decision_ = OptInDecision::Decline;
            // optedIn_ stays as-is (a prior accept is never un-stuck by this path
            // because a prior-accept load never raises the opt-in to decline).
        }
    }

    // The renderVersion the ENGINE renders at, consumed once at prepareToPlay —
    // never at audio rate [§9.2; §12 / ADR-023 V18]. For a legacy session this is
    // the pinned STORED version until a save+reload round-trip after accept.
    constexpr int renderVersionToUse() const noexcept { return pinnedRenderVersion_; }

    // Whether the non-modal opt-in affordance should be raised (legacy load not yet
    // answered) [§9.2; ADR-023 V8].
    constexpr bool shouldRaiseOptIn() const noexcept {
        return pinnedRenderVersion_ < mw101::version::kCurrentRenderVersion
            && decision_ == OptInDecision::Pending;
    }

    // The sticky <extras> renderOptIn flag to persist [§5.4; §9.2].
    constexpr bool renderOptIn() const noexcept { return optedIn_; }

    // The renderVersion to WRITE on the next save. Accepting (now or in a prior
    // session) writes kCurrentRenderVersion; pending/decline keeps the pinned
    // (legacy) version so a session never silently changes its audio [§9.2].
    constexpr int renderVersionForNextSave() const noexcept {
        return optedIn_ ? mw101::version::kCurrentRenderVersion : pinnedRenderVersion_;
    }

private:
    int           pinnedRenderVersion_ = mw101::version::kCurrentRenderVersion;
    bool          optedIn_             = false;
    OptInDecision decision_            = OptInDecision::Pending;
};

} // namespace mw::version
