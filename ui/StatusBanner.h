// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/StatusBanner.h — the NON-MODAL load-failure / disclaimer surface
// [docs/design/10-ui.md §9.4; ADR-021 L12]. The processor's graded load-failure
// recovery path (ADR-021) drives this banner via a thread-safe juce::AsyncUpdater so
// the actual UI mutation always lands on the MESSAGE THREAD, and the banner re-emits
// the change to its own listeners as a juce::ChangeBroadcaster. It is a plain
// juce::Component — it NEVER enters a modal loop and NEVER blocks the host's load
// path (a modal dialog from a plugin during host project load can deadlock the host)
// [ADR-021 Decision rule 6, L12].
//
// SCOPE (task 129 — the SURFACE only, §9.4):
//   • A non-modal, dismissible strip with three severity levels (info / warn / error).
//   • A thread-safe update path: postMessage()/postDisclaimer() may be CALLED from
//     any thread; the visible state only changes inside handleAsyncUpdate() on the
//     message thread, and a juce::ChangeBroadcaster notifies listeners there
//     [§9.4; ADR-021 L12, L13].
//   • Dismissal: dismiss() hides the banner and clears the active message; the hosted
//     disclaimer string is retained (it is a standing notice, not a transient
//     warning) and can be re-shown.
//   • HOSTS — does not author — the static non-affiliation disclaimer string; the
//     caller injects it via setDisclaimer() [§9.4; §1.2 legal text owned elsewhere].
//
// OUT OF SCOPE (deliberately NOT owned here): the load-failure runtime decision /
// classification logic (ADR-021 owner) and the legal/trademark TEXT itself (the
// naming/legal track). This component only renders whatever string it is handed.
//
// BUILD WIRING: this header lives at the design-faithful path ui/; its implementation
// lives at plugin/ui/StatusBanner.cpp so the plugin glob compiles it into the plugin
// target + mw101_plugin_tests (CONFIGURE_DEPENDS) — no shared CMakeLists edit
// (mirrors plugin/ui/MwAudioLookAndFeel.cpp).

#pragma once

#include <atomic>
#include <mutex>

#include <juce_gui_basics/juce_gui_basics.h>

#include "DesignTokens.h"  // sibling ui/: mw::ui::DesignTokens (JUCE-free POD)

namespace mw::ui {

class MwAudioLookAndFeel;  // fwd; the banner draws via the active LookAndFeel + tokens

class StatusBanner final : public juce::Component,
                           public juce::AsyncUpdater,
                           public juce::ChangeBroadcaster
{
public:
    // Severity of the active message. Higher == more urgent; the value mapping is the
    // (PI) StatusBannerConstants severity ordering (info/warn/error) [§9.4].
    enum class Severity
    {
        info,
        warn,
        error
    };

    // The active visible state of the banner, returned as a value snapshot so tests
    // (and a future coalescing policy) can inspect it without touching internals.
    struct State
    {
        bool        visible = false;     // is the banner currently showing a message?
        Severity    severity = Severity::info;
        juce::String message;            // the active (transient) warning text
        juce::String disclaimer;         // the hosted standing non-affiliation notice
    };

    explicit StatusBanner(const DesignTokens& tokens);
    ~StatusBanner() override;

    // --- Thread-safe update path (callable from ANY thread) -----------------------
    // These record the requested state under a small lock and schedule a message-
    // thread apply via AsyncUpdater. They NEVER paint, never show a modal dialog, and
    // never block on the caller's thread [§9.4; ADR-021 L12, L13]. The visible state
    // and the ChangeBroadcaster notification both happen later, on the message thread.
    void postMessage(Severity severity, juce::String text);

    // Inject (HOST, do not author) the static non-affiliation disclaimer string. The
    // legal posture is owned by the naming/legal track; this banner only renders the
    // string it is given [§9.4; §1.2]. Also thread-safe + async-applied.
    void postDisclaimer(juce::String text);

    // Convenience synchronous variants for the message thread (e.g. editor wiring).
    // They assert the message-thread precondition and apply immediately, still without
    // any modal loop. Prefer the post* variants from non-message threads.
    void setMessage(Severity severity, juce::String text);
    void setDisclaimer(juce::String text);

    // Dismiss the active message on the message thread (the user clicked the "x", or
    // the editor cleared it). Hides the banner and clears the transient message; the
    // hosted disclaimer is retained [§9.4]. Notifies ChangeListeners.
    void dismiss();

    // --- Inspection (message thread; pure reads for tests / editor) ---------------
    [[nodiscard]] State getState() const;
    [[nodiscard]] bool isShowingMessage() const noexcept { return state_.visible; }
    [[nodiscard]] Severity activeSeverity() const noexcept { return state_.severity; }
    [[nodiscard]] juce::String activeMessage() const { return state_.message; }
    [[nodiscard]] juce::String disclaimer() const { return state_.disclaimer; }

    // Live reskin: swap the token table the banner paints from [§6.1; ADR-015 C10].
    void setTokens(const DesignTokens& tokens);

    // --- juce::Component ----------------------------------------------------------
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;  // dismiss when the "x" is clicked

    // --- juce::AsyncUpdater (message thread) --------------------------------------
    // Applies the most-recent pending request to the visible state and notifies
    // ChangeListeners. This is the ONLY method that mutates the visible state from a
    // posted request, and it runs on the message thread [ADR-021 L12, L13].
    void handleAsyncUpdate() override;

private:
    // Lay the dismiss hit-rectangle out in this component's own (pixel) bounds. Pure
    // geometry from the (PI) StatusBannerConstants; no magic numbers inlined.
    void layoutDismissHitArea();

    // The pending request recorded by a post*() call, guarded by pendingLock_. Kept
    // tiny: the async apply copies it out under the lock then releases it.
    struct Pending
    {
        bool         hasMessage = false;     // a postMessage() is queued
        bool         clearMessage = false;   // a dismiss() is queued
        bool         hasDisclaimer = false;  // a postDisclaimer() is queued
        Severity     severity = Severity::info;
        juce::String message;
        juce::String disclaimer;
    };

    mutable std::mutex pendingLock_;
    Pending            pending_;

    // The committed, visible state — only ever written on the message thread.
    State state_;

    const DesignTokens* tokens_;             // non-owning; the editor owns the table
    juce::Rectangle<int> dismissHitArea_{};  // recomputed in resized()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBanner)
};

} // namespace mw::ui
