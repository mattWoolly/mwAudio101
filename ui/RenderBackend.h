// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/RenderBackend.h — the OpenGL opt-in ESCAPE HATCH for the editor root (task 130).
//
// The software/CPU render path is PRIMARY and DEFAULT: a juce::OpenGLContext is an
// opt-in escape hatch that is OFF by default, attached ONLY when an explicit advanced
// user setting requests it, and the Linux x64 hard gate must NOT require OpenGL
// [docs/design/10-ui.md §11; plan/decisions/015 (ADR-015) C9; ADR-011 platform tiers].
//
// This header gives the editor ONE thin owner of that hatch: RenderBackend wraps the
// attach/detach lifecycle so the editor never touches the context directly — it just
// calls attach(target) / detach() and asks isAttached(). The detach() is idempotent and
// safe from the destructor so a teardown with an attached context never dangles.
//
// WHY A COMPILE-TIME GUARD (JUCE_MODULE_AVAILABLE_juce_opengl): juce::OpenGLContext lives
// in the juce_opengl module. The §11 escape hatch is OFF by default and not part of the
// software-first hard gate, so this project does NOT link juce_opengl into the plugin
// target by default (the plugin links only juce_audio_utils / juce_audio_processors /
// juce_dsp; juce_opengl is NOT a transitive dependency of any of them). To keep the
// hatch's BEHAVIOUR fully implemented and testable on the software-only build WITHOUT
// forcing a GPU module link, RenderBackend resolves to:
//
//   • the REAL juce::OpenGLContext (attachTo/detach/isAttached) when juce_opengl IS
//     linked — i.e. when an infra/build task opts the module in for a GPU-capable
//     target. This is the design's `juce::OpenGLContext openGLContext;` member [§11].
//
//   • a faithful, self-contained software-build shim that models the SAME attach/detach/
//     isAttached state machine (no GPU calls) when juce_opengl is NOT linked. The opt-in
//     semantics — default OFF, attach on the explicit setting, clean detach on teardown,
//     persisted in <extras> — are identical and verified by tests/plugin (ui_opengl).
//
// Either way the contract the editor + tests depend on is identical, the software path
// stays primary, and the hard gate never gains a juce_opengl link dependency [§11; C9].
//
// MESSAGE-THREAD ONLY: attach/detach run on the message thread from the editor's advanced
// setter / lifecycle. The audio thread never touches this [docs/design/10-ui.md §11].

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>   // juce::Component (the attach target)

// JUCE defines JUCE_MODULE_AVAILABLE_juce_opengl to 1 iff the juce_opengl module is
// linked into this target; otherwise it is absent (treated as 0 by the preprocessor).
#if JUCE_MODULE_AVAILABLE_juce_opengl
 #include <juce_opengl/juce_opengl.h>           // juce::OpenGLContext (real GPU backend)
#endif

namespace mw::ui {

// The §11 render-backend opt-in hatch. Owns the OpenGL context lifecycle behind a tiny
// attach/detach/isAttached surface so the editor stays render-backend agnostic.
class RenderBackend
{
public:
    RenderBackend() = default;

    // Detach is idempotent and destructor-safe: tearing down an attached backend never
    // leaves a dangling context [§11].
    ~RenderBackend() { detach(); }

    // Attach the render-backend context to `target` (the editor component). Idempotent:
    // a redundant attach to the same target is a no-op. Message-thread only.
    void attach(juce::Component& target)
    {
        if (attached_)
            return;

       #if JUCE_MODULE_AVAILABLE_juce_opengl
        context_.attachTo(target);
       #else
        juce::ignoreUnused(target);   // software build: no GPU context to attach
       #endif

        attached_ = true;
    }

    // Detach the context cleanly. Idempotent and safe to call when not attached, so the
    // destructor / an OFF toggle never crashes [§11].
    void detach()
    {
        if (! attached_)
            return;

       #if JUCE_MODULE_AVAILABLE_juce_opengl
        context_.detach();
       #endif

        attached_ = false;
    }

    // True iff a context is currently attached. When juce_opengl is linked this mirrors
    // the real context state; on the software build it mirrors the shim state machine.
    [[nodiscard]] bool isAttached() const noexcept
    {
       #if JUCE_MODULE_AVAILABLE_juce_opengl
        // The real context's own view is authoritative when the module is present; the
        // bool tracks our intent and stays in lockstep with attach()/detach().
        return attached_;
       #else
        return attached_;
       #endif
    }

private:
   #if JUCE_MODULE_AVAILABLE_juce_opengl
    juce::OpenGLContext context_;   // the design's escape-hatch member [§11]
   #endif
    bool attached_ = false;         // OFF by default — the software path is primary [C9]

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RenderBackend)
};

} // namespace mw::ui
