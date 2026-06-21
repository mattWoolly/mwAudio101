// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/Signposts.cpp — implementation of the two non-modal honesty signposts
// declared in ui/Signposts.h [docs/design/09 §5; docs/design/00 §8.5;
// ADR-012 §Consequences; ADR-023 V16]. The component composes the reused StatusBanner
// (129) and decides WHICH static notice (if any) to surface; the only state change sent
// to the banner goes through its thread-safe AsyncUpdater post path (or its
// message-thread dismiss seam), so the visible mutation always lands on the MESSAGE
// THREAD and nothing here ever enters a modal loop [§9.4; ADR-021 L12, L13].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/Signposts.h and is reached
// by relative include — no shared CMakeLists edit (mirrors plugin/ui/StatusBanner.cpp).

#include "../../ui/Signposts.h"

#include <cmath>

namespace mw::ui {

namespace cal = mw::cal::ui::signpost;

Signposts::Signposts(const DesignTokens& tokens)
    : banner_(tokens)
{
    // The signposts component is a transparent container: it paints nothing of its own,
    // it composes the StatusBanner surface. Add the banner as a child so 114c places one
    // component and gets the banner + both signposts together [§9.4 reuse].
    addAndMakeVisible(banner_);
    setInterceptsMouseClicks(false, true);  // clicks fall through to the banner's "x"

    // Listen to the banner so a USER dismissal ("x") is latched here too.
    banner_.addChangeListener(this);
}

Signposts::~Signposts()
{
    banner_.removeChangeListener(this);
}

// ---------------------------------------------------------------------------
// Inputs (message thread).
// ---------------------------------------------------------------------------
void Signposts::setProvenance(const mw::version::RenderProvenance& provenance)
{
    provenance_      = provenance;
    provenanceKnown_ = true;   // a real provenance has now been published (not the default)
    recomputeConditions();
    update();
}

void Signposts::setTuningA4Hz(float a4Hz)
{
    a4Hz_ = a4Hz;
    recomputeConditions();
    update();
}

void Signposts::recomputeConditions() noexcept
{
    const bool prevUnblessed = unblessedRate_;
    const bool prevTuning    = tuningOff440_;

    // (1) Unblessed-rate condition: the host SR is NOT in the blessed {44.1/48/88.2/96k}
    // set, OR a >1x oversampling request was clamped to 1x at OS_CEILING. Both facts come
    // from the published RenderProvenance — we never recompute the policy here, we read
    // the engine's verdict [docs/design/00 §8.5; ADR-023 V15/V16]. Until a REAL provenance
    // has been published (the default-constructed POD reads as hostFs=0/unblessed), the
    // condition is held false so an un-prepared editor does not flash the notice.
    unblessedRate_ = provenanceKnown_
        && ((! provenance_.blessedSampleRate) || provenance_.oversampleClampedToEco);

    // (2) Tuning-duality condition: A4 is off the 440 default by more than the (PI)
    // epsilon — i.e. a non-default reference such as the recalled 442 Hz hardware-accurate
    // preset. A user who never touches tuning (A4 == 440) is not nagged [docs/design/09
    // §5; ADR-012 §Consequences C21/C22].
    tuningOff440_ = std::abs(a4Hz_ - cal::kDefaultA4Hz) > cal::kA4DefaultEpsilonHz;

    // A condition that has just RE-ASSERTED (false -> true edge) clears any stale
    // dismissal latched against it, so an honestly recurring condition can re-appear after
    // the user dismissed an earlier instance.
    if (unblessedRate_ && ! prevUnblessed && dismissed_ == Active::unblessedRate)
        dismissed_ = Active::none;
    if (tuningOff440_ && ! prevTuning && dismissed_ == Active::tuningDuality)
        dismissed_ = Active::none;

    // A condition that has cleared (true -> false) also clears its dismissal latch — there
    // is nothing left to suppress, and a later re-assert should surface fresh.
    if (! unblessedRate_ && dismissed_ == Active::unblessedRate)
        dismissed_ = Active::none;
    if (! tuningOff440_ && dismissed_ == Active::tuningDuality)
        dismissed_ = Active::none;
}

// ---------------------------------------------------------------------------
// Recompute the active notice + drive the banner (message thread).
// ---------------------------------------------------------------------------
void Signposts::update()
{
    // Coalesce to the more-urgent notice when both conditions hold: unblessed-rate (warn)
    // outranks the tuning note (info), mirroring the StatusBanner severity ordering
    // [ADR-021 L12]. A condition the user has dismissed is skipped until it edges.
    Active desired = Active::none;
    if (unblessedRate_ && dismissed_ != Active::unblessedRate)
        desired = Active::unblessedRate;
    else if (tuningOff440_ && dismissed_ != Active::tuningDuality)
        desired = Active::tuningDuality;

    if (desired == active_)
        return;  // idempotent: a repeated poll with no change must not thrash the banner

    active_ = desired;

    switch (desired)
    {
        case Active::unblessedRate:
            // "Running unblessed at this host rate." — the §8.5 / V16 normative phrase.
            banner_.postMessage(StatusBanner::Severity::warn,
                                juce::String::fromUTF8(cal::kUnblessedRateNotice));
            break;

        case Active::tuningDuality:
            // The 440-vs-442 honesty note so users do not mistrust the pitch.
            banner_.postMessage(StatusBanner::Severity::info,
                                juce::String::fromUTF8(cal::kTuningDualityNotice));
            break;

        case Active::none:
            // No condition holds (or the active notice was dismissed): hide the banner via
            // its own message-thread dismiss seam (update() runs on the message thread).
            if (banner_.isShowingMessage())
                banner_.dismiss();
            break;
    }
}

// ---------------------------------------------------------------------------
// Banner change notifications (message thread). Detect a USER dismissal.
// ---------------------------------------------------------------------------
void Signposts::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source != &banner_)
        return;

    // If WE intended a notice to be visible but the banner is now hidden, the user clicked
    // the "x": latch the dismissal so the next update() does not immediately re-show this
    // standing notice. We do NOT post anything back to the banner here (no feedback loop).
    if (active_ != Active::none && ! banner_.isShowingMessage())
    {
        dismissed_ = active_;
        active_    = Active::none;
    }
}

// ---------------------------------------------------------------------------
// Reduce-motion + reskin (message thread).
// ---------------------------------------------------------------------------
void Signposts::setReduceMotion(bool reduceMotion)
{
    // The signpost carries no animation of its own; the preference is latched + forwarded
    // so the composed surface respects the same motion setting as the rest of the UI. No
    // binding is affected [§10; ADR-015 C8].
    reduceMotion_ = reduceMotion;
}

void Signposts::setTokens(const DesignTokens& tokens)
{
    banner_.setTokens(tokens);
}

// ---------------------------------------------------------------------------
// juce::Component — the signposts area is the banner's area (it fills the bounds).
// ---------------------------------------------------------------------------
void Signposts::resized()
{
    banner_.setBounds(getLocalBounds());
}

} // namespace mw::ui
