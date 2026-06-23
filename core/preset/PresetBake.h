// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/preset/PresetBake.h — the deterministic flat-POD preset bake loader (task 144b).
//
// Realizes docs/design/11 §9.1 (the presets/ subtree "deterministic loader baking the
// ~64 patches into a flat POD table at build/load time, never parsing on the audio
// thread") and ADR-014 C9 (baked into a flat POD table at build/load time; never
// parsed/allocated on the audio thread; each entry verifiable by schemaVersion +
// checksum) under ADR-001 C3/C4 (no heap alloc / no lock / no parse on the hot path).
//
// CONTRACT. The POD table (`BakedTable`, a contiguous array of `BakedPreset`) is the
// ONLY thing the audio thread ever touches. JSON parsing + projection happen at the
// bake (build/load time, message thread); the audio thread only INDEXES the finished
// POD. `BakedPreset` is `trivially_copyable` with a fixed `sizeof`, asserted below.
//
// WHY mwcore (JUCE-FREE). The baker, the loader, and the POD table carry NO JUCE: the
// task locks mwcore JUCE-free and says any build-time bake tooling stays host-side, not
// in mwcore. The §6.4 *validation* of the on-disk format is owned by the JUCE-bound
// projection (task 025, plugin-side) and round-tripped by task 025b; this module bakes
// the well-formed factory corpus into the audio-thread POD. To honour "checksum via
// task 040" WITHOUT making mwcore depend on the test/golden tree, the SHA-256 hasher is
// INJECTED (a `Hasher` callback): the caller passes `mw::golden::sha256` (task 040), so
// the digest is computed by the single SHA-256 source of truth, while mwcore stays
// dependency-light and JUCE-free [ADR-001 C1; ADR-013 C5].
//
// DETERMINISM / STABLE BYTE LAYOUT. The checksum and the table-reproducibility hash are
// computed over a CANONICAL serialization (`canonicalImage`) — fixed little-endian for
// every scalar, IEEE-754 little-endian for floats, fixed-width NUL-padded strings — NOT
// over raw struct memory (whose padding bytes are unspecified). That canonical image is
// byte-identical across the bless/Linux/Windows boxes, so the same patch set re-bakes to
// the same bytes everywhere [docs/design/11 §9.1].

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "params/ParamDefs.h"        // kParamDefs — the canonical param order/count
#include "state/Extras.h"            // mw::state::Extras / SeqStep — the seq POD
#include "version/EngineVersion.h"   // kCurrentSchemaVersion

namespace mw::preset {

// --- One bake input: a stable id (the corpus-relative path) + the raw JSON text ------
// `id` carries no runtime meaning beyond authoring/diagnostics; it makes the bake input
// self-describing and the determinism reproducible (the caller orders inputs).
struct PresetSource {
    std::string id;     // e.g. "Lead/Bright Lead.mw101preset"
    std::string json;   // the raw .mw101preset JSON text
};

// --- A 32-byte digest, value-comparable. Layout matches the task-040 Sha256 digest so
// the injected hasher can return it directly. ----------------------------------------
struct Checksum {
    std::array<std::uint8_t, 32> bytes{};
    bool operator==(const Checksum& o) const noexcept { return bytes == o.bytes; }
    bool operator!=(const Checksum& o) const noexcept { return bytes != o.bytes; }
};

// The hasher seam: a SHA-256 over a byte span. The caller injects task-040's
// `mw::golden::sha256` so mwcore needs no dependency on the test/golden tree.
using Hasher = std::function<Checksum(std::span<const std::byte>)>;

// --- The flat POD table entry --------------------------------------------------------
// Trivially copyable, fixed sizeof, no pointers/strings parsed at runtime: the audio
// thread only indexes this. Param values are stored in kParamDefs order; the seq pattern
// is the existing `mw::state::Extras` POD; meta needed at runtime (name/category) is a
// fixed-width NUL-padded char buffer (NOT a heap string).
struct BakedPreset {
    static constexpr std::size_t kParamCount = 91;   // == kParamDefs.size() [§3.0]
    static constexpr std::size_t kNameCap    = 64;   // fixed-width meta name
    static constexpr std::size_t kCategoryCap = 32;  // fixed-width §6.5 category

    std::uint16_t schemaVersion = 0;                 // ADR-008 C9-C10; carried per entry
    std::uint16_t paramCount    = 0;                 // == kParamCount (self-describing)

    std::array<float, kParamCount> params{};         // values in kParamDefs order
    mw::state::Extras              extras{};          // the seq pattern POD (no heap)

    std::array<char, kNameCap>     nameBuf{};         // NUL-padded; not heap
    std::array<char, kCategoryCap> categoryBuf{};     // NUL-padded; not heap

    Checksum checksum{};                              // SHA-256 computed AT BAKE TIME

    // Read-only views over the fixed-width meta (no allocation; for the message thread).
    [[nodiscard]] const char* name() const noexcept { return nameBuf.data(); }
    [[nodiscard]] const char* category() const noexcept { return categoryBuf.data(); }
};

static_assert(std::is_trivially_copyable_v<BakedPreset>,
              "BakedPreset MUST be trivially copyable (audio-thread POD table) [§9.1].");
static_assert(BakedPreset::kParamCount == mw::params::kParamDefs.size(),
              "BakedPreset::kParamCount MUST equal kParamDefs.size() [§3.0].");
// A fixed sizeof pins the layout so a silent field drift is caught at build time. The
// value is layout-derived (see canonicalImage for the platform-stable hash image).
static_assert(sizeof(BakedPreset) ==
                  2 * sizeof(std::uint16_t)
                + BakedPreset::kParamCount * sizeof(float)
                + sizeof(mw::state::Extras)
                + BakedPreset::kNameCap + BakedPreset::kCategoryCap
                + sizeof(Checksum),
              "BakedPreset sizeof MUST match its declared fields (no hidden padding) "
              "[§9.1].");

// --- The contiguous flat POD table ---------------------------------------------------
// A std::vector storage (allocated ONCE at bake/load time on the message thread); the
// audio thread only indexes the contiguous entries via entry()/data()/size().
class BakedTable {
public:
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    // Pure POD index — no allocation, no lock, no parse. THIS is the audio-thread seam.
    [[nodiscard]] const BakedPreset& entry(std::size_t i) const noexcept { return entries_[i]; }
    [[nodiscard]] const BakedPreset* data() const noexcept { return entries_.data(); }

    // Message-thread-only mutable access (e.g. corruption tests / future re-bake).
    [[nodiscard]] BakedPreset& entryMutable(std::size_t i) noexcept { return entries_[i]; }

    void reserve(std::size_t n) { entries_.reserve(n); }
    void push(const BakedPreset& e) { entries_.push_back(e); }

private:
    std::vector<BakedPreset> entries_;
};

// --- kParamDefs index helper ---------------------------------------------------------
// Returns the kParamDefs index of `id`, or -1 if unknown. Used by the projection and by
// callers reading a named param out of a baked entry.
[[nodiscard]] inline int paramIndex(std::string_view id) noexcept {
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
        if (id == mw::params::kParamDefs[i].id)
            return static_cast<int>(i);
    return -1;
}

// =====================================================================================
// Canonical little-endian serialization (the platform-stable hash image)
// =====================================================================================
namespace detail {

inline void putU16(std::vector<std::byte>& out, std::uint16_t v) {
    out.push_back(static_cast<std::byte>(v & 0xFFu));
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFFu));
}

inline void putU32(std::vector<std::byte>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFFu));
}

inline void putU64(std::vector<std::byte>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFFu));
}

inline void putF32(std::vector<std::byte>& out, float v) {
    // IEEE-754 little-endian. bit_cast is well-defined; the FP discipline flags keep
    // the stored value reproducible run-to-run [ADR-013 C5; ADR-014 C4].
    putU32(out, std::bit_cast<std::uint32_t>(v));
}

inline void putI8(std::vector<std::byte>& out, std::int8_t v) {
    out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(v)));
}

inline void putBool(std::vector<std::byte>& out, bool v) {
    out.push_back(static_cast<std::byte>(v ? 1u : 0u));
}

template <std::size_t N>
inline void putFixedStr(std::vector<std::byte>& out, const std::array<char, N>& s) {
    for (std::size_t i = 0; i < N; ++i)
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(s[i])));
}

// Append the canonical byte image of ONE entry's PAYLOAD (everything except the stored
// checksum). This is what the checksum is computed over, and what the table-determinism
// hash concatenates. Fixed field order; fixed little-endian; fixed-width strings.
inline void appendEntryPayload(std::vector<std::byte>& out, const BakedPreset& e) {
    putU16(out, e.schemaVersion);
    putU16(out, e.paramCount);
    for (const float v : e.params) putF32(out, v);

    // The seq pattern POD (canonical field order — NOT raw struct memory).
    putU32(out, static_cast<std::uint32_t>(e.extras.stepCount));
    putBool(out, e.extras.arpLatch);
    putU64(out, static_cast<std::uint64_t>(e.extras.driftSeed));
    putBool(out, e.extras.seedLocked);
    for (const auto& step : e.extras.steps) {
        putI8(out, step.noteSemitone);
        putBool(out, step.gate);
        putBool(out, step.tie);
        putBool(out, step.rest);
    }

    putFixedStr(out, e.nameBuf);
    putFixedStr(out, e.categoryBuf);
}

} // namespace detail

// The canonical byte image of ONE entry's payload (excludes the stored checksum). The
// per-entry checksum is `hasher(entryPayloadImage(e))`.
[[nodiscard]] inline std::vector<std::byte> entryPayloadImage(const BakedPreset& e) {
    std::vector<std::byte> out;
    out.reserve(512);
    detail::appendEntryPayload(out, e);
    return out;
}

// The canonical byte image of the WHOLE table: a fixed 4-byte little-endian entry count
// followed by each entry's payload AND its stored checksum, in table order. Re-baking
// the same patch set produces a byte-identical image [docs/design/11 §9.1].
[[nodiscard]] inline std::vector<std::byte> canonicalImage(const BakedTable& t) {
    std::vector<std::byte> out;
    out.reserve(t.size() * 512 + 4);
    detail::putU32(out, static_cast<std::uint32_t>(t.size()));
    for (std::size_t i = 0; i < t.size(); ++i) {
        detail::appendEntryPayload(out, t.entry(i));
        const auto& cs = t.entry(i).checksum.bytes;
        for (const auto b : cs) out.push_back(static_cast<std::byte>(b));
    }
    return out;
}

// =====================================================================================
// A minimal JUCE-free reader for the well-formed .mw101preset corpus shape
// =====================================================================================
// The factory corpus is machine-authored, well-formed JSON whose schema (§6.2) is
// validated plugin-side by task 025 and round-tripped by task 025b. This reader extracts
// exactly the fields the POD needs (schemaVersion; meta.name/category; the flat params
// number map; the seq stepCount + steps; arp.latch). It is bake-time-only — never on the
// audio thread — so its allocations are out of the hot-path scope [ADR-001 C3/C4].
namespace detail {

inline void skipWs(std::string_view s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
}

// Parse a JSON string starting at s[i]=='"'; advances i past the closing quote.
inline std::string parseString(std::string_view s, std::size_t& i) {
    std::string out;
    ++i;  // opening quote
    while (i < s.size() && s[i] != '"') {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            ++i;
            const char esc = s[i];
            switch (esc) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                default: out.push_back(esc); break;  // minimal: pass through
            }
        } else {
            out.push_back(c);
        }
        ++i;
    }
    if (i < s.size()) ++i;  // closing quote
    return out;
}

// Parse a JSON number/keyword token (number | true | false | null) into a double and a
// kind. Advances i past the token.
struct Scalar { double num = 0.0; bool isTrue = false; bool isNull = false; bool isBool = false; };

inline Scalar parseScalar(std::string_view s, std::size_t& i) {
    Scalar out;
    if (s.compare(i, 4, "true") == 0)  { out.isBool = true; out.isTrue = true;  i += 4; return out; }
    if (s.compare(i, 5, "false") == 0) { out.isBool = true; out.isTrue = false; i += 5; return out; }
    if (s.compare(i, 4, "null") == 0)  { out.isNull = true; i += 4; return out; }
    const std::size_t start = i;
    while (i < s.size()) {
        const char c = s[i];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
            ++i;
        else
            break;
    }
    out.num = std::strtod(std::string(s.substr(start, i - start)).c_str(), nullptr);
    return out;
}

// Skip an arbitrary JSON value (object/array/string/scalar) starting at s[i].
inline void skipValue(std::string_view s, std::size_t& i);

inline void skipObjectOrArray(std::string_view s, std::size_t& i, char open, char close) {
    int depth = 0;
    bool inStr = false;
    while (i < s.size()) {
        const char c = s[i];
        if (inStr) {
            if (c == '\\') { i += 2; continue; }
            if (c == '"') inStr = false;
            ++i;
            continue;
        }
        if (c == '"') { inStr = true; ++i; continue; }
        if (c == open) ++depth;
        else if (c == close) { --depth; if (depth == 0) { ++i; return; } }
        ++i;
    }
    (void)open;
}

inline void skipValue(std::string_view s, std::size_t& i) {
    skipWs(s, i);
    if (i >= s.size()) return;
    const char c = s[i];
    if (c == '{') skipObjectOrArray(s, i, '{', '}');
    else if (c == '[') skipObjectOrArray(s, i, '[', ']');
    else if (c == '"') (void)parseString(s, i);
    else (void)parseScalar(s, i);
}

} // namespace detail

// The intermediate parse result (bake-time only). Holds the fields the POD needs.
struct ParsedPreset {
    bool ok = false;
    std::uint16_t schemaVersion = 0;
    std::string name;
    std::string category;
    std::array<double, BakedPreset::kParamCount> paramValues{};  // by kParamDefs index
    std::array<bool, BakedPreset::kParamCount> paramSeen{};       // each registry id present?
    mw::state::Extras extras{};
};

// Parse one .mw101preset JSON text into the intermediate (bake-time only).
[[nodiscard]] inline ParsedPreset parsePreset(std::string_view json) {
    using namespace detail;
    ParsedPreset r;
    std::size_t i = 0;
    skipWs(json, i);
    if (i >= json.size() || json[i] != '{') return r;
    ++i;  // root '{'

    while (i < json.size()) {
        skipWs(json, i);
        if (i < json.size() && json[i] == '}') { ++i; break; }
        if (i >= json.size() || json[i] != '"') break;
        const std::string key = parseString(json, i);
        skipWs(json, i);
        if (i < json.size() && json[i] == ':') ++i;
        skipWs(json, i);

        if (key == "schemaVersion") {
            const auto sc = parseScalar(json, i);
            r.schemaVersion = static_cast<std::uint16_t>(sc.num);
        } else if (key == "meta") {
            // Walk the meta object for name/category; skip everything else.
            if (i < json.size() && json[i] == '{') {
                ++i;
                while (i < json.size()) {
                    skipWs(json, i);
                    if (i < json.size() && json[i] == '}') { ++i; break; }
                    if (i >= json.size() || json[i] != '"') break;
                    const std::string mk = parseString(json, i);
                    skipWs(json, i);
                    if (i < json.size() && json[i] == ':') ++i;
                    skipWs(json, i);
                    if (mk == "name" && i < json.size() && json[i] == '"')
                        r.name = parseString(json, i);
                    else if (mk == "category" && i < json.size() && json[i] == '"')
                        r.category = parseString(json, i);
                    else
                        skipValue(json, i);
                    skipWs(json, i);
                    if (i < json.size() && json[i] == ',') ++i;
                }
            } else {
                skipValue(json, i);
            }
        } else if (key == "params") {
            if (i < json.size() && json[i] == '{') {
                ++i;
                while (i < json.size()) {
                    skipWs(json, i);
                    if (i < json.size() && json[i] == '}') { ++i; break; }
                    if (i >= json.size() || json[i] != '"') break;
                    const std::string pk = parseString(json, i);
                    skipWs(json, i);
                    if (i < json.size() && json[i] == ':') ++i;
                    skipWs(json, i);
                    const auto sc = parseScalar(json, i);
                    const int idx = paramIndex(pk);
                    if (idx >= 0) {
                        r.paramValues[static_cast<std::size_t>(idx)] =
                            sc.isBool ? (sc.isTrue ? 1.0 : 0.0) : sc.num;
                        r.paramSeen[static_cast<std::size_t>(idx)] = true;
                    }
                    skipWs(json, i);
                    if (i < json.size() && json[i] == ',') ++i;
                }
            } else {
                skipValue(json, i);
            }
        } else if (key == "seq") {
            if (i < json.size() && json[i] == '{') {
                ++i;
                int declaredCount = 0;
                int parsed = 0;
                while (i < json.size()) {
                    skipWs(json, i);
                    if (i < json.size() && json[i] == '}') { ++i; break; }
                    if (i >= json.size() || json[i] != '"') break;
                    const std::string sk = parseString(json, i);
                    skipWs(json, i);
                    if (i < json.size() && json[i] == ':') ++i;
                    skipWs(json, i);
                    if (sk == "stepCount") {
                        declaredCount = static_cast<int>(parseScalar(json, i).num);
                    } else if (sk == "steps" && i < json.size() && json[i] == '[') {
                        ++i;
                        while (i < json.size()) {
                            skipWs(json, i);
                            if (i < json.size() && json[i] == ']') { ++i; break; }
                            if (i < json.size() && json[i] == '{') {
                                ++i;
                                mw::state::SeqStep step{};
                                while (i < json.size()) {
                                    skipWs(json, i);
                                    if (i < json.size() && json[i] == '}') { ++i; break; }
                                    if (i >= json.size() || json[i] != '"') break;
                                    const std::string fk = parseString(json, i);
                                    skipWs(json, i);
                                    if (i < json.size() && json[i] == ':') ++i;
                                    skipWs(json, i);
                                    const auto sc = parseScalar(json, i);
                                    if (fk == "note") {
                                        long n = static_cast<long>(sc.num);
                                        if (n < -128) n = -128;
                                        if (n > 127) n = 127;
                                        step.noteSemitone = static_cast<std::int8_t>(n);
                                    } else if (fk == "gate") {
                                        step.gate = sc.isTrue;
                                    } else if (fk == "tie") {
                                        step.tie = sc.isTrue;
                                    } else if (fk == "rest") {
                                        step.rest = sc.isTrue;
                                    }
                                    skipWs(json, i);
                                    if (i < json.size() && json[i] == ',') ++i;
                                }
                                if (parsed < mw::state::kMaxSeqSteps) {
                                    r.extras.steps[static_cast<std::size_t>(parsed)] = step;
                                    ++parsed;
                                }
                            } else {
                                skipValue(json, i);
                            }
                            skipWs(json, i);
                            if (i < json.size() && json[i] == ',') ++i;
                        }
                    } else {
                        skipValue(json, i);
                    }
                    skipWs(json, i);
                    if (i < json.size() && json[i] == ',') ++i;
                }
                // Active step count is the min of the declared count and the steps seen,
                // clamped to capacity (mirrors task 025's projection) [§5.4; §6.2].
                int n = declaredCount < parsed ? declaredCount : parsed;
                if (n < 0) n = 0;
                if (n > mw::state::kMaxSeqSteps) n = mw::state::kMaxSeqSteps;
                r.extras.stepCount = n;
            } else {
                skipValue(json, i);
            }
        } else if (key == "arp") {
            if (i < json.size() && json[i] == '{') {
                ++i;
                while (i < json.size()) {
                    skipWs(json, i);
                    if (i < json.size() && json[i] == '}') { ++i; break; }
                    if (i >= json.size() || json[i] != '"') break;
                    const std::string ak = parseString(json, i);
                    skipWs(json, i);
                    if (i < json.size() && json[i] == ':') ++i;
                    skipWs(json, i);
                    const auto sc = parseScalar(json, i);
                    if (ak == "latch") r.extras.arpLatch = sc.isTrue;
                    skipWs(json, i);
                    if (i < json.size() && json[i] == ',') ++i;
                }
            } else {
                skipValue(json, i);
            }
        } else {
            skipValue(json, i);
        }

        skipWs(json, i);
        if (i < json.size() && json[i] == ',') ++i;
    }

    r.ok = true;
    return r;
}

// =====================================================================================
// The bake + verify entry points (message thread / build/load time)
// =====================================================================================

// Project ONE parsed preset into a baked POD entry. Param values use the kParamDefs
// default for any registry id the source omitted (mirrors task 025's "complete params"
// contract). The schemaVersion is normalized to the current engine schema if the source
// omits it. The checksum is computed AT BAKE TIME via the injected hasher over the
// entry's canonical payload image.
[[nodiscard]] inline BakedPreset bakeOne(const ParsedPreset& p, const Hasher& hasher) {
    BakedPreset e;
    e.schemaVersion = p.schemaVersion != 0
                          ? p.schemaVersion
                          : static_cast<std::uint16_t>(mw101::version::kCurrentSchemaVersion);
    e.paramCount = static_cast<std::uint16_t>(BakedPreset::kParamCount);

    for (std::size_t k = 0; k < BakedPreset::kParamCount; ++k) {
        const double v = p.paramSeen[k]
                             ? p.paramValues[k]
                             : static_cast<double>(mw::params::kParamDefs[k].defaultValue);
        e.params[k] = static_cast<float>(v);
    }

    e.extras = p.extras;

    // Fixed-width NUL-padded meta (truncated to capacity; never heap).
    const auto copyFixed = [](auto& buf, const std::string& src) {
        const std::size_t n = src.size() < buf.size() - 1 ? src.size() : buf.size() - 1;
        std::memcpy(buf.data(), src.data(), n);
        buf[n] = '\0';
    };
    copyFixed(e.nameBuf, p.name);
    copyFixed(e.categoryBuf, p.category);

    const auto img = entryPayloadImage(e);
    e.checksum = hasher(std::span<const std::byte>{ img.data(), img.size() });
    return e;
}

// Bake a whole patch set into the flat POD table, in the caller-supplied (deterministic)
// order. Parse + projection happen HERE (message thread); the returned table is the
// audio-thread POD [docs/design/11 §9.1; ADR-001 C3/C4; ADR-014 C9].
[[nodiscard]] inline BakedTable bake(const std::vector<PresetSource>& sources,
                                     const Hasher& hasher) {
    BakedTable table;
    table.reserve(sources.size());
    for (const auto& src : sources) {
        const ParsedPreset parsed = parsePreset(src.json);
        table.push(bakeOne(parsed, hasher));
    }
    return table;
}

// Verify a baked entry against its STORED checksum WITHOUT re-parsing the JSON: recompute
// the canonical payload hash with the same hasher and compare. A single corrupted payload
// byte makes this FALSE [ADR-014 C9]. This is message-thread verification at load; the
// audio thread, having only the verified POD, never parses.
[[nodiscard]] inline bool verify(const BakedPreset& e, const Hasher& hasher) {
    const auto img = entryPayloadImage(e);
    const Checksum recomputed = hasher(std::span<const std::byte>{ img.data(), img.size() });
    return recomputed == e.checksum;
}

// Verify every entry of a table (message thread / load). True iff all entries verify.
[[nodiscard]] inline bool verifyAll(const BakedTable& t, const Hasher& hasher) {
    for (std::size_t i = 0; i < t.size(); ++i)
        if (!verify(t.entry(i), hasher))
            return false;
    return true;
}

} // namespace mw::preset
