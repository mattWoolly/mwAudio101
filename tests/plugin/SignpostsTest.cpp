// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/SignpostsTest.cpp — JUCE-linked Catch2 tests for the two non-modal
// honesty signposts (task 129b; tag ui_signpost) [docs/design/09 §5; docs/design/00
// §8.5; ADR-012 §Consequences; ADR-023 V16].
//
// These exercise the mw::ui::Signposts component (composed over the reused StatusBanner)
// headlessly under a juce::ScopedJuceInitialiser_GUI (mirrors PluginHarnessTest.cpp):
//   • a 442-active (off-440) A4 -> the tuning-duality signpost shows (info);
//   • an unblessed host rate OR an OS-clamp provenance -> the unblessed-rate banner shows
//     (warn, the §8.5 / V16 normative phrase);
//   • a blessed rate + 440 default -> neither signpost shows (hidden);
//   • dismissal: a dismissed standing notice stays hidden until its condition edges;
//   • reduce-motion is latched/forwarded; no modal loop is ever entered.
//
// The Signposts updates the banner via the StatusBanner thread-safe AsyncUpdater post
// path; on the message thread the visible mutation lands in handleAsyncUpdate(), so the
// tests pump the dispatch loop briefly to apply the posted change (the editor's Timer
// runs on a live message loop in production).

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/Signposts.h"
#include "../../ui/DesignTokens.h"

#include "version/RenderProvenance.h"
#include "calibration/SignpostConstants.h"
#include "calibration/GoldenKeyConstants.h"

using mw::ui::Signposts;
using mw::ui::StatusBanner;
using mw::ui::DesignTokens;
using mw::version::RenderProvenance;
using mw::version::captureRenderProvenance;

namespace {

// Apply any pending AsyncUpdater callback on the hosted banner synchronously. The
// StatusBanner's post path coalesces into a juce::AsyncUpdater that would fire on a live
// message loop in production; this build forbids modal/dispatch loops in tests
// (JUCE_MODAL_LOOPS_PERMITTED off), so we flush the pending callback directly via the
// AsyncUpdater seam, message-loop-free. This still runs the SAME handleAsyncUpdate() on
// the calling (message) thread, exactly as production would.
void flushBanner(Signposts& s)
{
    // (1) Apply a pending posted message (AsyncUpdater seam).
    s.banner().handleUpdateNowIfNeeded();
    // (2) Deliver any pending change notification synchronously to the Signposts'
    // ChangeListener (the banner's ChangeBroadcaster is async by default; this is the
    // message-loop-free flush of that callback).
    s.banner().dispatchPendingMessages();
}

constexpr int kCurrentVersion = mw101::version::kCurrentRenderVersion;

} // namespace

TEST_CASE("ui_signpost: a 442-active tuning A4 surfaces the tuning-duality signpost", "[ui_signpost]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    Signposts signposts(tokens);

    // 442 Hz (the recalled hardware-accurate value) is off the 440 default.
    signposts.setTuningA4Hz(mw::cal::ui::signpost::kHardwareA4Hz);
    flushBanner(signposts);

    REQUIRE(signposts.tuningDualityCondition());
    REQUIRE(signposts.activeSignpost() == Signposts::Active::tuningDuality);

    const auto state = signposts.banner().getState();
    REQUIRE(state.visible);
    REQUIRE(state.severity == StatusBanner::Severity::info);
    REQUIRE(state.message.isNotEmpty());
}

TEST_CASE("ui_signpost: an above-blessed host rate surfaces the unblessed-rate banner", "[ui_signpost]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    Signposts signposts(tokens);

    // 176.4 kHz is supported-but-unblessed (above the {44.1/48/88.2/96k} set) [ADR-023 V14].
    const RenderProvenance prov = captureRenderProvenance(kCurrentVersion, 176400.0, /*factor*/ 2);
    REQUIRE_FALSE(prov.blessedSampleRate);

    signposts.setProvenance(prov);
    flushBanner(signposts);

    REQUIRE(signposts.unblessedRateCondition());
    REQUIRE(signposts.activeSignpost() == Signposts::Active::unblessedRate);

    const auto state = signposts.banner().getState();
    REQUIRE(state.visible);
    REQUIRE(state.severity == StatusBanner::Severity::warn);
    REQUIRE(state.message == juce::String::fromUTF8(mw::cal::ui::signpost::kUnblessedRateNotice));
}

TEST_CASE("ui_signpost: an OS-clamp-to-1x provenance surfaces the unblessed-rate banner", "[ui_signpost]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    Signposts signposts(tokens);

    // 96 kHz IS in the blessed set, but a 2x request would push the internal rate to
    // 192 kHz which does NOT exceed OS_CEILING; pick a higher blessed-set-membership case:
    // at 96 kHz a 4x request would exceed the 192 kHz ceiling and clamp to 1x. Use the
    // provenance capture to derive the clamp exactly (single source of truth).
    const RenderProvenance prov = captureRenderProvenance(kCurrentVersion, 96000.0, /*factor*/ 4);
    REQUIRE(prov.blessedSampleRate);          // the rate itself is blessed ...
    REQUIRE(prov.oversampleClampedToEco);     // ... but the OS factor was clamped to 1x

    signposts.setProvenance(prov);
    flushBanner(signposts);

    REQUIRE(signposts.unblessedRateCondition());
    REQUIRE(signposts.activeSignpost() == Signposts::Active::unblessedRate);
    REQUIRE(signposts.banner().getState().visible);
    REQUIRE(signposts.banner().getState().severity == StatusBanner::Severity::warn);
}

TEST_CASE("ui_signpost: a blessed rate + 440 default shows neither signpost", "[ui_signpost]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    Signposts signposts(tokens);

    // 48 kHz blessed, 2x request (no clamp), A4 at the 440 default.
    const RenderProvenance prov = captureRenderProvenance(kCurrentVersion, 48000.0, /*factor*/ 2);
    REQUIRE(prov.blessedSampleRate);
    REQUIRE_FALSE(prov.oversampleClampedToEco);

    signposts.setProvenance(prov);
    signposts.setTuningA4Hz(mw::cal::ui::signpost::kDefaultA4Hz);  // 440
    flushBanner(signposts);

    REQUIRE_FALSE(signposts.tuningDualityCondition());
    REQUIRE_FALSE(signposts.unblessedRateCondition());
    REQUIRE(signposts.activeSignpost() == Signposts::Active::none);
    REQUIRE_FALSE(signposts.banner().getState().visible);
}

TEST_CASE("ui_signpost: unblessed-rate (warn) outranks the tuning note (info) when both hold", "[ui_signpost]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    Signposts signposts(tokens);

    signposts.setTuningA4Hz(442.0f);  // tuning condition holds
    const RenderProvenance prov = captureRenderProvenance(kCurrentVersion, 176400.0, 2);  // unblessed
    signposts.setProvenance(prov);
    flushBanner(signposts);

    REQUIRE(signposts.tuningDualityCondition());
    REQUIRE(signposts.unblessedRateCondition());
    // The more-urgent notice wins.
    REQUIRE(signposts.activeSignpost() == Signposts::Active::unblessedRate);
    REQUIRE(signposts.banner().getState().severity == StatusBanner::Severity::warn);
}

TEST_CASE("ui_signpost: a dismissed standing notice stays hidden until its condition edges", "[ui_signpost]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    Signposts signposts(tokens);

    signposts.setTuningA4Hz(442.0f);
    flushBanner(signposts);
    REQUIRE(signposts.banner().getState().visible);
    REQUIRE(signposts.activeSignpost() == Signposts::Active::tuningDuality);

    // User dismisses via the banner's own message-thread dismiss seam.
    signposts.banner().dismiss();
    flushBanner(signposts);
    REQUIRE_FALSE(signposts.banner().getState().visible);
    // The Signposts ChangeListener latched the dismissal.
    REQUIRE(signposts.activeSignpost() == Signposts::Active::none);

    // A repeated poll with the SAME (still-off-440) tuning must NOT re-show it.
    signposts.update();
    flushBanner(signposts);
    REQUIRE_FALSE(signposts.banner().getState().visible);
    REQUIRE(signposts.activeSignpost() == Signposts::Active::none);

    // Return to 440 (condition clears), then go off-440 again (a fresh edge): re-shows.
    signposts.setTuningA4Hz(mw::cal::ui::signpost::kDefaultA4Hz);
    flushBanner(signposts);
    REQUIRE_FALSE(signposts.banner().getState().visible);

    signposts.setTuningA4Hz(442.0f);
    flushBanner(signposts);
    REQUIRE(signposts.banner().getState().visible);
    REQUIRE(signposts.activeSignpost() == Signposts::Active::tuningDuality);
}

TEST_CASE("ui_signpost: reduce-motion is latched/forwarded and nothing is modal", "[ui_signpost]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    Signposts signposts(tokens);

    REQUIRE_FALSE(signposts.isReduceMotion());
    signposts.setReduceMotion(true);
    REQUIRE(signposts.isReduceMotion());
    signposts.setReduceMotion(false);
    REQUIRE_FALSE(signposts.isReduceMotion());

    // Non-modal proof: showing a signpost must not enter a modal loop, so the test thread
    // returns immediately and no modal component is active.
    signposts.setTuningA4Hz(442.0f);
    flushBanner(signposts);
    REQUIRE(juce::Component::getCurrentlyModalComponent() == nullptr);
    REQUIRE(signposts.banner().getState().visible);
}

TEST_CASE("ui_signpost: the banner is reskinnable via setTokens and renders into an image", "[ui_signpost]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    auto tokens = DesignTokens::defaultTheme();
    Signposts signposts(tokens);

    signposts.setSize(360, 36);
    signposts.setTuningA4Hz(442.0f);
    flushBanner(signposts);
    REQUIRE(signposts.banner().getState().visible);

    // Draw into an offscreen image (no window / no modal): a non-trivial paint produces
    // some non-background pixels.
    juce::Image img(juce::Image::ARGB, 360, 36, true);
    {
        juce::Graphics g(img);
        signposts.banner().paintEntireComponent(g, false);
    }

    bool anyPainted = false;
    for (int y = 0; y < img.getHeight() && ! anyPainted; ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (img.getPixelAt(x, y).getAlpha() != 0)
            {
                anyPainted = true;
                break;
            }
    REQUIRE(anyPainted);

    // A live reskin must not throw / must keep the surface valid.
    signposts.setTokens(DesignTokens::highContrast());
    flushBanner(signposts);
    REQUIRE(signposts.banner().getState().visible);
}
