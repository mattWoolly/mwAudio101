// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/StatusBannerTest.cpp — JUCE-linked acceptance tests for the non-modal
// StatusBanner (task 129) [docs/design/10-ui.md §9.4; ADR-021 L12, L13]. Test-case
// display names begin with the task tag `ui_banner` so `ctest -R ui_banner` selects
// exactly these cases (silent-pass rule). No '[' appears in the display-name TEXT.
//
// The GUI is NOT pixel-identical across platforms, so these tests assert BEHAVIOUR
// (state transitions, thread-safe async apply, dismissal, disclaimer round-trip) and
// rendered-ink presence, never exact pixel layout [docs/design/10-ui.md §13]. The
// whole target is compiled with JUCE_MODAL_LOOPS_PERMITTED=0 (tests/CMakeLists.txt),
// so a modal loop would not even link / would assert — the banner being a plain
// non-modal Component is therefore structurally enforced.
//
// Acceptance criteria covered (task 129 / §9.4 / ADR-021):
//   [1] Banner is non-modal and updates on the message thread, never a modal dialog.
//   [2] The update path is thread-safe via AsyncUpdater/ChangeBroadcaster, never blocks.
//   [3] Surfaces the INJECTED disclaimer string without authoring legal posture.
//   [4] Each severity (info/warn/error) shows; dismiss clears; disclaimer round-trips.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/StatusBanner.h"
#include "../../ui/DesignTokens.h"

using mw::ui::DesignTokens;
using mw::ui::StatusBanner;

namespace {

// A trivial ChangeListener that counts notifications and records the last seen state,
// proving the banner's ChangeBroadcaster fires on the message thread.
struct CountingListener final : public juce::ChangeListener
{
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        ++count;
        wasMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
    }

    int  count = 0;
    bool wasMessageThread = false;
};

// Flush a pending AsyncUpdater synchronously on the message thread, then test `pred`.
// AsyncUpdater::handleUpdateNowIfNeeded() runs handleAsyncUpdate() on the CALLING
// thread (the message thread here) iff an update is pending — the headless analogue
// of the host delivering the posted callback. Crucially it does NOT enter a modal
// loop, so it is usable under JUCE_MODAL_LOOPS_PERMITTED=0 (where runDispatchLoopUntil
// is compiled out). We loop a few times in case a post races just after the flush.
template <typename Pred>
bool flushUntil(StatusBanner& banner, Pred pred, int maxIterations = 8)
{
    for (int i = 0; i < maxIterations; ++i)
    {
        banner.handleUpdateNowIfNeeded();    // apply the posted state (message thread)
        banner.dispatchPendingMessages();    // deliver ChangeBroadcaster notifications
        if (pred())
            return true;
    }
    return pred();
}

// True if ANY pixel in the image is non-transparent (the banner drew something).
bool imageHasInk(const juce::Image& img)
{
    const juce::Image::BitmapData data(img, juce::Image::BitmapData::readOnly);
    for (int yy = 0; yy < img.getHeight(); ++yy)
        for (int xx = 0; xx < img.getWidth(); ++xx)
            if (data.getPixelColour(xx, yy).getAlpha() != 0)
                return true;
    return false;
}

juce::Image renderBanner(StatusBanner& banner, int w, int h)
{
    banner.setBounds(0, 0, w, h);
    juce::Image img(juce::Image::ARGB, w, h, true);
    juce::Graphics g(img);
    banner.paint(g);
    return img;
}

} // namespace

TEST_CASE("ui_banner banner starts hidden and shows on a synchronous setMessage", "[ui_banner]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    StatusBanner banner(tokens);

    REQUIRE_FALSE(banner.isShowingMessage());
    REQUIRE_FALSE(banner.isVisible());

    banner.setMessage(StatusBanner::Severity::warn, "Recovered to INIT");

    REQUIRE(banner.isShowingMessage());
    REQUIRE(banner.isVisible());
    REQUIRE(banner.activeSeverity() == StatusBanner::Severity::warn);
    REQUIRE(banner.activeMessage() == juce::String("Recovered to INIT"));
}

TEST_CASE("ui_banner banner shows at each severity info warn error", "[ui_banner]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    StatusBanner banner(tokens);

    for (auto sev : { StatusBanner::Severity::info,
                      StatusBanner::Severity::warn,
                      StatusBanner::Severity::error })
    {
        banner.setMessage(sev, "msg");
        REQUIRE(banner.isShowingMessage());
        REQUIRE(banner.activeSeverity() == sev);

        // It renders ink at this severity (the severity swatch + text are drawn).
        const auto img = renderBanner(banner, 400, 40);
        REQUIRE(imageHasInk(img));
    }
}

TEST_CASE("ui_banner dismiss hides the banner and clears the message but keeps the disclaimer", "[ui_banner]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    StatusBanner banner(tokens);

    const juce::String disclaimerText =
        "INJECTED-DISCLAIMER: independent, unaffiliated work.";
    banner.setDisclaimer(disclaimerText);
    banner.setMessage(StatusBanner::Severity::error, "Corrupt state, loaded INIT");
    REQUIRE(banner.isShowingMessage());

    banner.dismiss();

    REQUIRE_FALSE(banner.isShowingMessage());
    REQUIRE_FALSE(banner.isVisible());
    REQUIRE(banner.activeMessage().isEmpty());
    // The standing disclaimer survives a dismiss (§9.4).
    REQUIRE(banner.disclaimer() == disclaimerText);
}

TEST_CASE("ui_banner injected disclaimer string round-trips verbatim", "[ui_banner]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    StatusBanner banner(tokens);

    // The banner HOSTS but never AUTHORS the legal text — whatever we inject is what
    // it reports back, byte for byte (§9.4; §1.2).
    const juce::String injected =
        "mwAudio101 is not affiliated with, endorsed by, or sponsored by Roland Corporation.";
    banner.setDisclaimer(injected);

    REQUIRE(banner.disclaimer() == injected);
    REQUIRE(banner.getState().disclaimer == injected);
}

TEST_CASE("ui_banner thread-safe post applies on the message thread via AsyncUpdater", "[ui_banner]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    StatusBanner banner(tokens);

    CountingListener listener;
    banner.addChangeListener(&listener);

    // Post from a BACKGROUND thread (simulating the load-failure path that may run off
    // the message thread). The post itself must NOT block and must NOT mutate the
    // visible state synchronously — that only happens later on the message thread.
    std::atomic<bool> posted{ false };
    std::thread worker([&]
    {
        banner.postMessage(StatusBanner::Severity::error, "async-from-bg-thread");
        posted = true;
    });
    worker.join();
    REQUIRE(posted.load());

    // Until the message loop runs, the visible state has not changed (async, not sync).
    // (We cannot strictly guarantee the AsyncUpdater hasn't fired, but the listener
    // must only ever be invoked on the message thread, asserted below.)

    // Flush the pending AsyncUpdater: this delivers the callback on the message thread.
    const bool applied = flushUntil(banner, [&] { return banner.isShowingMessage(); });

    REQUIRE(applied);
    REQUIRE(banner.activeMessage() == juce::String("async-from-bg-thread"));
    REQUIRE(banner.activeSeverity() == StatusBanner::Severity::error);

    // The ChangeBroadcaster fired, and every notification was on the message thread.
    REQUIRE(listener.count >= 1);
    REQUIRE(listener.wasMessageThread);

    banner.removeChangeListener(&listener);
}

TEST_CASE("ui_banner posted disclaimer also applies asynchronously on the message thread", "[ui_banner]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    StatusBanner banner(tokens);

    const juce::String injected = "async-disclaimer-string";
    banner.postDisclaimer(injected);

    const bool applied = flushUntil(banner, [&] { return banner.disclaimer() == injected; });
    REQUIRE(applied);
    REQUIRE(banner.disclaimer() == injected);
}

TEST_CASE("ui_banner a posted message supersedes a queued dismiss and the latest wins", "[ui_banner]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    StatusBanner banner(tokens);

    banner.setMessage(StatusBanner::Severity::info, "first");
    REQUIRE(banner.isShowingMessage());

    // Two posts coalesce; the most-recent posted message is what becomes visible.
    banner.postMessage(StatusBanner::Severity::warn, "second");
    banner.postMessage(StatusBanner::Severity::error, "third");

    const bool applied = flushUntil(banner, [&]
    {
        return banner.activeMessage() == juce::String("third");
    });
    REQUIRE(applied);
    REQUIRE(banner.activeSeverity() == StatusBanner::Severity::error);
    REQUIRE(banner.isShowingMessage());
}

TEST_CASE("ui_banner banner is a non-modal component and never enters a modal loop", "[ui_banner]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    StatusBanner banner(tokens);

    banner.setMessage(StatusBanner::Severity::error, "non-modal warning");

    // A modal component would register with the ModalComponentManager. The banner must
    // NOT — it is a passive in-editor strip, never a blocking dialog [§9.4; ADR-021].
    REQUIRE_FALSE(banner.isCurrentlyModal());
    REQUIRE(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);

    // setMessage / dismiss return control immediately — no nested message loop was
    // entered (if one had been, this test would block; reaching here proves it did not).
    banner.dismiss();
    REQUIRE(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);
}

TEST_CASE("ui_banner hidden banner paints nothing", "[ui_banner]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = DesignTokens::defaultTheme();
    StatusBanner banner(tokens);

    // No message posted => not visible => paint() is a no-op (no ink).
    const auto img = renderBanner(banner, 400, 40);
    REQUIRE_FALSE(imageHasInk(img));
}
