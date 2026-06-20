// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/state/ValueTreeMutableAdapter.h — the thin juce::ValueTree adapter that
// IMPLEMENTS the JUCE-free mw::state::IMutableTree seam (task 024 support; the
// "thin juce::ValueTree adapter" promised by core/state/Migration.h).
//
// WHY plugin/ AND NOT core/: this header references juce::ValueTree / juce::var, so it
// CANNOT live under core/ (the no-JUCE-in-core guard, cmake/NoJuceInCore.cmake, fails
// the build on any JUCE token under core/) [ADR-001 C1]. The migration LOGIC (the
// ordered chain, the >CURRENT no-op down-bind, the accent drop, the os.factor alias)
// stays JUCE-free in core/state/Migration.{h,cpp}; this adapter only carries the JUCE
// dependency so migrateToCurrent can run over the canonical juce::ValueTree the
// (de)serializer produces [docs/design/06 §7.1; ADR-008 C10].
//
// MESSAGE-THREAD ONLY: all migration runs on the message thread; nothing here is
// audio-thread work [docs/design/06 §7.2; ADR-008 C19; ADR-021 L13].

#pragma once

#include <optional>
#include <string_view>

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/Migration.h"   // mwcore (JUCE-free): mw::state::IMutableTree
#include "state/StateTree.h"   // canonical key constants (kRootId, kParamsId, ...)

namespace mw::plugin::state {

// Wraps a canonical MW101_STATE juce::ValueTree as an mw::state::IMutableTree so the
// JUCE-free migration chain can read/mutate it. Holds the tree by value (juce::ValueTree
// is a reference-counted handle to shared data, so edits land on the wrapped tree).
class ValueTreeMutableAdapter final : public mw::state::IMutableTree
{
public:
    explicit ValueTreeMutableAdapter(juce::ValueTree root) : tree_(std::move(root)) {}

    [[nodiscard]] const juce::ValueTree& tree() const noexcept { return tree_; }

    // --- schemaVersion root attribute (int; nullopt == absent) [§5.1] ----------
    std::optional<int> getSchemaVersion() const override
    {
        const juce::Identifier key{ mw::state::kAttrSchemaVersion };
        if (! tree_.hasProperty(key))
            return std::nullopt;
        return static_cast<int>(tree_.getProperty(key));
    }

    void setSchemaVersion(int v) override
    {
        tree_.setProperty(juce::Identifier{ mw::state::kAttrSchemaVersion }, v, nullptr);
    }

    // --- <PARAMS> string-keyed values (the APVTS subtree) [§5.1; §7.4] ----------
    // APVTS stores each parameter as a <PARAM id="..." value="..."> child; value is the
    // DENORMALISED (modeled) number. double is the widest carrier (the §7.1 contract).
    bool hasParam(std::string_view id) const override
    {
        return findParam(id).isValid();
    }

    std::optional<double> getParam(std::string_view id) const override
    {
        const auto child = findParam(id);
        if (! child.isValid())
            return std::nullopt;
        return static_cast<double>(child.getProperty(kValueKey));
    }

    void setParam(std::string_view id, double value) override
    {
        auto child = findParam(id);
        if (! child.isValid())
        {
            // The §7.4 alias copy may create the canonical ID if absent: append a
            // well-formed <PARAM> child so the bind side finds it.
            auto params = paramsSubtree(/*createIfMissing=*/true);
            child = juce::ValueTree{ kParamElement };
            child.setProperty(kIdKey, juce::String{ juce::CharPointer_UTF8{ id.data() },
                                                    id.size() }, nullptr);
            params.appendChild(child, nullptr);
        }
        child.setProperty(kValueKey, value, nullptr);
    }

    // --- <seq> per-step surface (the ADR-025 stray-accent drop) [§7.3] ----------
    int getNumSeqSteps() const override
    {
        const auto seq = seqSubtree();
        return seq.isValid() ? seq.getNumChildren() : 0;
    }

    bool seqStepHasAttribute(int stepIndex, std::string_view attr) const override
    {
        const auto seq = seqSubtree();
        if (! seq.isValid() || stepIndex < 0 || stepIndex >= seq.getNumChildren())
            return false;
        return seq.getChild(stepIndex).hasProperty(toIdentifier(attr));
    }

    void removeSeqStepAttribute(int stepIndex, std::string_view attr) override
    {
        auto seq = seqSubtree();
        if (! seq.isValid() || stepIndex < 0 || stepIndex >= seq.getNumChildren())
            return;
        seq.getChild(stepIndex).removeProperty(toIdentifier(attr), nullptr);
    }

private:
    static juce::Identifier toIdentifier(std::string_view s)
    {
        return juce::Identifier{ juce::String{ juce::CharPointer_UTF8{ s.data() }, s.size() } };
    }

    juce::ValueTree paramsSubtree(bool createIfMissing = false) const
    {
        auto params = tree_.getChildWithName(juce::Identifier{ mw::state::kParamsId });
        if (! params.isValid() && createIfMissing)
        {
            params = juce::ValueTree{ juce::Identifier{ mw::state::kParamsId } };
            const_cast<juce::ValueTree&>(tree_).appendChild(params, nullptr);
        }
        return params;
    }

    juce::ValueTree seqSubtree() const
    {
        const auto extras = tree_.getChildWithName(juce::Identifier{ mw::state::kExtrasId });
        if (! extras.isValid())
            return {};
        return extras.getChildWithName(juce::Identifier{ mw::state::kSeqId });
    }

    juce::ValueTree findParam(std::string_view id) const
    {
        const auto params = paramsSubtree();
        if (! params.isValid())
            return {};
        const juce::String wanted{ juce::CharPointer_UTF8{ id.data() }, id.size() };
        for (int i = 0; i < params.getNumChildren(); ++i)
        {
            const auto child = params.getChild(i);
            if (child.getProperty(kIdKey).toString() == wanted)
                return child;
        }
        return {};
    }

    // APVTS PARAM child shape: <PARAM id="..." value="..."> [juce APVTS].
    static const juce::Identifier kParamElement;
    static const juce::Identifier kIdKey;
    static const juce::Identifier kValueKey;

    juce::ValueTree tree_;
};

inline const juce::Identifier ValueTreeMutableAdapter::kParamElement{ "PARAM" };
inline const juce::Identifier ValueTreeMutableAdapter::kIdKey{ "id" };
inline const juce::Identifier ValueTreeMutableAdapter::kValueKey{ "value" };

} // namespace mw::plugin::state
