// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/Signposts.h — the two NON-MODAL honesty SIGNPOSTS (task 129b) surfaced via the
// reused StatusBanner (task 129) [docs/design/09 §5; docs/design/00 §8.5;
// ADR-012 §Consequences; ADR-023 V16].
//
// WHAT THIS OWNS. A thin policy component that decides WHICH honesty notice (if any)
// should be visible and drives the hosted StatusBanner to show it on the MESSAGE
// THREAD via the banner's thread-safe AsyncUpdater post path. It authors NO drawing of
// its own — it composes the existing StatusBanner surface (§9.4 reuse) so dismissal,
// severity, reskin and reduce-motion behave identically to every other banner. It
// NEVER enters a modal loop and NEVER blocks the host's load path [ADR-021 L12].
//
// THE TWO SIGNPOSTS:
//   (1) Tuning-duality — a contextual note that the A4 reference is off the 440 default
//       (e.g. the recalled 442 Hz "hardware-accurate" value), so users do not mistrust
//       the pitch. Driven by setTuningA4Hz() from the editor; shown only when A4 is off
//       the 440 default [docs/design/09 §5; ADR-012 §Consequences C21/C22].
//   (2) Unblessed-rate — "running unblessed at this host rate" when the host sample
//       rate is above the blessed {44.1/48/88.2/96k} set OR 2x oversampling was clamped
//       to 1x at OS_CEILING. Driven by setProvenance() from the published RenderProvenance
//       (the 112 atomic-pointer UI publish, polled by the 115 telemetry Timer)
//       [docs/design/00 §8.5; ADR-023 V16].
//
// COALESCING POLICY. A single banner hosts both notices; when both conditions hold the
// more-urgent (unblessed-rate, warn) wins over the tuning note (info) — mirroring the
// StatusBanner severity ordering [ADR-021 L12]. update() recomputes the active notice
// from the two latched inputs and posts it (or a dismiss) to the banner.
//
// DISMISSAL. The user can dismiss the active notice via the banner's own "x"; a signpost
// that has been dismissed stays hidden until its condition CHANGES (a new edge), so a
// dismissed standing notice is not immediately re-shown by the next Timer poll. The
// reduce-motion preference is forwarded to the banner so the signpost respects the same
// motion setting as the rest of the UI [§10; ADR-015 C8].
//
// THREADING. All public mutators are MESSAGE-THREAD methods (the editor's Timer + wiring
// call them there); the only state change ever sent to the banner goes through its
// thread-safe post path, so the visible mutation lands on the message thread. STATIC
// strings only — no audio-thread work, no allocation on a hot path [§9.4; ADR-021 L13].
//
// BUILD WIRING: this design-faithful header lives at ui/; its implementation lives at
// plugin/ui/Signposts.cpp so the plugin glob compiles it into the plugin target +
// mw101_plugin_tests (CONFIGURE_DEPENDS) — no shared CMakeLists edit (mirrors
// plugin/ui/StatusBanner.cpp, plugin/ui/ScopeMeterOverlay.cpp).
//
// OUT OF SCOPE (owned elsewhere): the StatusBanner widget itself (129); computing /
// publishing the unblessed-rate provenance flag in the engine (core/CapabilityShim
// provenance, surfaced by 112); authoring the 442 preset / tuning param wiring (104b);
// and wiring this component into the editor (114c assembly). This component only decides
// which static notice to surface and posts it to the banner it hosts.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "StatusBanner.h"   // sibling ui/: mw::ui::StatusBanner (the reused surface, 129)
#include "DesignTokens.h"   // sibling ui/: mw::ui::DesignTokens (JUCE-free POD)

#include "version/RenderProvenance.h"     // mwcore (JUCE-free, core/ on include path): mw::version::RenderProvenance
#include "calibration/SignpostConstants.h" // mwcore (JUCE-free): (PI) trigger thresholds + static notice strings

namespace mw::ui {

// The two honesty signposts, composed over a single hosted StatusBanner. A plain
// juce::Component: it owns the banner as a child and forwards layout to it, so 114c can
// place this one component and get both signposts + the banner surface together. It is a
// ChangeListener on its own banner so a USER dismissal (the "x") is latched here too,
// keeping a dismissed standing notice hidden until its condition edges.
class Signposts final : public juce::Component,
                        private juce::ChangeListener
{
public:
    // Which signpost (if any) is currently surfaced — exposed for the editor/tests so the
    // active notice can be inspected without parsing the banner string.
    enum class Active
    {
        none,           // neither condition holds (or the active notice was dismissed)
        tuningDuality,  // A4 is off the 440 default
        unblessedRate   // host SR above blessed set OR OS clamped to 1x at OS_CEILING
    };

    // The component paints nothing of its own; it composes the StatusBanner, which paints
    // from the injected token table (non-owning view; the editor owns the table and may
    // live-swap it via setTokens()).
    explicit Signposts(const DesignTokens& tokens);
    ~Signposts() override;

    // --- Inputs (MESSAGE THREAD; editor wiring + the 115 Timer poll) ------------------

    // Latch the render provenance published by the engine (the 112 atomic-pointer UI
    // publish, polled by the 115 Timer). The unblessed-rate signpost is active iff the
    // host rate is NOT in the blessed set OR the oversampling was clamped to 1x at
    // OS_CEILING [docs/design/00 §8.5; ADR-023 V16]. Recomputes + posts via update().
    void setProvenance(const mw::version::RenderProvenance& provenance);

    // Latch the active A4 reference (Hz). The tuning-duality signpost is active iff A4 is
    // off the 440 default by more than the (PI) epsilon — i.e. a non-default reference
    // such as the recalled 442 Hz "hardware-accurate" preset [docs/design/09 §5;
    // ADR-012 §Consequences]. Recomputes + posts via update().
    void setTuningA4Hz(float a4Hz);

    // Recompute the active notice from the two latched inputs and post it (or a dismiss)
    // to the hosted banner via its thread-safe AsyncUpdater path. Idempotent: re-posting
    // the same active notice is a no-op so a repeated Timer poll does not thrash the
    // banner. Honours a prior dismissal until the underlying condition changes (an edge).
    void update();

    // --- Reduce-motion (MESSAGE THREAD; UI preference, §10) ---------------------------
    // Forwarded to the hosted banner so the signpost respects the same motion preference
    // as the rest of the UI; affects no binding [§10; ADR-015 C8].
    void setReduceMotion(bool reduceMotion);
    [[nodiscard]] bool isReduceMotion() const noexcept { return reduceMotion_; }

    // --- Live reskin: swap the token table the hosted banner paints from --------------
    void setTokens(const DesignTokens& tokens);

    // --- Access to the composed surface (for 114c wiring + tests) ---------------------
    [[nodiscard]] StatusBanner&       banner()       noexcept { return banner_; }
    [[nodiscard]] const StatusBanner& banner() const noexcept { return banner_; }

    // --- Inspection (MESSAGE THREAD; pure reads for tests / editor) -------------------
    // The signpost currently surfaced (after the latest update()/dismissal). A value read
    // off the latched policy state — does not parse the banner.
    [[nodiscard]] Active activeSignpost() const noexcept { return active_; }
    [[nodiscard]] bool tuningDualityCondition() const noexcept { return tuningOff440_; }
    [[nodiscard]] bool unblessedRateCondition() const noexcept { return unblessedRate_; }

    // --- juce::Component --------------------------------------------------------------
    void resized() override;

private:
    // Recompute the latched conditions from the two inputs (no banner I/O).
    void recomputeConditions() noexcept;

    // juce::ChangeListener: the hosted banner notifies on every state change. We use it to
    // detect a USER dismissal (the banner went from showing our active notice to hidden
    // without us posting that change) and latch dismissed_ so the next update() does not
    // immediately re-show the standing notice [§9.4 dismissal].
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // The reused non-modal surface (129). Owned as a child so 114c places one component.
    StatusBanner banner_;

    // Latched inputs (message-thread only).
    mw::version::RenderProvenance provenance_{};  // last published provenance
    bool  provenanceKnown_ = false;               // true once setProvenance() has run
    float a4Hz_ = mw::cal::ui::signpost::kDefaultA4Hz;  // last latched A4 reference

    // Derived conditions (recomputed by recomputeConditions()).
    bool unblessedRate_ = false;  // SR above blessed set OR OS clamped to 1x
    bool tuningOff440_  = false;  // A4 off the 440 default

    // The notice currently surfaced + the notice the user last dismissed (so a dismissed
    // standing notice is not re-shown until its condition edges).
    Active active_    = Active::none;
    Active dismissed_ = Active::none;

    bool reduceMotion_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Signposts)
};

} // namespace mw::ui
