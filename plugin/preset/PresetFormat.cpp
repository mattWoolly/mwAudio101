// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/preset/PresetFormat.cpp — the .mw101preset JSON <-> canonical ValueTree
// projection + the §6.4 validator (task 025). See PresetFormat.h for the contract and
// the WHY-plugin/ rationale. Realizes docs/design/06 §6.2-§6.5; ADR-008 C13-C18;
// ADR-025; ADR-021 L11.
//
// Message-thread only; no audio-thread work [docs/design/06 §6.3, §12; ADR-008 C19].

#include "preset/PresetFormat.h"

#include <array>
#include <cmath>
#include <string_view>

#include "params/ParamDefs.h"   // mwcore (JUCE-free): kParamDefs / ParamDef / ParamType
#include "state/StateTree.h"    // mwcore (JUCE-free): canonical key constants
#include "state/Extras.h"       // mwcore (JUCE-free): kMaxSeqSteps

namespace mw::plugin::preset {

namespace {

// --- JSON top-level keys (§6.2 schema) ----------------------------------------
constexpr const char* kJsonSchemaVersion = "schemaVersion";
constexpr const char* kJsonMeta          = "meta";
constexpr const char* kJsonParams        = "params";
constexpr const char* kJsonSeq           = "seq";
constexpr const char* kJsonArp           = "arp";

// --- meta sub-keys (§6.2) -----------------------------------------------------
constexpr const char* kMetaName        = "name";
constexpr const char* kMetaAuthor      = "author";
constexpr const char* kMetaCategory    = "category";
constexpr const char* kMetaTags        = "tags";
constexpr const char* kMetaDescription = "description";
constexpr const char* kMetaInspiredBy  = "inspired_by";
constexpr const char* kMetaSoundExt    = "sound_ext";

// --- seq / step / arp sub-keys (§6.2; §5.5) -----------------------------------
constexpr const char* kSeqStepCount = "stepCount";
constexpr const char* kSeqSteps     = "steps";
constexpr const char* kStepNote     = "note";
constexpr const char* kStepGate     = "gate";
constexpr const char* kStepTie      = "tie";
constexpr const char* kStepRest     = "rest";
constexpr const char* kStepAccent   = "accent";  // FORBIDDEN in v1 [ADR-025]
constexpr const char* kArpLatch     = "latch";

// The §6.5 category taxonomy — `meta.category` MUST be exactly one of these
// [docs/design/06 §6.5; ADR-008 C14; research/11 §7.1]. Spec contract, not an invented
// numeric constant.
constexpr std::array<std::string_view, 6> kCategoryEnum{
    "AcidBassLead", "SubBass", "Lead", "PWMStrings", "BlipsFX", "SeqArpRiff"
};

// Attribution-discipline forbidden substrings (§6.4) [ADR-008 C16; research/11 §4.2,
// §7.3]. Matched case-insensitively across name/description/tags/inspired_by:
//   - "as used on track" — a track-reconstruction claim (never permitted)
//   - "tb-303 filter"     — the factually-wrong descriptor research/11 §4.2 corrects
constexpr std::array<std::string_view, 2> kForbiddenPhrases{
    "as used on track", "tb-303 filter"
};

// The software-only choice indices that force sound_ext == true (§6.4)
// [docs/design/06 §3.4; ADR-008 C6/C15; research/11 §6.1, §6.2]:
//   mw101.vco.range index >= 4 (32'/64'), mw101.lfo.shape index == 4 (Sine).
constexpr const char* kVcoRangeId      = "mw101.vco.range";
constexpr int         kVcoRangeSwFirst = 4;     // indices >= 4 are software-only
constexpr const char* kLfoShapeId      = "mw101.lfo.shape";
constexpr int         kLfoShapeSwSine  = 4;     // index 4 (Sine) is software-only

// ------------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------------

bool isKnownCategory(const juce::String& category)
{
    const auto sv = category.toStdString();
    for (const auto& c : kCategoryEnum)
        if (sv == c)
            return true;
    return false;
}

// Does `text` contain any forbidden attribution substring (case-insensitive)?
bool containsForbiddenPhrase(const juce::String& text)
{
    const auto lower = text.toLowerCase();
    for (const auto& phrase : kForbiddenPhrases)
        if (lower.contains(juce::String::fromUTF8(phrase.data(),
                                                  static_cast<int>(phrase.size()))))
            return true;
    return false;
}

// Is `id` a software-ext index in `valueVar`? (vco.range >= 4 or lfo.shape == 4.)
bool usesSoftwareExtFeature(const juce::String& id, const juce::var& valueVar)
{
    const int idx = static_cast<int>(valueVar);
    if (id == kVcoRangeId)
        return idx >= kVcoRangeSwFirst;
    if (id == kLfoShapeId)
        return idx == kLfoShapeSwSine;
    return false;
}

// Validate a single param value against its registry entry. Continuous => within
// [minValue, maxValue]; Choice/Bool => integer index in [0, choiceCount) [§6.4].
bool valueWithinRegistry(const mw::params::ParamDef& def, const juce::var& v)
{
    if (def.type == mw::params::ParamType::Continuous)
    {
        const double x = static_cast<double>(v);
        return x >= static_cast<double>(def.minValue)
            && x <= static_cast<double>(def.maxValue);
    }

    // Choice / Bool: an integer index strictly below choiceCount [§6.4]. A fractional
    // index (e.g. 1.5) is invalid; tolerate only text-precision noise around an integer.
    const double raw = static_cast<double>(v);
    const int idx = static_cast<int>(std::lround(raw));
    if (std::abs(raw - static_cast<double>(idx)) > 1.0e-6)
        return false;
    return idx >= 0 && idx < static_cast<int>(def.choiceCount);
}

// Convert one validated JSON param value into the ValueTree `value` property (the
// modeled value, matching the APVTS <PARAM> shape StateSerializer reads). Continuous
// stores a float; Choice/Bool store the integer index.
juce::var modeledValueProperty(const mw::params::ParamDef& def, const juce::var& v)
{
    if (def.type == mw::params::ParamType::Continuous)
        return juce::var{ static_cast<double>(v) };
    return juce::var{ static_cast<int>(std::lround(static_cast<double>(v))) };
}

// Build the canonical <PARAMS> subtree (one <PARAM id= value=> per validated entry).
juce::ValueTree buildParamsSubtree(const juce::DynamicObject& paramsObj)
{
    juce::ValueTree params{ juce::Identifier{ mw::state::kParamsId } };
    for (const auto& def : mw::params::kParamDefs)
    {
        const juce::var v = paramsObj.getProperty(juce::Identifier{ def.id });
        juce::ValueTree node{ juce::Identifier{ "PARAM" } };
        node.setProperty("id", juce::String::fromUTF8(def.id), nullptr);
        node.setProperty("value", modeledValueProperty(def, v), nullptr);
        params.appendChild(node, nullptr);
    }
    return params;
}

// Project a JSON seq object (if present and well-formed) into the canonical
// <extras>/<seq> subtree (note/gate/tie/rest steps; no accent). Returns an invalid tree
// when no seq is present (an empty/absent seq is legal for non-SeqArpRiff presets).
// Pre-validated: the caller has already rejected any accent-bearing step.
juce::ValueTree buildExtrasSubtree(const juce::var& seqVar, const juce::var& arpVar)
{
    juce::ValueTree extras{ juce::Identifier{ mw::state::kExtrasId } };

    const bool arpLatch = arpVar.isObject()
        && static_cast<bool>(arpVar.getProperty(juce::Identifier{ kArpLatch }, false));
    extras.setProperty(mw::state::kExtrasArpLatch, arpLatch, nullptr);

    juce::ValueTree seq{ juce::Identifier{ mw::state::kSeqId } };
    int stepCount = 0;
    if (seqVar.isObject())
        stepCount = juce::jlimit(0, mw::state::kMaxSeqSteps,
                                 static_cast<int>(seqVar.getProperty(
                                     juce::Identifier{ kSeqStepCount }, 0)));

    if (const auto* steps = seqVar.isObject()
            ? seqVar.getProperty(juce::Identifier{ kSeqSteps }, juce::var()).getArray()
            : nullptr)
    {
        const int n = juce::jmin(stepCount, steps->size());
        for (int i = 0; i < n; ++i)
        {
            const auto& s = steps->getReference(i);
            juce::ValueTree step{ juce::Identifier{ mw::state::kStepId } };
            step.setProperty("note", static_cast<int>(s.getProperty(juce::Identifier{ kStepNote }, 0)), nullptr);
            step.setProperty("gate", static_cast<bool>(s.getProperty(juce::Identifier{ kStepGate }, true)), nullptr);
            step.setProperty("tie",  static_cast<bool>(s.getProperty(juce::Identifier{ kStepTie }, false)), nullptr);
            step.setProperty("rest", static_cast<bool>(s.getProperty(juce::Identifier{ kStepRest }, false)), nullptr);
            seq.appendChild(step, nullptr);
        }
        stepCount = n;
    }
    seq.setProperty(kSeqStepCount, stepCount, nullptr);
    extras.appendChild(seq, nullptr);
    return extras;
}

// --- the §6.4 validation pass --------------------------------------------------
// Returns true iff the parsed JSON object satisfies EVERY §6.4 rule. On success the
// out-params are filled so the projection can run without re-walking the JSON.
bool validate(const juce::var& root, PresetMeta& outMeta)
{
    if (! root.isObject())
        return false;

    // (1) schemaVersion present [§6.4].
    if (! root.hasProperty(kJsonSchemaVersion))
        return false;

    // meta present.
    const auto metaVar = root.getProperty(kJsonMeta, juce::var());
    auto* metaObj = metaVar.getDynamicObject();
    if (metaObj == nullptr)
        return false;

    // (1) meta.name / meta.author / meta.category present [§6.4].
    if (! (metaObj->hasProperty(kMetaName)
        && metaObj->hasProperty(kMetaAuthor)
        && metaObj->hasProperty(kMetaCategory)))
        return false;

    const juce::String name     = metaObj->getProperty(kMetaName).toString();
    const juce::String author   = metaObj->getProperty(kMetaAuthor).toString();
    const juce::String category = metaObj->getProperty(kMetaCategory).toString();
    if (name.isEmpty() || author.isEmpty())
        return false;

    // (1) category in the §6.5 enum [§6.4; §6.5].
    if (! isKnownCategory(category))
        return false;

    const juce::String description = metaObj->getProperty(kMetaDescription).toString();

    // inspired_by: null OR a string; a non-null/non-string is malformed.
    const juce::var inspiredVar = metaObj->getProperty(kMetaInspiredBy);
    juce::String inspiredBy;
    if (! inspiredVar.isVoid() && ! inspiredVar.isString())
    {
        // A non-null inspired_by must be textual (an inspired-by/disputed reference).
        if (! inspiredVar.equals(juce::var()))
            return false;
    }
    else if (inspiredVar.isString())
    {
        inspiredBy = inspiredVar.toString();
    }

    // (4) attribution discipline: no forbidden phrasing/descriptor anywhere in the
    //     human-facing meta text (name/description/tags/inspired_by) [§6.4].
    if (containsForbiddenPhrase(name) || containsForbiddenPhrase(description)
        || containsForbiddenPhrase(inspiredBy))
        return false;

    juce::StringArray tags;
    if (const auto* tagArr = metaVar.getProperty(juce::Identifier{ kMetaTags }, juce::var()).getArray())
        for (const auto& t : *tagArr)
        {
            const juce::String tagStr = t.toString();
            if (containsForbiddenPhrase(tagStr))
                return false;
            tags.add(tagStr);
        }

    // (2) params: every registry ID present + in range / valid choice index [§6.4].
    const auto paramsVar = root.getProperty(kJsonParams, juce::var());
    auto* paramsObj = paramsVar.getDynamicObject();
    if (paramsObj == nullptr)
        return false;

    bool usesSoftwareExt = false;
    for (const auto& def : mw::params::kParamDefs)
    {
        const juce::Identifier id{ def.id };
        if (! paramsObj->hasProperty(id))
            return false;   // a missing registry ID is a hard rejection [§6.4].

        const juce::var v = paramsObj->getProperty(id);
        if (! valueWithinRegistry(def, v))
            return false;

        if (usesSoftwareExtFeature(def.id, v))
            usesSoftwareExt = true;
    }

    // (3) sound_ext == true IFF a software-only feature is used [§6.4].
    const bool declaredSoundExt =
        static_cast<bool>(metaObj->getProperty(kMetaSoundExt));
    if (declaredSoundExt != usesSoftwareExt)
        return false;

    // (5) no per-step `accent` field anywhere in seq.steps [§6.4; ADR-025].
    const auto seqVar = root.getProperty(kJsonSeq, juce::var());
    if (const auto* steps = seqVar.isObject()
            ? seqVar.getProperty(juce::Identifier{ kSeqSteps }, juce::var()).getArray()
            : nullptr)
    {
        for (const auto& s : *steps)
        {
            auto* stepObj = s.getDynamicObject();
            if (stepObj != nullptr && stepObj->hasProperty(kStepAccent))
                return false;
        }
    }

    // All §6.4 rules satisfied — fill the meta out-param.
    outMeta.name        = name;
    outMeta.author      = author;
    outMeta.category    = category;
    outMeta.description = description;
    outMeta.tags        = tags;
    outMeta.inspiredBy  = inspiredBy;
    outMeta.soundExt    = usesSoftwareExt;
    return true;
}

} // namespace

std::optional<juce::ValueTree> loadPresetJson(const juce::File& file, PresetMeta& outMeta)
{
    // Read the file text and parse it; a parse failure (or non-object root) is a
    // malformed preset -> nullopt (the caller recovers via §8 L11) [docs/design/06 §6.3].
    const juce::String text = file.loadFileAsString();

    juce::var root;
    const auto parseResult = juce::JSON::parse(text, root);
    if (parseResult.failed() || ! root.isObject())
        return std::nullopt;

    PresetMeta meta;
    if (! validate(root, meta))
        return std::nullopt;

    // Project the validated JSON into the canonical MW101_STATE tree [docs/design/06
    // §6.1, §6.3]. The root carries schemaVersion (the migration chain reads it);
    // pluginVersion/engineVersion/renderVersion are authored by the loader after the
    // chain runs and are not part of the on-disk preset shape.
    juce::ValueTree canonical{ juce::Identifier{ mw::state::kRootId } };
    canonical.setProperty(mw::state::kAttrSchemaVersion,
                          static_cast<int>(root.getProperty(kJsonSchemaVersion, 1)), nullptr);

    auto* paramsObj = root.getProperty(kJsonParams, juce::var()).getDynamicObject();
    jassert(paramsObj != nullptr);   // validate() guarantees it
    canonical.appendChild(buildParamsSubtree(*paramsObj), nullptr);

    canonical.appendChild(buildExtrasSubtree(root.getProperty(kJsonSeq, juce::var()),
                                             root.getProperty(kJsonArp, juce::var())),
                          nullptr);

    outMeta = meta;
    return canonical;
}

juce::String writePresetJson(const juce::ValueTree& canonical, const PresetMeta& meta)
{
    auto root = std::make_unique<juce::DynamicObject>();

    // schemaVersion: carried from the canonical root, defaulting to 1.
    root->setProperty(kJsonSchemaVersion,
                      static_cast<int>(canonical.getProperty(mw::state::kAttrSchemaVersion, 1)));

    // --- meta block (§6.2) ----------------------------------------------------
    auto metaObj = std::make_unique<juce::DynamicObject>();
    metaObj->setProperty(kMetaName,        meta.name);
    metaObj->setProperty(kMetaAuthor,      meta.author);
    metaObj->setProperty(kMetaCategory,    meta.category);
    metaObj->setProperty(kMetaDescription, meta.description);

    juce::Array<juce::var> tags;
    for (const auto& t : meta.tags)
        tags.add(juce::var{ t });
    metaObj->setProperty(kMetaTags, juce::var{ tags });

    // inspired_by: empty => JSON null (nullable; inspired-by/disputed only) [§6.2].
    metaObj->setProperty(kMetaInspiredBy,
                         meta.inspiredBy.isEmpty() ? juce::var() : juce::var{ meta.inspiredBy });
    metaObj->setProperty(kMetaSoundExt, meta.soundExt);
    root->setProperty(kJsonMeta, juce::var{ metaObj.release() });

    // --- params block: every live registry ID + its modeled value (§6.2) ------
    // The value comes from the canonical <PARAMS> subtree; a missing id falls back to
    // the registry default so an export is always complete.
    const auto params = canonical.getChildWithName(juce::Identifier{ mw::state::kParamsId });
    auto paramsObj = std::make_unique<juce::DynamicObject>();
    for (const auto& def : mw::params::kParamDefs)
    {
        juce::var value{ static_cast<double>(def.defaultValue) };
        if (params.isValid())
        {
            const auto node = params.getChildWithProperty("id", juce::String::fromUTF8(def.id));
            if (node.isValid())
                value = node.getProperty("value");
        }

        // Emit choice/bool as integer indices and continuous as numbers (§6.2).
        if (def.type == mw::params::ParamType::Continuous)
            paramsObj->setProperty(juce::Identifier{ def.id }, static_cast<double>(value));
        else
            paramsObj->setProperty(juce::Identifier{ def.id }, static_cast<int>(static_cast<double>(value)));
    }
    root->setProperty(kJsonParams, juce::var{ paramsObj.release() });

    // --- seq block: stepCount + one step per active step (note/gate/tie/rest) --
    auto seqObj = std::make_unique<juce::DynamicObject>();
    const auto extras = canonical.getChildWithName(juce::Identifier{ mw::state::kExtrasId });
    const auto seq = extras.isValid()
        ? extras.getChildWithName(juce::Identifier{ mw::state::kSeqId })
        : juce::ValueTree{};

    int stepCount = 0;
    juce::Array<juce::var> steps;
    if (seq.isValid())
    {
        stepCount = juce::jlimit(0, mw::state::kMaxSeqSteps,
                                 static_cast<int>(seq.getProperty(kSeqStepCount, 0)));
        const int n = juce::jmin(stepCount, seq.getNumChildren());
        for (int i = 0; i < n; ++i)
        {
            const auto stepTree = seq.getChild(i);
            auto stepObj = std::make_unique<juce::DynamicObject>();
            stepObj->setProperty(kStepNote, static_cast<int>(stepTree.getProperty("note", 0)));
            stepObj->setProperty(kStepGate, static_cast<bool>(stepTree.getProperty("gate", true)));
            stepObj->setProperty(kStepTie,  static_cast<bool>(stepTree.getProperty("tie", false)));
            stepObj->setProperty(kStepRest, static_cast<bool>(stepTree.getProperty("rest", false)));
            // NO accent property — the v1 sequencer has none [ADR-025].
            steps.add(juce::var{ stepObj.release() });
        }
        stepCount = n;
    }
    seqObj->setProperty(kSeqStepCount, stepCount);
    seqObj->setProperty(kSeqSteps, juce::var{ steps });
    root->setProperty(kJsonSeq, juce::var{ seqObj.release() });

    // --- arp block: the arp config snapshot (latch) ---------------------------
    auto arpObj = std::make_unique<juce::DynamicObject>();
    const bool arpLatch = extras.isValid()
        && static_cast<bool>(extras.getProperty(mw::state::kExtrasArpLatch, false));
    arpObj->setProperty(kArpLatch, arpLatch);
    root->setProperty(kJsonArp, juce::var{ arpObj.release() });

    return juce::JSON::toString(juce::var{ root.release() });
}

} // namespace mw::plugin::preset
