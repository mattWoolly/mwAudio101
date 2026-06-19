// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/midi/CcLearnMap.cpp — implementation of the double-buffered, single-writer,
// atomic-swap CC/learn map (task 100). See CcLearnMap.h and docs/design/09 §6.2-6.3.

#include "midi/CcLearnMap.h"

#include <string_view>

#include "params/ParamDefs.h"                    // mw::params::kParamDefs (JUCE-free registry)
#include "calibration/CcLearnMapConstants.h"     // mw::cal::cclearn::kHoldParamIndex

namespace mw::plugin {

namespace {

// Resolve a doc-06 string ID to its index in the kParamDefs registry at COMPILE time.
// Returns CcLearnMap::kUnmapped (-1) if the ID is absent. Pure function of the frozen
// registry, so the default seed is a constant with no runtime string scan and no
// hand-typed index that could drift from the registry order [docs/design/06 §3.0].
consteval std::int32_t paramIndexOf(std::string_view id) noexcept {
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
        if (std::string_view{ mw::params::kParamDefs[i].id } == id)
            return static_cast<std::int32_t>(i);
    return CcLearnMap::kUnmapped;
}

// One row of the §6.2 default CC map: a CC number and the param index it binds to.
struct DefaultBinding {
    std::uint8_t ccNumber;
    std::int32_t paramIndex;
};

// The §6.2 default CC map [docs/design/09 §6.2; ADR-012 C15, C20]. CC1/7/11/74/71/5
// resolve to their doc-06 registry indices at compile time; CC64 sustain -> the HOLD
// sentinel (HOLD / external-HOLD input semantics, a real stock jack — NOT a doc-06
// parameter) [ADR-012 C20; docs/research/08 §2.1]. Every non-HOLD index is verified to
// exist in the registry by the static_assert below.
inline constexpr std::array<DefaultBinding, 7> kDefaultMap = {{
    { 1,  paramIndexOf("mw101.mod.lfo_mod_wheel") },   // CC1  Modulation
    { 7,  paramIndexOf("mw101.vca.level") },           // CC7  Volume
    { 11, paramIndexOf("mw101.amp.expression") },      // CC11 Expression
    { 74, paramIndexOf("mw101.vcf.cutoff") },          // CC74 Cutoff / brightness
    { 71, paramIndexOf("mw101.vcf.resonance") },       // CC71 Resonance
    { 5,  paramIndexOf("mw101.glide.time") },          // CC5  Portamento time
    { 64, mw::cal::cclearn::kHoldParamIndex },         // CC64 Sustain -> HOLD semantics
}};

// Compile-time guard: every non-HOLD default binding resolved to a REAL registry index
// (so a renamed/missing ID is a build error, not a silent unmapped CC) [§6.2].
consteval bool defaultsAllResolve() {
    for (const auto& b : kDefaultMap) {
        if (b.ccNumber == 64) {
            if (b.paramIndex != mw::cal::cclearn::kHoldParamIndex) return false;   // CC64 == HOLD
        } else {
            if (b.paramIndex < 0
                || b.paramIndex >= static_cast<std::int32_t>(mw::params::kParamDefs.size()))
                return false;   // every other default binds a live registry index
        }
    }
    return true;
}
static_assert(defaultsAllResolve(),
              "CcLearnMap: a §6.2 default CC binding did not resolve to a live kParamDefs "
              "index (param ID renamed/removed?) [docs/design/09 §6.2].");

// Seed a buffer with the §6.2 default map: all rows ccNumber-stamped + disabled, then
// the default-mapped rows enabled. Pure, allocation-free.
void seedDefaults(std::array<CcBinding, CcLearnMap::kNumCc>& buf) noexcept {
    for (std::size_t cc = 0; cc < buf.size(); ++cc) {
        buf[cc].ccNumber   = static_cast<std::uint8_t>(cc);
        buf[cc].paramIndex = CcLearnMap::kUnmapped;
        buf[cc].enabled    = false;
    }
    for (const auto& b : kDefaultMap) {
        buf[b.ccNumber].ccNumber   = b.ccNumber;
        buf[b.ccNumber].paramIndex = b.paramIndex;
        buf[b.ccNumber].enabled    = true;
    }
}

} // namespace

CcLearnMap::CcLearnMap() noexcept {
    // Seed buffer A with the §6.2 defaults and publish it as live; leave B as the
    // initial inactive draft buffer. Construction runs off the audio thread.
    seedDefaults(buffers_[0]);
    live_.store(&buffers_[0], std::memory_order_release);
}

CcLearnMap::Buffer* CcLearnMap::inactiveBuffer() noexcept {
    // The inactive buffer is whichever of the two the live pointer does NOT name.
    const Buffer* liveBuf = live_.load(std::memory_order_acquire);
    return (liveBuf == &buffers_[0]) ? &buffers_[1] : &buffers_[0];
}

CcBinding* CcLearnMap::editableCopy() noexcept {
    // Hand back the inactive buffer, pre-seeded with the current live contents so an
    // unedited publish() is a faithful no-op and edits start from the live state. The
    // audio thread never reads the inactive buffer, so this copy cannot race lookup().
    Buffer* draft = inactiveBuffer();
    *draft = *live_.load(std::memory_order_acquire);   // POD array copy; no allocation
    return draft->data();
}

void CcLearnMap::publish() noexcept {
    // Atomically swap the live pointer to the (edited) inactive buffer. A single
    // release store — lock-free, allocation-free [docs/design/09 §6.3; ADR-012 C16].
    live_.store(inactiveBuffer(), std::memory_order_release);
}

std::int32_t CcLearnMap::lookup(std::uint8_t ccNumber) const noexcept {
    // Branch-free: read the live pointer once, index the row, and select between the
    // bound param index and kUnmapped by the enabled flag without a conditional branch.
    // ccNumber is a std::uint8_t (0..127 == kNumCc-1) so the index is always in range —
    // no bounds branch needed. No lock, no allocation [docs/design/09 §6.3; ADR-012 C16].
    const Buffer* buf = live_.load(std::memory_order_acquire);
    const CcBinding& row = (*buf)[ccNumber];
    const std::int32_t enabledMask = -static_cast<std::int32_t>(row.enabled);   // 0 or -1 (all-ones)
    // enabled -> paramIndex ; disabled -> kUnmapped (-1), all without a branch.
    return (row.paramIndex & enabledMask) | (kUnmapped & ~enabledMask);
}

const CcBinding* CcLearnMap::liveBuffer() const noexcept {
    return live_.load(std::memory_order_acquire)->data();
}

} // namespace mw::plugin
