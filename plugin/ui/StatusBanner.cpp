// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/StatusBanner.cpp — implementation of the non-modal StatusBanner declared
// in ui/StatusBanner.h [docs/design/10-ui.md §9.4; ADR-021 L12, L13]. The visible
// state is only ever mutated on the MESSAGE THREAD (inside dismiss()/setMessage()/
// setDisclaimer() called there, or inside handleAsyncUpdate() reached via the
// thread-safe AsyncUpdater post path). Nothing here ever enters a modal loop.
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/StatusBanner.h and is
// reached by relative include — no shared CMakeLists edit (mirrors
// plugin/ui/MwAudioLookAndFeel.cpp).

#include "../../ui/StatusBanner.h"

#include "../../ui/MwAudioLookAndFeel.h"  // toColour() / toFont() token lift seam
#include "../../core/calibration/StatusBannerConstants.h"

namespace mw::ui {

namespace cal = mw::cal::ui::status_banner;

namespace {

// Map a severity to the DesignTokens colour ROLE that encodes it. The banner never
// authors a concrete colour: info reuses the accent (controlFill), warn the extension
// tag, error the (most-urgent) thumb colour — all read from the injected table so a
// token swap reskins the banner like every other surface [§6.1; ADR-015 C10].
mw::ui::Colour severityColour(StatusBanner::Severity sev, const DesignTokens& t) noexcept
{
    switch (sev)
    {
        case StatusBanner::Severity::error: return t.controlThumb;
        case StatusBanner::Severity::warn:  return t.extensionTag;
        case StatusBanner::Severity::info:  return t.controlFill;
        default:                            return t.controlFill;
    }
}

} // namespace

StatusBanner::StatusBanner(const DesignTokens& tokens)
    : tokens_(&tokens)
{
    // Hidden until a message is posted. A standing disclaimer alone does not force it
    // visible; the editor decides when to surface the disclaimer (§9.4).
    setVisible(false);
    setInterceptsMouseClicks(true, true);
}

StatusBanner::~StatusBanner()
{
    // AsyncUpdater base cancels any pending callback in its own dtor; nothing else to
    // tear down (we own no JUCE child components).
    cancelPendingUpdate();
}

// ---------------------------------------------------------------------------
// Thread-safe post path: record the request + schedule a message-thread apply.
// ---------------------------------------------------------------------------
void StatusBanner::postMessage(Severity severity, juce::String text)
{
    {
        const std::scoped_lock lock(pendingLock_);
        pending_.hasMessage   = true;
        pending_.clearMessage = false;   // a new message supersedes a queued dismiss
        pending_.severity     = severity;
        pending_.message      = std::move(text);
    }
    triggerAsyncUpdate();   // coalesces; applied on the message thread (never blocks)
}

void StatusBanner::postDisclaimer(juce::String text)
{
    {
        const std::scoped_lock lock(pendingLock_);
        pending_.hasDisclaimer = true;
        pending_.disclaimer    = std::move(text);
    }
    triggerAsyncUpdate();
}

// ---------------------------------------------------------------------------
// Synchronous message-thread variants (editor wiring). Apply immediately; still no
// modal loop, still notify ChangeListeners.
// ---------------------------------------------------------------------------
void StatusBanner::setMessage(Severity severity, juce::String text)
{
    JUCE_ASSERT_MESSAGE_THREAD
    state_.severity = severity;
    state_.message  = std::move(text);
    state_.visible  = true;
    setVisible(true);
    repaint();
    sendChangeMessage();   // synchronous on the message thread
}

void StatusBanner::setDisclaimer(juce::String text)
{
    JUCE_ASSERT_MESSAGE_THREAD
    state_.disclaimer = std::move(text);
    repaint();
    sendChangeMessage();
}

void StatusBanner::dismiss()
{
    JUCE_ASSERT_MESSAGE_THREAD
    state_.visible = false;
    state_.message = juce::String{};
    // The hosted disclaimer is a standing notice — retain it across a dismiss (§9.4).
    setVisible(false);
    repaint();
    sendChangeMessage();
}

// ---------------------------------------------------------------------------
// Message-thread apply of a posted request (AsyncUpdater callback).
// ---------------------------------------------------------------------------
void StatusBanner::handleAsyncUpdate()
{
    Pending p;
    {
        const std::scoped_lock lock(pendingLock_);
        p = pending_;
        pending_ = Pending{};   // consume; release the lock before touching JUCE state
    }

    bool changed = false;

    if (p.hasDisclaimer)
    {
        state_.disclaimer = std::move(p.disclaimer);
        changed = true;
    }

    if (p.hasMessage)
    {
        state_.severity = p.severity;
        state_.message  = std::move(p.message);
        state_.visible  = true;
        changed = true;
    }
    else if (p.clearMessage)
    {
        state_.visible = false;
        state_.message = juce::String{};
        changed = true;
    }

    if (changed)
    {
        setVisible(state_.visible);
        repaint();
        sendChangeMessage();   // notify listeners on the message thread [ADR-021 L12]
    }
}

// ---------------------------------------------------------------------------
// Inspection.
// ---------------------------------------------------------------------------
StatusBanner::State StatusBanner::getState() const
{
    return state_;
}

void StatusBanner::setTokens(const DesignTokens& tokens)
{
    tokens_ = &tokens;
    repaint();
}

// ---------------------------------------------------------------------------
// juce::Component.
// ---------------------------------------------------------------------------
void StatusBanner::resized()
{
    layoutDismissHitArea();
}

void StatusBanner::layoutDismissHitArea()
{
    const auto bounds = getLocalBounds();
    const float h = static_cast<float>(bounds.getHeight());
    const int dismissW = juce::roundToInt(h * cal::kDismissWidthFraction);
    dismissHitArea_ = juce::Rectangle<int>(bounds.getRight() - dismissW, bounds.getY(),
                                           dismissW, bounds.getHeight());
}

void StatusBanner::paint(juce::Graphics& g)
{
    if (! state_.visible)
        return;

    const DesignTokens& t = *tokens_;
    const auto bounds = getLocalBounds().toFloat();
    const float h = bounds.getHeight();
    const float pad = h * cal::kPaddingFraction;

    // Background panel (rounded). Colour from the token table; never inlined.
    g.setColour(MwAudioLookAndFeel::toColour(t.panel));
    g.fillRoundedRectangle(bounds, cal::kBackgroundCornerRadius);

    g.setColour(MwAudioLookAndFeel::toColour(t.moduleOutline));
    g.drawRoundedRectangle(bounds.reduced(t.outlineStroke * 0.5f),
                           cal::kBackgroundCornerRadius, t.outlineStroke);

    auto content = bounds.reduced(pad);

    // Leading severity swatch — colour encodes info/warn/error via the token roles.
    const float swatchW = h * cal::kSwatchWidthFraction;
    auto swatch = content.removeFromLeft(swatchW);
    content.removeFromLeft(pad);
    g.setColour(MwAudioLookAndFeel::toColour(severityColour(state_.severity, t)));
    g.fillRoundedRectangle(swatch, cal::kBackgroundCornerRadius * 0.5f);

    // Trailing dismiss "x".
    const float dismissW = h * cal::kDismissWidthFraction;
    auto dismiss = content.removeFromRight(dismissW);
    content.removeFromRight(pad);
    {
        g.setColour(MwAudioLookAndFeel::toColour(t.textSecondary));
        auto x = dismiss.reduced(dismiss.getWidth() * 0.30f, dismiss.getHeight() * 0.30f);
        g.drawLine(x.getX(), x.getY(), x.getRight(), x.getBottom(), cal::kDismissStrokeWidth);
        g.drawLine(x.getX(), x.getBottom(), x.getRight(), x.getY(), cal::kDismissStrokeWidth);
    }

    // Message + disclaimer text. The message is the active warning; the disclaimer is
    // the standing non-affiliation notice rendered as a quieter trailing line. We
    // never author the disclaimer text — we render whatever was injected (§9.4).
    g.setColour(MwAudioLookAndFeel::toColour(t.textPrimary));
    g.setFont(MwAudioLookAndFeel::toFont(t.valueFont));

    juce::String shown = state_.message;
    if (state_.disclaimer.isNotEmpty())
    {
        if (shown.isNotEmpty())
            shown << "  —  ";
        shown << state_.disclaimer;
    }

    g.drawText(shown, content.toNearestInt(),
               juce::Justification::centredLeft, /*useEllipsesIfTooBig*/ true);
}

void StatusBanner::mouseUp(const juce::MouseEvent& e)
{
    // A click in the trailing dismiss hit-area clears the active message (message
    // thread — mouse events arrive there). Never a modal interaction [§9.4].
    if (state_.visible && dismissHitArea_.contains(e.getPosition()))
        dismiss();
}

} // namespace mw::ui
